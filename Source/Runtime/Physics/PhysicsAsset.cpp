#include "EnginePCH.h"
#include "Physics/PhysicsAsset.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"
#include "Serialization/Formatter/JsonArchiveInputFormatter.h"
#include <algorithm>
#include <climits>
#include <fstream>

uint64 UPhysicsAsset::PairKey(int32 A, int32 B)
{
	const uint32 Low = static_cast<uint32>(std::min(A, B));
	const uint32 High = static_cast<uint32>(std::max(A, B));
	return (static_cast<uint64>(Low) << 32) | High;
}

int32 UPhysicsAsset::FindBodyIndex(const FName& BoneName) const
{
	if (BodySetupIndexMap.size() == static_cast<size_t>(SkeletalBodySetups.Num()))
	{
		const auto It = BodySetupIndexMap.find(BoneName.ToString());
		return It != BodySetupIndexMap.end() ? It->second : -1;
	}
	for (int32 Index = 0; Index < SkeletalBodySetups.Num(); ++Index)
		if (SkeletalBodySetups[Index] && SkeletalBodySetups[Index]->BoneName == BoneName)
			return Index;
	return -1;
}

int32 UPhysicsAsset::FindConstraintIndex(const FName& Bone1, const FName& Bone2) const
{
	for (int32 Index = 0; Index < ConstraintSetup.Num(); ++Index)
	{
		const FConstraintInstance& Instance = ConstraintSetup[Index]->DefaultInstance;
		if (Instance.ConstraintBone1 == Bone1 && Instance.ConstraintBone2 == Bone2)
			return Index;
	}
	return -1;
}

USkeletalBodySetup* UPhysicsAsset::AddBody(const FName& BoneName)
{
	if (FindBodyIndex(BoneName) >= 0)
		return nullptr;
	auto* Setup = NewObject<USkeletalBodySetup>(this);
	Setup->BoneName = BoneName;
	SkeletalBodySetups.Add(Setup);
	UpdateBodySetupIndexMap();
	bDirty = true;
	return Setup;
}

UPhysicsConstraintTemplate* UPhysicsAsset::AddConstraint(const FName& ChildBone, const FName& ParentBone)
{
	if (FindConstraintIndex(ChildBone, ParentBone) >= 0)
		return nullptr;
	auto* Template = NewObject<UPhysicsConstraintTemplate>(this);
	Template->DefaultInstance.JointName = ChildBone;
	Template->DefaultInstance.ConstraintBone1 = ChildBone;
	Template->DefaultInstance.ConstraintBone2 = ParentBone;
	ConstraintSetup.Add(Template);
	bDirty = true;
	return Template;
}

void UPhysicsAsset::ClearAll()
{
	SkeletalBodySetups.Empty();
	ConstraintSetup.Empty();
	CollisionDisableTable.Empty();
	BodySetupIndexMap.clear();
	bCollisionSetDirty = true;
	bDirty = true;
}

void UPhysicsAsset::DisableCollision(int32 BodyIndexA, int32 BodyIndexB)
{
	if (BodyIndexA == BodyIndexB || BodyIndexA < 0 || BodyIndexB < 0 || !IsCollisionEnabled(BodyIndexA, BodyIndexB))
		return;
	CollisionDisableTable.Add({std::min(BodyIndexA, BodyIndexB), std::max(BodyIndexA, BodyIndexB)});
	bCollisionSetDirty = true;
	bDirty = true;
}

void UPhysicsAsset::EnableCollision(int32 BodyIndexA, int32 BodyIndexB)
{
	const int32 Low = std::min(BodyIndexA, BodyIndexB), High = std::max(BodyIndexA, BodyIndexB);
	for (int32 Index = CollisionDisableTable.Num() - 1; Index >= 0; --Index)
		if (CollisionDisableTable[Index].BodyIndex1 == Low && CollisionDisableTable[Index].BodyIndex2 == High)
		{
			CollisionDisableTable.RemoveAt(Index, 1);
			bCollisionSetDirty = true;
			bDirty = true;
		}
}

void UPhysicsAsset::RebuildCollisionSet() const
{
	DisabledPairs.clear();
	for (const FPhysicsAssetCollisionPair& Pair : CollisionDisableTable)
		DisabledPairs.insert(PairKey(Pair.BodyIndex1, Pair.BodyIndex2));
	bCollisionSetDirty = false;
}

bool UPhysicsAsset::IsCollisionEnabled(int32 BodyIndexA, int32 BodyIndexB) const
{
	if (bCollisionSetDirty)
		RebuildCollisionSet();
	return !DisabledPairs.contains(PairKey(BodyIndexA, BodyIndexB));
}

void UPhysicsAsset::UpdateBodySetupIndexMap()
{
	BodySetupIndexMap.clear();
	for (int32 Index = 0; Index < SkeletalBodySetups.Num(); ++Index)
		if (SkeletalBodySetups[Index])
			BodySetupIndexMap.emplace(SkeletalBodySetups[Index]->BoneName.ToString(), Index);
}

void UPhysicsAsset::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	const bool bLoading = Record.GetUnderlyingArchive().IsLoading();

	// 이 필드가 생기기 전에 저장한 파일에는 없으므로 있을 때만 읽는다.
	if (auto PreviewMesh = Record.TryEnterField("PreviewMeshPath", true))
		PreviewMesh.GetValue() << PreviewMeshPath;

	// 바디·관절은 이 에셋이 소유한 하위 객체이므로 참조가 아니라 내용을 그대로 기록한다.
	int32 NumBodies = SkeletalBodySetups.Num();
	FStructuredArchive::FArray Bodies = Record.EnterArray("SkeletalBodySetups", NumBodies);
	if (bLoading)
	{
		SkeletalBodySetups.Empty();
		for (int32 Index = 0; Index < NumBodies; ++Index)
			SkeletalBodySetups.Add(NewObject<USkeletalBodySetup>(this));
	}
	for (int32 Index = 0; Index < NumBodies; ++Index)
		SkeletalBodySetups[Index]->Serialize(Bodies.EnterElement().EnterRecord());

	int32 NumConstraints = ConstraintSetup.Num();
	FStructuredArchive::FArray Constraints = Record.EnterArray("ConstraintSetup", NumConstraints);
	if (bLoading)
	{
		ConstraintSetup.Empty();
		for (int32 Index = 0; Index < NumConstraints; ++Index)
			ConstraintSetup.Add(NewObject<UPhysicsConstraintTemplate>(this));
	}
	for (int32 Index = 0; Index < NumConstraints; ++Index)
		ConstraintSetup[Index]->Serialize(Constraints.EnterElement().EnterRecord());

	if (bLoading)
	{
		UpdateBodySetupIndexMap();
		bCollisionSetDirty = true;
		for (USkeletalBodySetup* Setup : SkeletalBodySetups)
			Setup->MarkSaved();
	}
}

bool UPhysicsAsset::SaveToFile(const std::filesystem::path& Path)
{
	if (Path.empty())
		return false;
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	Writer.SetIsPersistent(true);
	{
		FJsonArchiveOutputFormatter Formatter(Writer);
		FStructuredArchive Ar(Formatter);
		Serialize(Ar.Open().EnterRecord());
		Ar.Close();
	}
	if (Writer.IsError())
		return false;
	std::error_code Error;
	std::filesystem::create_directories(Path.parent_path(), Error);
	if (Error)
		return false;
	// 새 내용을 다 쓴 뒤에만 바꾼다.
	auto Temp = Path;
	Temp += ".tmp";
	{
		std::ofstream File(Temp, std::ios::binary | std::ios::trunc);
		if (!File.is_open())
			return false;
		File.write(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num());
		File.flush();
		if (File.fail())
			return false;
	}
	if (!MoveFileExW(Temp.c_str(), Path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		return false;
	MarkSaved();
	return true;
}

bool UPhysicsAsset::LoadFromFile(const std::filesystem::path& Path)
{
	std::ifstream File(Path, std::ios::binary | std::ios::ate);
	if (!File.is_open())
		return false;
	const auto Size = File.tellg();
	if (Size <= 0 || Size >= INT_MAX)
		return false;
	TArray<uint8> Bytes;
	Bytes.SetNum(static_cast<int32>(Size));
	File.seekg(0);
	File.read(reinterpret_cast<char*>(Bytes.GetData()), Bytes.Num());
	FMemoryReader Reader(Bytes, true);
	FJsonArchiveInputFormatter Formatter(Reader);
	if (Reader.IsError())
		return false;
	FStructuredArchive Ar(Formatter);
	Serialize(Ar.Open().EnterRecord());
	Ar.Close();
	if (Reader.IsError())
		return false;
	MarkSaved();
	return true;
}

void UPhysicsAsset::InvalidatePhysicsData()
{
	bDirty = true;
	bCollisionSetDirty = true;
	UpdateBodySetupIndexMap();
	for (USkeletalBodySetup* Setup : SkeletalBodySetups)
		if (Setup)
			Setup->MarkSaved(); // 개별 BodySetup의 Dirty는 이 에셋의 Dirty로 대신한다
	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
		// 물리 상태가 있는 컴포넌트만 본다 (GetPhysicsAsset이 다른 메시의 에셋을 자동 생성하지 않게).
		if (It->IsPhysicsStateCreated() && It->GetPhysicsAsset() == this)
			It->RecreatePhysicsState();
}
