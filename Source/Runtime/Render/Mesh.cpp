#include "EnginePCH.h"
#include "Physics/BodySetup.h"
#include "Core/Misc/Paths.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"
#include "Serialization/Formatter/JsonArchiveInputFormatter.h"
#include <fstream>

#include "Mesh.h"
#include "Renderer.h"
#include "Asset/AssetManager.h"
#include "Engine/PrimitiveSceneProxy.h"
#include <algorithm>
#include <cmath>
#include <limits>

UStaticMesh::~UStaticMesh()
{
	for (FPrimitiveSceneProxy* Proxy : RenderProxies)
		Proxy->OnMeshDestroyed();
	VertexBuffer = nullptr;
	IndexBuffer = nullptr;
}

UMaterial* UStaticMesh::GetMaterial(uint32 SlotIndex) const
{
	if (SlotIndex < Materials.size() && Materials[SlotIndex])
	{
		return Materials[SlotIndex];
	}
	return UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
}

size_t UStaticMesh::FMaterialSlotsHash::operator()(const std::vector<UMaterial*>& Slots) const
{
	size_t Hash = 0;
	for (const UMaterial* Material : Slots)
		Hash ^= std::hash<const UMaterial*>{}(Material) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
	return Hash;
}

void UStaticMesh::RebuildRenderData()
{
	RenderBounds = MeshData.AABB;
	RenderStates.clear();
	for (FPrimitiveSceneProxy* Proxy : RenderProxies)
		Proxy->OnMeshRenderDataChanged();
}

TSharedPtr<const FMeshRenderState> UStaticMesh::GetRenderState(const std::vector<UMaterial*>& Materials)
{
	auto& WeakState = RenderStates[Materials];
	if (auto State = WeakState.lock())
		return State;
	auto State = MakeShared<FMeshRenderState>();
	for (const FStaticMeshSection& Section : MeshData.Sections)
	{
		UMaterial* Material = Section.MaterialSlotIndex < Materials.size()
		                          ? Materials[Section.MaterialSlotIndex]
		                          : GetMaterial(Section.MaterialSlotIndex);
		if (Material)
			State->Sections.Add({Material, Section.StartIndex, Section.IndexCount});
	}
	WeakState = State;
	return State;
}

void UStaticMesh::RegisterProxy(FPrimitiveSceneProxy* Proxy)
{
	RenderProxies.Add(Proxy);
}

void UStaticMesh::UnregisterProxy(FPrimitiveSceneProxy* Proxy)
{
	const auto It = std::find(RenderProxies.begin(), RenderProxies.end(), Proxy);
	if (It != RenderProxies.end())
		RenderProxies.RemoveAtSwap(static_cast<uint32>(It - RenderProxies.begin()));
}

namespace
{
	std::filesystem::path BodySetupPath(const UStaticMesh& Mesh)
	{
		const auto Key = FPaths::FromUtf8(Mesh.GetPath());
		if (!FPaths::HasProject() || Key.empty() || Key.is_absolute() || Key.has_root_name())
			return {};
		for (const auto& Part : Key)
			if (Part == "..")
				return {};
		auto Path = FPaths::ProjectContentDir() / "Physics" / "BodySetups" / Key;
		Path += ".bodysetup";
		return Path;
	}
}

namespace
{
	// 자동 박스로 초기화한 뒤 디스크의 .bodysetup이 있으면 덮어쓴다.
	// 과도기 경로: UE는 BodySetup을 UStaticMesh 패키지 안에 함께 저장한다. 이 엔진은 메시가 원본(.obj 등)에서
	// 바로 로드되므로 Content/Physics/BodySetups/<메시 경로>.bodysetup 별도 파일에 둔다. 메시 에셋 저장 형식이 생기면 합친다.
	// 엔진 기본 도형은 모양에 맞는 충돌을 기본값으로 쓴다 (구는 구르고 캡슐은 눕는다). 나머지는 상자.
	// 크기: Sphere 반지름 100cm, Capsule 반지름 50cm·높이 200cm, Cone 반지름·높이 100cm.
	void FitDefaultCollision(const UStaticMesh& Mesh, UBodySetup& Setup)
	{
		const FString& Path = Mesh.GetPath();
		Setup.AggGeom.EmptyElements();
		if (Path == "Sphere")
			Setup.AggGeom.SphereElems.Add(UBodySetup::MakeFitSphere(Mesh.MeshData));
		else if (Path == "Capsule")
			Setup.AggGeom.SphylElems.Add(UBodySetup::MakeFitCapsule(Mesh.MeshData));
		else if (Path == "Cone")
			Setup.AggGeom.ConvexElems.Add(UBodySetup::MakeFitConvex(Mesh.MeshData));
		else
			Setup.AggGeom.BoxElems.Add(UBodySetup::MakeFitBox(Mesh.MeshData));
	}

	void LoadBodySetupFromDisk(const UStaticMesh& Mesh, UBodySetup& Setup)
	{
		FitDefaultCollision(Mesh, Setup);
		const auto Path = BodySetupPath(Mesh);
		std::ifstream File(Path, std::ios::binary | std::ios::ate);
		if (File.is_open())
		{
			const auto Size = File.tellg();
			if (Size > 0 && Size < INT_MAX)
			{
				TArray<uint8> Bytes;
				Bytes.SetNum(static_cast<int32>(Size));
				File.seekg(0);
				File.read(reinterpret_cast<char*>(Bytes.GetData()), Bytes.Num());
				FMemoryReader Reader(Bytes, true);
				FJsonArchiveInputFormatter Formatter(Reader);
				if (!Reader.IsError())
				{
					FStructuredArchive Ar(Formatter);
					Setup.Serialize(Ar.Open().EnterRecord());
					Ar.Close();
				}
				if (Reader.IsError())
					KRS_LOG(Error, "[Physics] Failed to load collision asset for {}", Mesh.GetPath());
			}
		}
		Setup.MarkSaved();
	}
}

UBodySetup* UStaticMesh::GetBodySetup() const
{
	if (BodySetup)
		return BodySetup;
	BodySetup = NewObject<UBodySetup>(const_cast<UStaticMesh*>(this));
	LoadBodySetupFromDisk(*this, *BodySetup);
	return BodySetup;
}

void UStaticMesh::RevertBodySetup()
{
	if (!BodySetup || !BodySetup->IsDirty())
		return;
	// 컴포넌트가 이 포인터를 들고 있으므로 객체는 그대로 두고 내용만 되돌린다.
	FBodyInstance Defaults;
	Defaults.bOverrideMass = false;
	BodySetup->DefaultInstance.CopyBodyInstancePropertiesFrom(Defaults);
	BodySetup->PhysMaterial = nullptr;
	LoadBodySetupFromDisk(*this, *BodySetup);
	BodySetup->InvalidatePhysicsData();
	BodySetup->MarkSaved();
}

bool UStaticMesh::SaveBodySetup()
{
	if (!BodySetup || !BodySetup->IsDirty())
		return true;
	const auto Path = BodySetupPath(*this);
	if (Path.empty())
		return false;
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	Writer.SetIsPersistent(true);
	{
		FJsonArchiveOutputFormatter Formatter(Writer);
		FStructuredArchive Ar(Formatter);
		BodySetup->Serialize(Ar.Open().EnterRecord());
		Ar.Close();
	}
	if (Writer.IsError())
		return false;
	std::error_code Error;
	std::filesystem::create_directories(Path.parent_path(), Error);
	if (Error)
		return false;
	// Replace only after the entire new asset was written successfully.
	auto Temp = Path;
	Temp += ".tmp";
	std::ofstream File(Temp, std::ios::binary | std::ios::trunc);
	if (!File.is_open())
		return false;
	File.write(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num());
	File.flush();
	if (File.fail())
		return false;
	File.close();
	if (!MoveFileExW(Temp.c_str(), Path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		return false;
	BodySetup->MarkSaved();
	return true;
}
