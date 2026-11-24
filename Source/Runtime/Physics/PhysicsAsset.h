#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Physics/BodySetup.h"
#include "Physics/ConstraintInstance.h"
#include <filesystem>
#include <unordered_set>
#include "PhysicsAsset.generated.h"

class USkeletalMesh;

// UE USkeletalBodySetup: PhysicsAsset 안의 본 하나에 대한 충돌 형상. BoneName이 대상 본이다.
UCLASS()
class USkeletalBodySetup : public UBodySetup
{
	GENERATED_BODY()
public:
	USkeletalBodySetup() = default;
};

// UE UPhysicsConstraintTemplate: PhysicsAsset 안의 관절 정의 하나. 실행 중 관절은 DefaultInstance를 복사해 만든다.
UCLASS()
class UPhysicsConstraintTemplate : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere)
	FConstraintInstance DefaultInstance;
};

// UE FRigidBodyIndexPair. CollisionDisableTable의 한 쌍 (작은 인덱스가 앞).
USTRUCT()
struct FPhysicsAssetCollisionPair
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere)
	int32 BodyIndex1 = -1;
	UPROPERTY(EditAnywhere)
	int32 BodyIndex2 = -1;
};

// UE UPhysicsAsset. 스켈레탈 메시의 본별 충돌 형상과 바디 사이 관절 정의를 담는 공유 에셋.
// 실행 바디·관절은 USkeletalMeshComponent가 인스턴스마다 따로 만든다 (에셋에는 SDK 객체가 없다).
// 저장: 과도기 경로로 Content/Physics/PhysicsAssets/<메시 경로>.physicsasset (BodySetup과 같은 방식).
UCLASS()
class UPhysicsAsset : public UObject
{
	GENERATED_BODY()
public:
	// UE 이름. 자료의 BodySetup 배열에 대응한다.
	TArray<USkeletalBodySetup*> SkeletalBodySetups;
	TArray<UPhysicsConstraintTemplate*> ConstraintSetup;
	// 서로 충돌하지 않는 바디 쌍 (UE CollisionDisableTable)
	UPROPERTY(EditAnywhere)
	TArray<FPhysicsAssetCollisionPair> CollisionDisableTable;
	// 에디터에서 미리 보여 줄 스켈레탈 메시 경로 (UE UPhysicsAsset::PreviewSkeletalMesh). 실행에는 쓰지 않는다.
	FString PreviewMeshPath;

	int32 FindBodyIndex(const FName& BoneName) const;
	int32 FindConstraintIndex(const FName& Bone1, const FName& Bone2) const;
	USkeletalBodySetup* AddBody(const FName& BoneName);
	UPhysicsConstraintTemplate* AddConstraint(const FName& ChildBone, const FName& ParentBone);
	void ClearAll();

	void DisableCollision(int32 BodyIndexA, int32 BodyIndexB);
	void EnableCollision(int32 BodyIndexA, int32 BodyIndexB);
	bool IsCollisionEnabled(int32 BodyIndexA, int32 BodyIndexB) const;
	// 본 이름 → 바디 인덱스 검색표를 다시 만든다. 바디 목록을 바꾼 뒤 부른다 (UE UpdateBodySetupIndexMap).
	void UpdateBodySetupIndexMap();

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;
	bool SaveToFile(const std::filesystem::path& Path);
	bool LoadFromFile(const std::filesystem::path& Path);

	bool IsDirty() const { return bDirty; }
	void MarkSaved() { bDirty = false; }
	// 정의가 바뀌었음을 알리고, 이 에셋으로 만든 실행 바디를 다시 만든다.
	void InvalidatePhysicsData();

private:
	static uint64 PairKey(int32 A, int32 B);
	void RebuildCollisionSet() const;
	std::unordered_map<FString, int32> BodySetupIndexMap;
	mutable std::unordered_set<uint64> DisabledPairs;
	mutable bool bCollisionSetDirty = true;
	bool bDirty = false;
};
