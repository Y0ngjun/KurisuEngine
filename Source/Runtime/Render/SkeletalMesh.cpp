#include "EnginePCH.h"
#include "SkeletalMesh.h"
#include "Render/RenderCommand.h"
#include "Asset/AssetManager.h"
#include "Core/Misc/Paths.h"
#include "Physics/PhysicsAsset.h"
#include "Asset/AssetUtils.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/UObjectIterator.h"
#include "Core/Misc/Guid.h"
#include "Physics/PhysicsAssetUtils.h"
#include "UObject/UObjectGlobals.h"

#include <cmath>

namespace
{
	std::filesystem::path PhysicsAssetPath(const USkeletalMesh& Mesh)
	{
		const auto Key = FPaths::FromUtf8(Mesh.GetPath());
		if (!FPaths::HasProject() || Key.empty() || Key.is_absolute() || Key.has_root_name())
			return {};
		for (const auto& Part : Key)
			if (Part == "..")
				return {};
		auto Path = FPaths::ProjectContentDir() / "Physics" / "PhysicsAssets" / Key;
		Path += ".physicsasset";
		return Path;
	}

	void LoadOrCreatePhysicsAsset(const USkeletalMesh& Mesh, UPhysicsAsset& Asset)
	{
		const auto Path = PhysicsAssetPath(Mesh);
		if (!Path.empty() && std::filesystem::exists(Path))
		{
			if (Asset.LoadFromFile(Path))
				return;
			KRS_LOG(Error, "[Physics] Failed to load physics asset for {}; regenerating.", Mesh.GetPath());
		}
		if (!FPhysicsAssetUtils::CreateFromSkeletalMesh(Asset, Mesh))
			Asset.ClearAll();
	}

	TArray<FBox> BuildBoneBounds(const FSkeletalMeshLODRenderData& LOD, int32 NumBones)
	{
		TArray<FBox> Bounds;
		Bounds.SetNum(NumBones);
		for (FBox& Box : Bounds)
			Box = FBox{FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX)};

		for (int32 V = 0; V < LOD.BindVertices.Num(); ++V)
		{
			const FVector& Position = LOD.BindVertices[V].Position;
			const FSkinWeight& Weight = LOD.SkinWeights[V];
			for (int32 K = 0; K < MaxBoneInfluences; ++K)
			{
				if (Weight.Weight[K] <= 0.0f)
					continue;
				FBox& Box = Bounds[Weight.BoneIndex[K]];
				for (int Axis = 0; Axis < 3; ++Axis)
				{
					Box.Min[Axis] = std::min(Box.Min[Axis], Position[Axis]);
					Box.Max[Axis] = std::max(Box.Max[Axis], Position[Axis]);
				}
			}
		}
		return Bounds;
	}

	 bool IsFiniteClothPosition(const FVector& P)
	{
		return std::isfinite(P.X) &&
		       std::isfinite(P.Y) &&
		       std::isfinite(P.Z);
	}

	bool IsValidClothSkinWeight(const FSkinWeight& Skin, int32 NumBones)
	{
		float Sum = 0.0f;

		for (int32 I = 0; I < MaxBoneInfluences; ++I)
		{
			const float Weight = Skin.Weight[I];
			if (!std::isfinite(Weight) || Weight < 0.0f ||
			    (Weight > 0.0f && Skin.BoneIndex[I] >= NumBones))
			{
				return false;
			}

			Sum += Weight;
		}

		return std::fabs(Sum - 1.0f) <= 1.0e-3f;
	}

	bool HasSameClothSkinWeights(const FSkinWeight& A, const FSkinWeight& B)
	{
		for (int32 I = 0; I < MaxBoneInfluences; ++I)
		{
			if (A.Weight[I] != B.Weight[I] ||
			    (A.Weight[I] > 0.0f && A.BoneIndex[I] != B.BoneIndex[I]))
			{
				return false;
			}
		}

		return true;
	}
}

bool USkeletalMesh::Build(FSkeletalMeshData&& ImportData, FString& OutError)
{
	// CreateStaticMesh와 같은 기본값: 슬롯/섹션이 없으면 하나씩 채운다
	if (ImportData.MaterialSlots.IsEmpty())
	{
		FSkeletalMaterialSlot DefaultSlot;
		DefaultSlot.Name = "Default";
		ImportData.MaterialSlots.Add(DefaultSlot);
	}
	if (ImportData.Sections.IsEmpty())
	{
		FSkeletalMeshSection DefaultSection;
		DefaultSection.StartIndex = 0;
		DefaultSection.IndexCount = static_cast<uint32>(ImportData.Indices.Num());
		DefaultSection.MaterialSlotIndex = 0;
		ImportData.Sections.Add(DefaultSection);
	}

	if (!ImportData.Validate(OutError))
	{
		return false;
	}

	// 공유 IB. VB는 인스턴스마다 FSkeletalMeshObjectCPUSkin이 동적으로 만든다
	TUniquePtr<FIndexBuffer> IB = RenderCommand::CreateStaticIndexBuffer(ImportData.Indices.GetData(), static_cast<uint32>(ImportData.Indices.Num()));
	if (!IB)
	{
		OutError = "SkeletalMesh: index buffer creation failed";
		return false;
	}

	TArray<UMaterial*> NewMaterials;
	if (!UAssetManager::BuildMaterialsFromSlots(ImportData.MaterialSlots, NewMaterials))
	{
		OutError = "SkeletalMesh: material creation failed";
		return false;
	}

	FSkeletalMeshLODRenderData LOD;
	LOD.BindVertices = std::move(ImportData.BindVertices);
	LOD.SkinWeights = std::move(ImportData.SkinWeight);
	LOD.Indices = std::move(ImportData.Indices);
	LOD.Sections = std::move(ImportData.Sections);
	LOD.IndexBuffer = std::move(IB);

	// 임포터가 채운 값에 의존하지 않고 바인드 정점에서 다시 계산
	LOD.BindAABB.Min = LOD.BindAABB.Max = LOD.BindVertices[0].Position;
	for (const FVertex& Vertex : LOD.BindVertices)
	{
		for (int Axis = 0; Axis < 3; ++Axis)
		{
			LOD.BindAABB.Min[Axis] = std::min(LOD.BindAABB.Min[Axis], Vertex.Position[Axis]);
			LOD.BindAABB.Max[Axis] = std::max(LOD.BindAABB.Max[Axis], Vertex.Position[Axis]);
		}
	}

	TArray<FGpuSkinVertex> GpuSkinVertices;
	GpuSkinVertices.SetNum(LOD.BindVertices.Num());
	for (int32 V = 0; V < GpuSkinVertices.Num(); ++V)
	{
		GpuSkinVertices[V].Vertex = LOD.BindVertices[V];
		for (int32 K = 0; K < MaxBoneInfluences; ++K)
		{
			GpuSkinVertices[V].BoneIndex[K] = LOD.SkinWeights[V].BoneIndex[K];
			GpuSkinVertices[V].Weight[K] = LOD.SkinWeights[V].Weight[K];
		}
	}
	LOD.GpuSkinVertexBuffer = RenderCommand::CreateStaticVertexBuffer(GpuSkinVertices.GetData(),
	                                                                  sizeof(FGpuSkinVertex) * static_cast<uint32>(GpuSkinVertices.Num()), sizeof(FGpuSkinVertex));
	LOD.BoneBounds = BuildBoneBounds(LOD, ImportData.Bones.Num());

	RefSkeleton = std::move(ImportData.Bones);
	MaterialSlots = std::move(ImportData.MaterialSlots);
	Materials = std::move(NewMaterials);
	RenderData.LODRenderData.Empty();
	RenderData.LODRenderData.Add(std::move(LOD));
	ClothingBindings.Empty();
	AssetData = {};
	bAssetDirty = false;
	RenderStates.Reset();
	return true;
}

int32 USkeletalMesh::FindBoneIndex(const FString& Name) const
{
	for (int32 I = 0; I < RefSkeleton.Num(); ++I)
	{
		if (RefSkeleton[I].Name == Name)
			return I;
	}
	return -1;
}

UMaterial* USkeletalMesh::GetMaterial(uint32 SlotIndex) const
{
	if (SlotIndex < Materials.size() && Materials[SlotIndex])
	{
		return Materials[SlotIndex];
	}
	return UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
}

size_t USkeletalMesh::FMaterialSlotsHash::operator()(const TArray<UMaterial*>& Slots) const
{
	size_t Hash = 0;
	for (const UMaterial* Material : Slots)
		Hash ^= std::hash<const UMaterial*>{}(Material) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
	return Hash;
}

TSharedPtr<const FMeshRenderState> USkeletalMesh::GetRenderState(const TArray<UMaterial*>& InMaterials)
{
	auto& WeakState = RenderStates[InMaterials];
	if (auto State = WeakState.lock())
		return State;

	// LOD는 하나뿐이라 임계값 없이 LOD 0 범위만 채운다
	auto State = MakeShared<FMeshRenderState>();
	if (!RenderData.LODRenderData.IsEmpty())
	{
		for (const FSkeletalMeshSection& Section : RenderData.LODRenderData[0].Sections)
		{
			UMaterial* Material = Section.MaterialSlotIndex < InMaterials.size()
			                          ? InMaterials[Section.MaterialSlotIndex]
			                          : GetMaterial(Section.MaterialSlotIndex);
			if (Material)
				State->Sections.Add({Material, Section.StartIndex, Section.IndexCount});
		}
	}
	WeakState = State;
	return State;
}

UPhysicsAsset* USkeletalMesh::GetPhysicsAsset() const
{
	if (IsSkeletalMeshAsset())
		return PhysicsAsset;
	if (!PhysicsAsset)
	{
		PhysicsAsset = NewObject<UPhysicsAsset>(const_cast<USkeletalMesh*>(this));
		if (PhysicsAsset)
			LoadOrCreatePhysicsAsset(*this, *PhysicsAsset);
	}
	return PhysicsAsset;
}

void USkeletalMesh::SetPhysicsAsset(UPhysicsAsset* InPhysicsAsset)
{
	if (PhysicsAsset == InPhysicsAsset)
		return;
	PhysicsAsset = InPhysicsAsset;
	if (PhysicsAsset)
		PhysicsAsset->InvalidatePhysicsData();
}

bool USkeletalMesh::SavePhysicsAsset()
{
	UPhysicsAsset* Asset = GetPhysicsAsset();
	const auto Path = IsSkeletalMeshAsset()
	                      ? FPaths::FromUtf8(SkeletalMeshAssetFile::ResolveContentReference(AssetData.PhysicsAssetPath))
	                      : PhysicsAssetPath(*this);
	return Asset && !Path.empty() && Asset->SaveToFile(Path);
}

bool USkeletalMesh::RevertPhysicsAsset()
{
	UPhysicsAsset* Asset = GetPhysicsAsset();
	if (!Asset)
		return false;
	// 컴포넌트가 이 포인터를 들고 있으므로 객체는 그대로 두고 내용만 바꾼다.
	if (IsSkeletalMeshAsset())
	{
		const auto Path = FPaths::FromUtf8(SkeletalMeshAssetFile::ResolveContentReference(AssetData.PhysicsAssetPath));
		if (Path.empty() || !Asset->LoadFromFile(Path))
			return false;
	}
	else
	{
		LoadOrCreatePhysicsAsset(*this, *Asset);
	}
	Asset->InvalidatePhysicsData();
	Asset->MarkSaved();
	return true;
}

void USkeletalMesh::ApplyAssetData(FSkeletalMeshAssetData&& Data)
{
	const bool ReloadPhysics = !IsSkeletalMeshAsset() || AssetData.PhysicsAssetPath != Data.PhysicsAssetPath;
	AssetData = std::move(Data);
	if (ReloadPhysics)
	{
		UPhysicsAsset* Asset = nullptr;
		if (!AssetData.PhysicsAssetPath.empty())
		{
			const auto Path = FPaths::FromUtf8(SkeletalMeshAssetFile::ResolveContentReference(AssetData.PhysicsAssetPath));
			Asset = NewObject<UPhysicsAsset>(this);
			if (Path.empty() || !Asset->LoadFromFile(Path))
			{
				delete Asset;
				Asset = nullptr;
				KRS_LOG(Warning, "Cannot load Physics Asset: {}", AssetData.PhysicsAssetPath);
			}
		}
		ReplaceAssignedPhysicsAsset(Asset);
	}
	RebuildClothingBindings();
	bAssetDirty = false;
}

bool USkeletalMesh::AssignPhysicsAssetPath(const FString& Path, FString& OutError)
{
	OutError.clear();
	if (!IsSkeletalMeshAsset())
	{
		OutError = "Create a .skeletalmesh asset before assigning a Physics Asset";
		return false;
	}
	const FString Reference = Path.empty() ? FString{} : SkeletalMeshAssetFile::ProjectContentReference(Path);
	if (!Path.empty() && (Reference.empty() || !Reference.ends_with(".physicsasset")))
	{
		OutError = "Select a .physicsasset under project Content";
		return false;
	}
	UPhysicsAsset* Asset = nullptr;
	if (!Reference.empty())
	{
		Asset = NewObject<UPhysicsAsset>(this);
		if (!Asset->LoadFromFile(FPaths::FromUtf8(SkeletalMeshAssetFile::ResolveContentReference(Reference))))
		{
			delete Asset;
			OutError = "Cannot load Physics Asset: " + Reference;
			return false;
		}
	}

	AssetData.PhysicsAssetPath = Reference;
	ReplaceAssignedPhysicsAsset(Asset);
	MarkAssetDirty();
	return true;
}

void USkeletalMesh::ReplaceAssignedPhysicsAsset(UPhysicsAsset* Asset)
{
	auto* Previous = PhysicsAsset;
	PhysicsAsset = Asset;
	if (Previous == Asset)
		return;
	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
		if (It->GetSkeletalMesh() == this && It->IsRegistered() && It->GetPhysicsAsset() == Asset)
			It->RecreatePhysicsState();
	delete Previous;
}

bool USkeletalMesh::SaveSkeletalMeshAsset(FString& OutError)
{
	if (AssetNeedsReload())
	{
		OutError = "Restart the editor to use the reimported skeletal mesh before saving";
		return false;
	}
	if (!IsSkeletalMeshAsset())
	{
		OutError = "Create a .skeletalmesh asset before saving";
		return false;
	}
	const FString Path = FAssetUtils::ResolveAssetFilePath(GetPath());
	if (!SkeletalMeshAssetFile::Write(FPaths::FromUtf8(Path), AssetData, OutError))
		return false;
	bAssetDirty = false;
	return true;
}

bool USkeletalMesh::RevertSkeletalMeshAsset(FString& OutError)
{
	if (!IsSkeletalMeshAsset() || AssetNeedsReload())
	{
		OutError = "Skeletal Mesh cannot be reverted in its current state";
		return false;
	}
	const FString Path = FAssetUtils::ResolveAssetFilePath(GetPath());
	FSkeletalMeshAssetData Saved;
	if (!SkeletalMeshAssetFile::Read(FPaths::FromUtf8(Path), Saved, OutError))
		return false;
	if (Saved.BakedMeshPath != AssetData.BakedMeshPath)
	{
		OutError = "Skeletal Mesh bake changed on disk. Restart the editor to re-open it";
		return false;
	}
	ApplyAssetData(std::move(Saved));
	return true;
}

FString USkeletalMesh::GetSectionSignature(int32 RenderLOD, int32 SectionIndex) const
{
	if (!RenderData.LODRenderData.IsValidIndex(RenderLOD))
		return {};
	const auto& LOD = RenderData.LODRenderData[RenderLOD];
	if (!LOD.Sections.IsValidIndex(SectionIndex))
		return {};
	return SkeletalMeshAssetFile::SectionSignature(LOD.BindVertices, LOD.SkinWeights, LOD.Indices,
	                                               RefSkeleton, LOD.Sections[SectionIndex]);
}

bool USkeletalMesh::CanBindClothingSection(int32 RenderLOD, int32 SectionIndex, int32 AssetIndex, int32 ClothLODIndex) const
{
	const auto* Asset = GetClothingAsset(AssetIndex);
	if (!Asset || !Asset->LODs.IsValidIndex(ClothLODIndex) ||
	    !RenderData.LODRenderData.IsValidIndex(RenderLOD))
		return false;
	const auto& Render = RenderData.LODRenderData[RenderLOD];
	if (!Render.Sections.IsValidIndex(SectionIndex))
		return false;
	const auto& Cloth = Asset->LODs[ClothLODIndex];
	const int32 Count = Cloth.RestPositions.Num();
	if (Cloth.SourceRenderLOD != RenderLOD || Cloth.SourceSectionIndex != SectionIndex ||
	    Count == 0 || Cloth.SkinWeights.Num() != Count || Cloth.MaxDistances.Num() != Count ||
	    Cloth.RenderToSimVertex.Num() != Render.BindVertices.Num() ||
	    Cloth.TriangleIndices.IsEmpty() || Cloth.TriangleIndices.Num() % 3 != 0)
		return false;
	for (int32 Index = 0; Index < Count; ++Index)
		if (!IsFiniteClothPosition(Cloth.RestPositions[Index]) ||
		    !IsValidClothSkinWeight(Cloth.SkinWeights[Index], GetNumBones()) ||
		    !std::isfinite(Cloth.MaxDistances[Index]) || Cloth.MaxDistances[Index] < 0)
			return false;
	for (int32 Index : Cloth.RenderToSimVertex)
		if (Index < -1 || Index >= Count)
			return false;
	for (uint32 Index : Cloth.TriangleIndices)
		if (Index >= static_cast<uint32>(Count))
			return false;
	const auto& Section = Render.Sections[SectionIndex];
	if (Cloth.TriangleIndices.Num() != Section.IndexCount)
		return false;
	for (uint32 Index = 0; Index < Section.IndexCount; ++Index)
		if (Cloth.RenderToSimVertex[Render.Indices[Section.StartIndex + Index]] !=
		    static_cast<int32>(Cloth.TriangleIndices[Index]))
			return false;
	return true;
}

void USkeletalMesh::RebuildClothingBindings()
{
	ClothingBindings.Empty();
	for (const auto& Binding : AssetData.ClothingBindings)
	{
		if (!Binding.Enabled || Binding.SectionSignature.empty() ||
		    GetSectionSignature(Binding.RenderLOD, Binding.SectionIndex) != Binding.SectionSignature)
			continue;
		for (int32 Index = 0; Index < GetClothingAssetCount(); ++Index)
		{
			if (GetClothingAsset(Index)->Id == Binding.ClothingAssetId &&
			    CanBindClothingSection(Binding.RenderLOD, Binding.SectionIndex, Index, Binding.ClothLODIndex) &&
			    GetClothingAsset(Index)->LODs[Binding.ClothLODIndex].SourceSectionSignature == Binding.SectionSignature)
			{
				ClothingBindings.Add(FClothingSectionBinding{Binding.RenderLOD, Binding.SectionIndex, Index, Binding.ClothLODIndex});
				break;
			}
		}
	}
}

int32 USkeletalMesh::CreateClothingAssetFromSection(
    int32 RenderLOD,
    int32 SectionIndex,
    const FString& Name,
    std::span<const float> RenderMaxDistances,
    FString& OutError,
    std::span<const uint32> WeldGroups)
{
	OutError.clear();

	const auto Fail = [&](const char* Message) -> int32
	{
		OutError = Message;
		return -1;
	};

	if (!RenderData.LODRenderData.IsValidIndex(RenderLOD))
		return Fail("Invalid render LOD");

	const auto& LOD = RenderData.LODRenderData[RenderLOD];
	if (!LOD.Sections.IsValidIndex(SectionIndex))
		return Fail("Invalid render section");

	const auto& Section = LOD.Sections[SectionIndex];
	const uint64 EndIndex = static_cast<uint64>(Section.StartIndex) +
	                        Section.IndexCount;

	if (Section.IndexCount == 0 ||
	    Section.IndexCount % 3 != 0 ||
	    EndIndex > static_cast<uint64>(LOD.Indices.Num()))
	{
		return Fail("Invalid section index range");
	}

	const int32 VertexCount = LOD.BindVertices.Num();
	if (LOD.SkinWeights.Num() != VertexCount ||
	    RenderMaxDistances.size() != static_cast<size_t>(VertexCount) ||
	    (!WeldGroups.empty() &&
	     WeldGroups.size() != static_cast<size_t>(VertexCount)))
	{
		return Fail("Invalid skin weight, Max Distance, or weld group count");
	}

	FClothingLODData ClothLOD;
	ClothLOD.SourceRenderLOD = RenderLOD;
	ClothLOD.SourceSectionIndex = SectionIndex;
	ClothLOD.SourceSectionSignature = GetSectionSignature(RenderLOD, SectionIndex);
	ClothLOD.RenderToSimVertex.Init(-1, static_cast<uint32>(VertexCount));

	std::unordered_map<uint32, int32> WeldGroupToSim;

	for (uint32 Index = Section.StartIndex; Index < EndIndex; ++Index)
	{
		const uint32 RenderVertex = LOD.Indices[Index];
		if (RenderVertex >= static_cast<uint32>(VertexCount))
			return Fail("Section references an invalid render vertex");

		int32& SimVertex = ClothLOD.RenderToSimVertex[RenderVertex];
		if (SimVertex < 0)
		{
			const FVector& Position = LOD.BindVertices[RenderVertex].Position;
			const FSkinWeight& Skin = LOD.SkinWeights[RenderVertex];
			const float MaxDistance = RenderMaxDistances[RenderVertex];

			if (!IsFiniteClothPosition(Position) ||
			    !IsValidClothSkinWeight(Skin, GetNumBones()) ||
			    !std::isfinite(MaxDistance) || MaxDistance < 0.0f)
			{
				return Fail("Invalid cloth position, skin weight, or Max Distance");
			}

			if (!WeldGroups.empty())
			{
				const uint32 Group = WeldGroups[RenderVertex];
				const auto [It, bInserted] =
				    WeldGroupToSim.try_emplace(Group, ClothLOD.RestPositions.Num());

				if (!bInserted)
				{
					const int32 Existing = It->second;
					const FVector& Rest = ClothLOD.RestPositions[Existing];

					if (Position.X != Rest.X || Position.Y != Rest.Y ||
					    Position.Z != Rest.Z ||
					    MaxDistance != ClothLOD.MaxDistances[Existing] ||
					    !HasSameClothSkinWeights(Skin, ClothLOD.SkinWeights[Existing]))
					{
						return Fail("Weld group contains incompatible vertices");
					}

					SimVertex = Existing;
				}
			}

			if (SimVertex < 0)
			{
				SimVertex = ClothLOD.RestPositions.Num();
				ClothLOD.RestPositions.Add(Position);
				ClothLOD.SkinWeights.Add(Skin);
				ClothLOD.MaxDistances.Add(MaxDistance);
			}
		}

		ClothLOD.TriangleIndices.Add(static_cast<uint32>(SimVertex));
	}

	for (int32 I = 0; I < ClothLOD.TriangleIndices.Num(); I += 3)
	{
		const uint32 A = ClothLOD.TriangleIndices[I];
		const uint32 B = ClothLOD.TriangleIndices[I + 1];
		const uint32 C = ClothLOD.TriangleIndices[I + 2];

		const FVector AB = ClothLOD.RestPositions[B] - ClothLOD.RestPositions[A];
		const FVector AC = ClothLOD.RestPositions[C] - ClothLOD.RestPositions[A];
		const float AreaSquaredTimesFour = AB.Cross(AC).SizeSquared();

		if (A == B || B == C || A == C ||
		    !std::isfinite(AreaSquaredTimesFour) ||
		    AreaSquaredTimesFour <= 0.0f)
		{
			return Fail("Degenerate cloth triangle");
		}
	}

	FClothingAsset Asset;
	Asset.Name = Name;
	Asset.LODs.Add(std::move(ClothLOD));
	Asset.Id = FGuid::NewGuid().ToString();
	MarkAssetDirty();
	return static_cast<int32>(AssetData.ClothingAssets.Add(std::move(Asset)));
}

bool USkeletalMesh::BindClothingSection(
    int32 RenderLOD, int32 SectionIndex,
    int32 AssetIndex, int32 ClothLODIndex,
    FString& OutError)
{
	OutError.clear();

	if (!CanBindClothingSection(RenderLOD, SectionIndex, AssetIndex, ClothLODIndex))
	{
		OutError = "Invalid Cloth mapping for this section. Create Cloth for the current geometry.";
		return false;
	}
	const auto Signature = GetSectionSignature(RenderLOD, SectionIndex);
	const auto& AssetId = GetClothingAsset(AssetIndex)->Id;
	if (Signature.empty() || GetClothingAsset(AssetIndex)->LODs[ClothLODIndex].SourceSectionSignature != Signature)
	{
		OutError = "Cloth was built for older geometry. Create Cloth for the current section.";
		return false;
	}
	UnbindClothingSection(RenderLOD, SectionIndex);
	AssetData.ClothingBindings.Add(FSkeletalMeshAssetBinding{RenderLOD, SectionIndex, AssetId, ClothLODIndex, Signature, true});
	RebuildClothingBindings();
	MarkAssetDirty();
	return true;
}

bool USkeletalMesh::UnbindClothingSection(int32 RenderLOD, int32 SectionIndex)
{
	for (int32 I = 0; I < AssetData.ClothingBindings.Num(); ++I)
	{
		const auto& Binding = AssetData.ClothingBindings[I];
		if (Binding.RenderLOD == RenderLOD && Binding.SectionIndex == SectionIndex)
		{
			AssetData.ClothingBindings.RemoveAt(static_cast<uint32>(I));
			RebuildClothingBindings();
			MarkAssetDirty();
			return true;
		}
	}

	return false;
}

const FClothingLODData* USkeletalMesh::FindClothingLOD(
    int32 RenderLOD, int32 SectionIndex) const
{
	for (const auto& Binding : ClothingBindings)
	{
		if (Binding.RenderLOD == RenderLOD && Binding.SectionIndex == SectionIndex)
		{
			return &AssetData.ClothingAssets[Binding.ClothingAssetIndex]
			            .LODs[Binding.ClothLODIndex];
		}
	}

	return nullptr;
}
