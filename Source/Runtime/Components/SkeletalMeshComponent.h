#pragma once

#include "SkinnedMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "SkeletalMeshComponent.generated.h"

class UAnimInstance;
class UAnimSequence;
class UBlendSpace1D;
class UWorld;
class FLineBatcher;

// World의 SkeletalMeshComponent마다 root 디버그를 그린다 (DrawCollisionShapes와 같은 방식). PIE의 Show Root에서 호출
void DrawSkeletalRootDebug(UWorld* World, FLineBatcher* LineBatcher);
// Settings > Debug Draw (PIE) > Show Root 값. 에디터가 매 프레임 갱신하고, 런타임 액터("root" 라벨 등)가 읽는다
extern bool GShowSkeletalRootDebug;
class UAnimationAsset;
class UAnimSingleNodeInstance;
class UAnimGraphInstance;
class UPhysicsAsset;
struct FConstraintInstance;
struct FClothingLODData;

UCLASS()
class USkeletalMeshComponent : public USkinnedMeshComponent
{
	GENERATED_BODY()

public:
	USkeletalMeshComponent();
	~USkeletalMeshComponent() override;

	void OnRegister() override;
	void OnUnregister() override;

	bool SetAnimBlueprint(const FString& Path);
	bool ReloadAnimBlueprint();
	UAnimGraphInstance* CreateAnimGraph();
	const FString& GetAnimBlueprintPath() const;
	const FString& GetAnimBlueprintError() const;
	void SetAnimInstance(UAnimInstance* InInstance);
	UAnimSingleNodeInstance* GetSingleNodeInstance() const;
	UAnimInstance* GetAnimInstance() const { return AnimInstance; }
	void BeginPlay() override;
	void TickComponent(float DeltaTime) override;

	const TArray<FBoneInfo>& GetBones() const;
	int32 FindBoneIndex(const FString& Name) const;
	FTransform GetBoneLocalTransform(int32 BoneIndex) const;
	void SetBoneLocalTransform(int32 BoneIndex, const FTransform& Local);
	const TArray<FTransform>& GetBoneSpaceTransforms() const { return BoneSpaceTransforms; }
	void SetBoneSpaceTransforms(const struct FCompactPose& Pose); // 애니메이션 평가 결과를 받는 입구
	FMatrix GetBoneComponentMatrix(int32 BoneIndex) const;
	FMatrix GetBoneWorldMatrix(int32 BoneIndex) const;

	void ResetPose();
	void RefreshBoneTransformsNow();

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	void SetAnimation(UAnimationAsset* NewAnimToPlay);
	void Play(bool bLooping = true);
	void Stop();
	bool IsPlaying() const;
	void PlayAnimation(UAnimationAsset* NewAnimToPlay, bool bLoop = true);
	void RefreshAnimationPose();

	UPROPERTY()
	float BlendSpaceInput = 300.0f;

	// 루트 모션: 이번 Tick까지 모인 이동량(월드 공간). CharacterMovement가 가져가 Actor를 움직인다
	bool HasPendingRootMotion() const { return bHasPendingRootMotion; }
	FTransform ConsumeRootMotion();

	// root 부모 공간 delta → 월드 공간 delta (Unreal ConvertLocalRootMotionToWorld + Armature 변환). 시퀀스 뷰어 미리보기도 쓴다
	FTransform ConvertRootMotionToWorld(const FTransform& ParentSpaceDelta) const;

	// 루트 모션이 뽑히는 본. "root"를 먼저, 없으면 첫 본 (UAnimSequence::FindRootTrack과 같은 규칙)
	int32 GetRootMotionBoneIndex() const;

	// 디버그: root 본 구체 + 액터 위치(root 높이)에서 root까지의 선
	void DrawRootDebug(FLineBatcher* LineBatcher) const;

	// ---- 물리 (UE USkeletalMeshComponent의 Bodies/Constraints) ----
	// PhysicsAsset의 본마다 바디를, 관절 정의마다 관절을 이 컴포넌트가 따로 만든다 (에셋은 공유, 실행 바디는 인스턴스별).
	// 충돌 설정(CollisionEnabled·채널·응답·Hit 알림)과 시뮬레이션 여부는 이 컴포넌트의 BodyInstance를 따르고,
	// 질량·감쇠는 각 USkeletalBodySetup의 DefaultInstance를 따른다.
	// 시뮬레이션하지 않는 바디는 매 Tick 애니메이션 포즈를 따라가고(키네마틱), 시뮬레이션하는 바디는 결과를
	// 컴포넌트 공간 본 포즈(스키닝 입력)에 섞는다. 애니메이션 포즈 자체는 바뀌지 않는다.
	// 컴포넌트 자체는 움직이지 않는다: 래그돌은 컴포넌트 공간에서 본만 움직인다 (UE 기본 동작).
	TArray<FBodyInstance*> Bodies;
	TArray<FConstraintInstance*> Constraints;

	// PhysicsAssetOverride가 있으면 그것을, 없으면 메시의 PhysicsAsset을 쓴다.
	UPhysicsAsset* GetPhysicsAsset() const;
	void SetPhysicsAsset(UPhysicsAsset* InPhysicsAsset);

	virtual FBodyInstance* FindBodyInstance(const FName& BoneName) override;
	FConstraintInstance* FindConstraintInstance(const FName& JointName) const;
	virtual bool IsBodyPairCollisionEnabled(int32 BodyIndexA, int32 BodyIndexB) const override;
	virtual void SyncComponentToRBPhysics() override;

	// 래그돌 제어 (UE 이름). 바디를 다시 만들지 않고 키네마틱/시뮬레이션만 바꾼다.
	void SetAllBodiesSimulatePhysics(bool bNewSimulate);
	// InBoneName과 그 아래 본의 바디. 부분 래그돌(팔만 늘어뜨리기 등)에 쓴다.
	void SetAllBodiesBelowSimulatePhysics(const FName& InBoneName, bool bNewSimulate, bool bIncludeSelf = true);
	void SetAllBodiesPhysicsBlendWeight(float PhysicsBlendWeight);
	void SetAllBodiesBelowPhysicsBlendWeight(const FName& InBoneName, float PhysicsBlendWeight, bool bIncludeSelf = true);
	bool IsAnyBodySimulating() const;
	// 이 본 위치에 충격을 준다 (UE AddImpulse(Impulse, BoneName)). 본 이름이 없으면 모든 바디.
	void AddImpulseToBone(const FVector& Impulse, const FName& BoneName, bool bVelChange = false);

	bool BuildClothPose(const FClothingLODData& ClothLOD,
	                    TArray<FVector>& OutPositions) const;

protected:
	virtual bool ShouldCreatePhysicsState() const override;
	virtual bool OnCreatePhysicsState() override;
	virtual void OnDestroyPhysicsState() override;
	// UE InstantiatePhysicsAssetBodies_Internal: 모든 바디를 만든 다음 관절을 잇는다. 하나라도 실패하면 false.
	bool InstantiatePhysicsAssetBodies_Internal(class FPhysScene& Scene);
	// 관절을 먼저, 그다음 바디를 해제한다.
	void TermArticulated();
	// PrePhysics: 시뮬레이션하지 않는 바디를 현재 본 포즈로 옮긴다 (UE UpdateKinematicBonesToAnim).
	void UpdateKinematicBonesToAnim();
	// 시뮬레이션 바디의 포즈를 컴포넌트 공간 본 포즈에 덮고 PhysicsBlendWeight로 애니메이션과 섞는다 (UE BlendPhysicsBones).
	// 애니메이션 포즈(BoneSpaceTransforms)는 바꾸지 않는다. RefreshBoneTransforms가 부른다.
	void BlendPhysicsBones();
	bool IsBoneChildOf(int32 BoneIndex, int32 ParentBoneIndex) const;

protected:
	// 단일 애니메이션 에셋 경로/루프/재생 속도를 씬에 저장·복원한다 (Serialize에서 부른다)
	void SerializeSingleAnimation(FStructuredArchive::FRecord& Record);

	void RefreshBoneTransforms() override;
	void OnSkeletalMeshChanged() override;
	void OnSkeletalMeshChanging() override;
	void OnSkeletalMeshReplaced() override;

private:
	UPROPERTY()
	FString AnimBlueprintPath;

	TUniquePtr<UAnimGraphInstance> ManagedAnimGraph;
	FString AnimBlueprintError;
	bool bBlueprintReloadRequested = false;
	bool bUpdatingAnimation = false;

	UPROPERTY()
	UAnimInstance* AnimInstance = nullptr;

	TArray<FTransform> BoneSpaceTransforms;

	FTransform PendingRootMotion; // 월드 공간 누적값
	bool bHasPendingRootMotion = false;

	UPhysicsAsset* PhysicsAssetOverride = nullptr;
	uint64 LastPhysicsSyncCount = 0;
};
