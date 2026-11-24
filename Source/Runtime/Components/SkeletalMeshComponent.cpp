#include "EnginePCH.h"
#include "SkeletalMeshComponent.h"
#include "Serialization/MathSerialization.h"
#include "Animation/BonePose.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimGraphInstance.h"
#include "Animation/AnimBlueprintCompiler.h"
#include "Animation/AnimBlueprintFile.h"
#include "Asset/AssetUtils.h"
#include "Core/Misc/Paths.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Casts.h"
#include "UObject/UObjectIterator.h"
#include "Render/LineBatcher.h"
#include "GameFramework/Actor.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/BlendSpaceFile.h"
#include "Asset/AssetManager.h"
#include "Engine/World.h"
#include "Physics/PhysScene.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsMath.h"
#include "Physics/ConstraintInstance.h"
#include <PxPhysicsAPI.h>
#include <algorithm>
#include <vector>

void USkeletalMeshComponent::SetAnimInstance(UAnimInstance* InInstance)
{
	if (AnimInstance == InInstance)
	{
		return;
	}
	if (AnimInstance)
	{
		AnimInstance->UninitializeAnimation();
	}
	AnimInstance = InInstance;

	if (ManagedAnimGraph && ManagedAnimGraph.get() != InInstance)
	{
		ManagedAnimGraph.reset();
	}

	AnimBlueprintPath.clear();
	bBlueprintReloadRequested = false;

	if (AnimInstance)
	{
		AnimInstance->InitializeAnimation();
	}
}

void USkeletalMeshComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!AnimBlueprintPath.empty() && (bBlueprintReloadRequested || !ManagedAnimGraph))
	{
		ReloadAnimBlueprint();
	}

	if (AnimInstance)
	{
		AnimInstance->InitializeAnimation();

		if (auto* Instance = GetSingleNodeInstance())
		{
			if (Instance->GetCurrentAsset())
			{
				Instance->SetPlaying(true);
			}
		}
	}
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
	if (bBlueprintReloadRequested)
	{
		bBlueprintReloadRequested = false;
		ReloadAnimBlueprint();
	}

	if (AnimInstance && GetSkeletalMesh())
	{
		bUpdatingAnimation = true;
		if (auto* Instance = Cast<UAnimSingleNodeInstance>(AnimInstance))
		{
			Instance->SetBlendSpaceInput(BlendSpaceInput);
		}

		AnimInstance->UpdateAnimation(DeltaTime);
		FPoseContext Pose(AnimInstance, GetBones());
		AnimInstance->EvaluateAnimation(Pose);
		SetBoneSpaceTransforms(Pose.Pose);
		AnimInstance->TriggerAnimNotifies(DeltaTime);

		// 루트 모션: AnimInstance에 모인 delta를 월드 공간으로 바꿔 CharacterMovement가 가져갈 때까지 쌓아 둔다
		const FRootMotionMovementParams RootMotion = AnimInstance->ConsumeExtractedRootMotion();
		if (RootMotion.bHasRootMotion)
		{
			const FTransform WorldDelta = ConvertRootMotionToWorld(RootMotion.GetRootMotionTransform());
			PendingRootMotion.Location = (bHasPendingRootMotion ? PendingRootMotion.Location : FVector::ZeroVector) + WorldDelta.Location;
			PendingRootMotion.Rotation = bHasPendingRootMotion ? (WorldDelta.Rotation * PendingRootMotion.Rotation).Normalized() : WorldDelta.Rotation;
			bHasPendingRootMotion = true;
		}
		bUpdatingAnimation = false;
	}
	// PrePhysics: 시뮬레이션하지 않는 바디를 이번 애니메이션 포즈로 옮긴다.
	UpdateKinematicBonesToAnim();
	Super::TickComponent(DeltaTime);
}

const TArray<FBoneInfo>& USkeletalMeshComponent::GetBones() const
{
	static const TArray<FBoneInfo> Empty{};
	return GetSkeletalMesh() ? GetSkeletalMesh()->GetRefSkeleton() : Empty;
}

int32 USkeletalMeshComponent::FindBoneIndex(const FString& Name) const
{
	return GetSkeletalMesh() ? GetSkeletalMesh()->FindBoneIndex(Name) : -1;
}

FTransform USkeletalMeshComponent::GetBoneLocalTransform(int32 BoneIndex) const
{
	if (BoneIndex < 0 || BoneIndex >= BoneSpaceTransforms.Num())
		return FTransform();
	return BoneSpaceTransforms[BoneIndex];
}

void USkeletalMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const FTransform& Local)
{
	if (BoneIndex < 0 || BoneIndex >= BoneSpaceTransforms.Num())
		return;
	BoneSpaceTransforms[BoneIndex] = Local;
	MarkBoneTransformsDirty();
}

void USkeletalMeshComponent::SetBoneSpaceTransforms(const FCompactPose& Pose)
{
	if (Pose.GetNumBones() != GetBones().Num())
	{
		KRS_LOG(Warning, "SetBoneSpaceTransforms: pose has {} bones, mesh has {}.", Pose.GetNumBones(), GetBones().Num());
		return;
	}
#if defined(_DEBUG)
	for (int32 i = 0; i < Pose.GetNumBones(); ++i)
	{
		const FQuat& Q = Pose[i].Rotation;
		const float LenSq = Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W;
		if (std::abs(LenSq - 1.0f) > 1e-3f)
			KRS_LOG(Warning, "Pose bone {} rotation not normalized (|q|^2 = {})", i, LenSq);
	}
#endif
	BoneSpaceTransforms = Pose.GetBones();
	MarkBoneTransformsDirty();
}

FMatrix USkeletalMeshComponent::GetBoneComponentMatrix(int32 BoneIndex) const
{
	if (BoneIndex < 0 || BoneIndex >= ComponentSpaceTransforms.Num())
		return FMatrix::Identity;
	return ComponentSpaceTransforms[BoneIndex];
}

FMatrix USkeletalMeshComponent::GetBoneWorldMatrix(int32 BoneIndex) const
{
	return GetBoneComponentMatrix(BoneIndex) * GetWorldMatrix();
}

void USkeletalMeshComponent::ResetPose()
{
	OnSkeletalMeshChanged();
	MarkBoneTransformsDirty();
}

void USkeletalMeshComponent::RefreshBoneTransformsNow()
{
	UpdateBoneTransformsNow();
}

void USkeletalMeshComponent::RefreshBoneTransforms()
{
	const TArray<FBoneInfo>& Bones = GetBones();
	if (BoneSpaceTransforms.Num() != Bones.Num())
		OnSkeletalMeshChanged();

	ComponentSpaceTransforms.SetNum(Bones.Num());
	for (int i = 0; i < Bones.Num(); ++i)
	{
		const FMatrix Local = BoneSpaceTransforms[i].GetLocalMatrix();
		const int32 Parent = Bones[i].ParentIndex;
		ComponentSpaceTransforms[i] = Parent < 0 ? Local : Local * ComponentSpaceTransforms[Parent];
	}
	// 애니메이션 포즈(BoneSpaceTransforms)는 그대로 두고, 시뮬레이션 바디가 있는 본만 컴포넌트 공간에서 덮는다.
	BlendPhysicsBones();
}

void USkeletalMeshComponent::Serialize(FStructuredArchive::FRecord Record)
{
	FArchive& Ar = Record.GetUnderlyingArchive();
	UAnimInstance* SavedInstance = AnimInstance;

	if (Ar.IsSaving() && (!AnimBlueprintPath.empty() || ManagedAnimGraph))
	{
		AnimInstance = nullptr;
	}

	Super::Serialize(Record);

	if (Ar.IsSaving())
	{
		AnimInstance = SavedInstance;
	}

	int32 Num = Ar.IsSaving() ? BoneSpaceTransforms.Num() : 0;
	FStructuredArchive::FArray PoseArray = Record.EnterArray("BonePose", Num);

	for (int32 I = 0; I < Num; ++I)
	{
		FStructuredArchive::FRecord BoneRecord = PoseArray.EnterElement().EnterRecord();
		FString Name = Ar.IsSaving() ? GetBones()[I].Name : FString();
		FTransform Local = Ar.IsSaving() ? BoneSpaceTransforms[I] : FTransform();
		BoneRecord << SA_VALUE("Name", Name) << SA_VALUE("Local", Local);

		if (Ar.IsLoading())
		{
			// 메시의 본 순서가 바뀌어도 맞도록 이름으로 찾는다
			const int32 Index = FindBoneIndex(Name);
			if (Index >= 0)
				BoneSpaceTransforms[Index] = Local;
		}
	}

	SerializeSingleAnimation(Record);

	if (Ar.IsLoading())
	{
		if (!AnimBlueprintPath.empty())
		{
			ReloadAnimBlueprint();
		}

		RefreshBoneTransformsNow();
	}
}

void USkeletalMeshComponent::SerializeSingleAnimation(FStructuredArchive::FRecord& Record)
{
	// 디테일 패널에서 고른 단일 애니메이션(SingleNodeInstance의 에셋)은 AnimInstance 객체가 아니라
	// 에셋 경로로 저장한다. 없으면 필드를 쓰지 않으므로 예전 씬도 그대로 읽힌다.
	FArchive& Ar = Record.GetUnderlyingArchive();
	UAnimSingleNodeInstance* Instance = GetSingleNodeInstance();
	UAnimationAsset* Asset = Instance ? Instance->GetCurrentAsset() : nullptr;
	const bool bHasAnimation = Ar.IsSaving() && Asset && !Asset->GetAssetPath().empty();

	TOptional<FStructuredArchive::FSlot> AnimSlot = Record.TryEnterField("SingleAnimation", bHasAnimation);
	if (!AnimSlot.IsSet())
		return;

	FStructuredArchive::FRecord AnimRecord = AnimSlot.GetValue().EnterRecord();
	FString Path = Ar.IsSaving() ? Asset->GetAssetPath() : FString();
	bool bLoop = Ar.IsSaving() ? Instance->IsLooping() : true;
	float Rate = Ar.IsSaving() ? Instance->GetPlayRate() : 1.0f;
	AnimRecord << SA_VALUE("Asset", Path) << SA_VALUE("Looping", bLoop) << SA_VALUE("PlayRate", Rate);

	if (!Ar.IsLoading())
		return;

	// AssetManager 캐시로 불러온다 (디테일 패널이 고를 때와 같은 객체)
	UAnimationAsset* Loaded = BlendSpaceFile::IsBlendSpacePath(Path)
	                              ? static_cast<UAnimationAsset*>(UAssetManager::LoadBlendSpace(Path))
	                              : static_cast<UAnimationAsset*>(UAssetManager::LoadAnimSequence(Path));
	if (!Loaded)
	{
		KRS_LOG(Warning, "SkeletalMeshComponent: single animation not found: {}", Path);
		return;
	}

	// 재생 시작은 BeginPlay가 한다 (에디터 월드에서는 첫 프레임 포즈만 보인다)
	SetAnimation(Loaded);
	if (UAnimSingleNodeInstance* NewInstance = GetSingleNodeInstance())
	{
		NewInstance->SetLooping(bLoop);
		NewInstance->SetPlayRate(Rate);
	}
}

void USkeletalMeshComponent::OnSkeletalMeshChanged()
{
	bBlueprintReloadRequested = !AnimBlueprintPath.empty();
	BoneSpaceTransforms.Reset();
	for (const FBoneInfo& Bone : GetBones())
		BoneSpaceTransforms.Add(Bone.LocalBindPose);
}

void USkeletalMeshComponent::OnSkeletalMeshChanging()
{
	if (UWorld* World = GetWorld())
		World->UnregisterClothComponent(*this);
}

void USkeletalMeshComponent::OnSkeletalMeshReplaced()
{
	if (UWorld* World = GetWorld())
		World->RegisterClothComponent(*this);
}

UAnimSingleNodeInstance* USkeletalMeshComponent::GetSingleNodeInstance() const
{
	return Cast<UAnimSingleNodeInstance>(AnimInstance);
}

void USkeletalMeshComponent::SetAnimation(UAnimationAsset* NewAnimToPlay)
{
	if (NewAnimToPlay && !Cast<UAnimSequence>(NewAnimToPlay) && !Cast<UBlendSpace1D>(NewAnimToPlay))
	{
		KRS_LOG(Warning, "SetAnimation: unsupported animation asset type");
		return;
	}
	AnimBlueprintPath.clear();
	AnimBlueprintError.clear();
	bBlueprintReloadRequested = false;
	auto* SingleNodeInstance = Cast<UAnimSingleNodeInstance>(AnimInstance);

	if (!SingleNodeInstance)
	{
		SingleNodeInstance = NewObject<UAnimSingleNodeInstance>(this);
		SetAnimInstance(SingleNodeInstance);
	}

	SingleNodeInstance->SetAnimationAsset(NewAnimToPlay);
	SingleNodeInstance->SetBlendSpaceInput(BlendSpaceInput);

	RefreshAnimationPose();
}

void USkeletalMeshComponent::Play(bool bLooping)
{
	if (auto* Instance = GetSingleNodeInstance())
	{
		Instance->SetLooping(bLooping);
		Instance->SetPlaying(true);
	}
}

void USkeletalMeshComponent::Stop()
{
	if (auto* Instance = GetSingleNodeInstance())
	{
		Instance->SetPlaying(false);
	}
}

bool USkeletalMeshComponent::IsPlaying() const
{
	const auto* Instance = GetSingleNodeInstance();
	return Instance && Instance->IsPlaying();
}

void USkeletalMeshComponent::PlayAnimation(UAnimationAsset* NewAnimToPlay, bool bLooping)
{
	if (NewAnimToPlay && !Cast<UAnimSequence>(NewAnimToPlay) && !Cast<UBlendSpace1D>(NewAnimToPlay))
	{
		return;
	}

	SetAnimation(NewAnimToPlay);
	Play(bLooping);
}

void USkeletalMeshComponent::RefreshAnimationPose()
{
	if (!AnimInstance)
	{
		ResetPose();
		RefreshBoneTransformsNow();
		return;
	}

	if (auto* Instance = GetSingleNodeInstance())
	{
		Instance->SetBlendSpaceInput(BlendSpaceInput);
	}

	FPoseContext Pose(AnimInstance, GetBones());
	AnimInstance->EvaluateAnimation(Pose);
	SetBoneSpaceTransforms(Pose.Pose);
	RefreshBoneTransformsNow();
}

FTransform USkeletalMeshComponent::ConsumeRootMotion()
{
	const FTransform Result = PendingRootMotion;
	PendingRootMotion = FTransform();
	bHasPendingRootMotion = false;
	return Result;
}

FTransform USkeletalMeshComponent::ConvertRootMotionToWorld(const FTransform& ParentSpaceDelta) const
{
	// delta는 root 본의 부모 공간 값이다. UAL은 Armature → root 구조라 부모(Armature)를 거쳐야 컴포넌트 공간이 된다.
	const TArray<FBoneInfo>& Bones = GetBones();
	const int32 RootIndex = GetRootMotionBoneIndex();
	const int32 ParentIndex = RootIndex >= 0 ? Bones[RootIndex].ParentIndex : -1;

	// 이동: 부모(바인드 포즈) → 컴포넌트 → 월드 행렬로 방향·스케일만 적용 (Armature 스케일, 메시 스케일, yaw −180 모두 여기서)
	const FMatrix ComponentToWorld = GetWorldMatrix();
	const FMatrix ParentToWorld = ParentIndex >= 0
	                                  ? Bones[ParentIndex].InverseBindPose.Inverse() * ComponentToWorld
	                                  : ComponentToWorld;

	FTransform Result;
	Result.Location = ParentToWorld.TransformVector(ParentSpaceDelta.Location);

	// 회전: 부모 공간 회전을 월드 공간 회전으로 바꾼다 (P × Δ × P⁻¹, P = 컴포넌트 월드 회전 × 부모 본 회전)
	FQuat ParentRotation = FQuat(GetWorldRotation());
	FQuat BoneChainRotation;
	for (int32 Index = ParentIndex; Index >= 0; Index = Bones[Index].ParentIndex)
	{
		BoneChainRotation = Bones[Index].LocalBindPose.Rotation * BoneChainRotation;
	}
	ParentRotation = (ParentRotation * BoneChainRotation).Normalized();
	Result.Rotation = (ParentRotation * ParentSpaceDelta.Rotation * ParentRotation.Inverse()).Normalized();
	return Result;
}

int32 USkeletalMeshComponent::GetRootMotionBoneIndex() const
{
	const int32 RootIndex = FindBoneIndex("root");
	if (RootIndex >= 0)
	{
		return RootIndex;
	}
	return GetBones().IsEmpty() ? -1 : 0;
}

void USkeletalMeshComponent::DrawRootDebug(FLineBatcher* LineBatcher) const
{
	const int32 RootIndex = GetRootMotionBoneIndex();
	if (!LineBatcher || RootIndex < 0 || RootIndex >= ComponentSpaceTransforms.Num())
	{
		return;
	}

	// root 본 위치에 구체
	const FVector Root = GetBoneWorldMatrix(RootIndex).TransformPosition(FVector::ZeroVector);
	const FVector4 Color(1.0f, 0.3f, 0.8f, 1.0f);
	LineBatcher->AddSphere(Root, 8.0f, Color);

	// 액터 위치를 root 높이로 내린 점 → root. 선 길이 = root가 액터(캡슐)에서 벗어난 거리
	if (const AActor* Owner = GetOwner())
	{
		const FVector ActorLocation = Owner->GetActorLocation();
		const FVector Base(ActorLocation.X, ActorLocation.Y, Root.Z);
		LineBatcher->AddLine(Base, Root, Color);
		LineBatcher->AddSphere(Base, 3.0f, Color);
	}
}
bool GShowSkeletalRootDebug = false;

void DrawSkeletalRootDebug(UWorld* World, FLineBatcher* LineBatcher)
{
	if (!World || !LineBatcher)
		return;
	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
	{
		const USkeletalMeshComponent* Mesh = *It;
		if (!Mesh || Mesh->GetWorld() != World || !Mesh->IsRegistered())
			continue;
		Mesh->DrawRootDebug(LineBatcher);
	}
}

USkeletalMeshComponent::USkeletalMeshComponent()
{
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
	TermArticulated();
	if (ManagedAnimGraph)
	{
		if (AnimInstance == ManagedAnimGraph.get())
		{
			SetAnimInstance(nullptr);
		}
		else
		{
			ManagedAnimGraph->UninitializeAnimation();
		}
	}
}

void USkeletalMeshComponent::OnRegister()
{
	Super::OnRegister();

	if (UWorld* World = GetWorld())
		World->RegisterClothComponent(*this);
}

void USkeletalMeshComponent::OnUnregister()
{
	if (UWorld* World = GetWorld())
		World->UnregisterClothComponent(*this);

	Super::OnUnregister();
}

const FString& USkeletalMeshComponent::GetAnimBlueprintPath() const
{
	return AnimBlueprintPath;
}

const FString& USkeletalMeshComponent::GetAnimBlueprintError() const
{
	return AnimBlueprintError;
}

bool USkeletalMeshComponent::SetAnimBlueprint(const FString& Path)
{
	if (bUpdatingAnimation)
	{
		AnimBlueprintError = "Load or reload Animation Blueprints from actor Tick, outside animation callbacks.";
		return false;
	}

	if (Path.empty())
	{
		const bool bHadBlueprint = !AnimBlueprintPath.empty();
		AnimBlueprintPath.clear();
		AnimBlueprintError.clear();
		bBlueprintReloadRequested = false;

		if (ManagedAnimGraph && bHadBlueprint)
		{
			SetAnimInstance(nullptr);
			ResetPose();
			RefreshBoneTransformsNow();
		}

		return true;
	}

	AnimBlueprintError.clear();
	const FString Key = FAssetUtils::GetAssetKey(Path);
	const FString FilePath = FAssetUtils::ResolveAssetFilePath(Key);
	FAnimBlueprintData Data;

	if (!AnimBlueprintFile::IsAnimBlueprintPath(Key) || !AnimBlueprintFile::Read(FPaths::FromUtf8(FilePath.empty() ? Key : FilePath), Data, AnimBlueprintError))
	{
		if (AnimBlueprintError.empty())
		{
			AnimBlueprintError = "Use an .animblueprint asset.";
		}

		return false;
	}

	TUniquePtr<UAnimGraphInstance> Instance;
	AnimBlueprintCompiler::FNodeMap NodeIndices;

	if (!AnimBlueprintCompiler::Build(Data, *this, Instance, NodeIndices, AnimBlueprintError))
	{
		return false;
	}

	SetAnimInstance(Instance.get());
	ManagedAnimGraph = std::move(Instance);
	AnimBlueprintPath = Key;
	bBlueprintReloadRequested = false;
	AnimBlueprintError.clear();
	RefreshAnimationPose();
	return true;
}

bool USkeletalMeshComponent::ReloadAnimBlueprint()
{
	const FString Path = AnimBlueprintPath;
	bBlueprintReloadRequested = false;

	if (Path.empty())
	{
		return false;
	}

	if (!SetAnimBlueprint(Path))
	{
		KRS_LOG(Warning, "AnimBlueprint load failed: {} ({})", Path, AnimBlueprintError);
		return false;
	}

	return true;
}

UAnimGraphInstance* USkeletalMeshComponent::CreateAnimGraph()
{
	if (bUpdatingAnimation)
	{
		AnimBlueprintError = "Create animation graphs outside animation callbacks.";
		return nullptr;
	}

	if (!AnimBlueprintPath.empty())
	{
		AnimBlueprintError = "An Animation Blueprint is assigned. Use GetAnimGraph to update its variables.";
		return nullptr;
	}

	AnimBlueprintError.clear();

	if (UAnimGraphInstance* Existing = Cast<UAnimGraphInstance>(AnimInstance))
	{
		Existing->ResetGraph();
		return Existing;
	}

	TUniquePtr<UAnimGraphInstance> Instance(NewObject<UAnimGraphInstance>(this));
	SetAnimInstance(Instance.get());
	ManagedAnimGraph = std::move(Instance);
	return ManagedAnimGraph.get();
}

// ---------------------------------------------------------------------------
// 물리: PhysicsAsset 바디·관절 (UE SkeletalMeshComponentPhysics.cpp 대응)
// ---------------------------------------------------------------------------

namespace
{
	// 행 벡터 행렬(스케일 포함)의 강체 부분. 행 i = 로컬 i축의 부모 공간 방향 × 스케일.
	physx::PxTransform RigidFromMatrix(const FMatrix& M)
	{
		physx::PxVec3 X(M.M[0][0], M.M[0][1], M.M[0][2]);
		physx::PxVec3 Y(M.M[1][0], M.M[1][1], M.M[1][2]);
		physx::PxVec3 Z(M.M[2][0], M.M[2][1], M.M[2][2]);
		X.normalize();
		Y.normalize();
		Z.normalize();
		physx::PxQuat Q(physx::PxMat33(X, Y, Z));
		Q.normalize();
		return physx::PxTransform(physx::PxVec3(M.M[3][0], M.M[3][1], M.M[3][2]), Q);
	}

	FTransform TransformFromMatrix(const FMatrix& M)
	{
		const physx::PxTransform Rigid = RigidFromMatrix(M);
		const FVector Scale(FVector(M.M[0][0], M.M[0][1], M.M[0][2]).Size(), FVector(M.M[1][0], M.M[1][1], M.M[1][2]).Size(),
		                    FVector(M.M[2][0], M.M[2][1], M.M[2][2]).Size());
		return FTransform(PhysicsMath::FromPx(Rigid.q), PhysicsMath::FromPx(Rigid.p), Scale);
	}

	// 두 컴포넌트 공간 포즈를 Weight(0=A, 1=B)로 섞는다. 스케일은 A를 따른다.
	FMatrix BlendPose(const FMatrix& A, const FMatrix& B, float Weight)
	{
		const FTransform TA = TransformFromMatrix(A);
		const physx::PxTransform RA = RigidFromMatrix(A), RB = RigidFromMatrix(B);
		physx::PxQuat QB = RB.q;
		if (RA.q.dot(QB) < 0.0f)
			QB = -QB;
		physx::PxQuat Q = RA.q * (1.0f - Weight) + QB * Weight;
		Q.normalize();
		const physx::PxVec3 P = RA.p * (1.0f - Weight) + RB.p * Weight;
		return FTransform(PhysicsMath::FromPx(Q), PhysicsMath::FromPx(P), TA.Scale).GetLocalMatrix();
	}

	void ComputeComponentSpace(const TArray<FBoneInfo>& Bones, const TArray<FTransform>& Locals, std::vector<FMatrix>& Out)
	{
		Out.resize(Bones.Num());
		const bool bUseLocals = Locals.Num() == Bones.Num();
		for (int32 Index = 0; Index < Bones.Num(); ++Index)
		{
			const FMatrix Local = (bUseLocals ? Locals[Index] : Bones[Index].LocalBindPose).GetLocalMatrix();
			const int32 Parent = Bones[Index].ParentIndex;
			Out[Index] = Parent < 0 ? Local : Local * Out[Parent];
		}
	}
}

UPhysicsAsset* USkeletalMeshComponent::GetPhysicsAsset() const
{
	if (PhysicsAssetOverride)
		return PhysicsAssetOverride;
	return GetSkeletalMesh() ? GetSkeletalMesh()->GetPhysicsAsset() : nullptr;
}

void USkeletalMeshComponent::SetPhysicsAsset(UPhysicsAsset* InPhysicsAsset)
{
	if (PhysicsAssetOverride == InPhysicsAsset)
		return;
	PhysicsAssetOverride = InPhysicsAsset;
	if (IsRegistered())
		RecreatePhysicsState();
}

FBodyInstance* USkeletalMeshComponent::FindBodyInstance(const FName& BoneName)
{
	for (FBodyInstance* Body : Bodies)
	{
		const UBodySetup* Setup = Body->GetBodySetup();
		if (BoneName == NAME_None || (Setup && Setup->BoneName == BoneName))
			return Body;
	}
	return nullptr;
}

FConstraintInstance* USkeletalMeshComponent::FindConstraintInstance(const FName& JointName) const
{
	for (FConstraintInstance* Constraint : Constraints)
		if (Constraint->JointName == JointName)
			return Constraint;
	return nullptr;
}

bool USkeletalMeshComponent::IsBodyPairCollisionEnabled(int32 BodyIndexA, int32 BodyIndexB) const
{
	const UPhysicsAsset* Asset = GetPhysicsAsset();
	return !Asset || Asset->IsCollisionEnabled(BodyIndexA, BodyIndexB);
}

bool USkeletalMeshComponent::ShouldCreatePhysicsState() const
{
	const UWorld* World = GetWorld();
	if (GetCollisionEnabled() == ECollisionEnabled::NoCollision || !World || !World->IsGameWorld() || !World->HasBegunPlay() || !GetSkeletalMesh())
		return false;
	const UPhysicsAsset* Asset = GetPhysicsAsset();
	return Asset && !Asset->SkeletalBodySetups.IsEmpty();
}

bool USkeletalMeshComponent::OnCreatePhysicsState()
{
	FPhysScene* Scene = GetWorld() ? GetWorld()->GetPhysicsScene() : nullptr;
	// 다시 만들 때도 이전 바디·관절을 먼저 모두 정리한다.
	OnDestroyPhysicsState();
	if (!Scene || !InstantiatePhysicsAssetBodies_Internal(*Scene))
	{
		TermArticulated();
		KRS_LOG(Error, "[Physics] {}: failed to create physics asset bodies.", GetName());
		return false;
	}
	ComponentVelocity = FVector::ZeroVector;
	return true;
}

void USkeletalMeshComponent::OnDestroyPhysicsState()
{
	// 바디가 사라지면 UpdatePhysicsOverlaps가 이 컴포넌트를 찾지 못하므로 여기서 Overlap을 끝낸다.
	if (!Bodies.IsEmpty())
		while (!GetOverlapInfos().IsEmpty())
		{
			UPrimitiveComponent* Other = GetOverlapInfos()[GetOverlapInfos().Num() - 1].OtherComp;
			EndComponentOverlap(Other);
			if (Other)
				Other->EndComponentOverlap(this);
		}
	TermArticulated();
}

bool USkeletalMeshComponent::InstantiatePhysicsAssetBodies_Internal(FPhysScene& Scene)
{
	UPhysicsAsset* Asset = GetPhysicsAsset();
	const TArray<FBoneInfo>& Bones = GetBones();
	if (!Asset || Bones.IsEmpty())
		return false;
	// 충돌 표를 미리 만들어 둔다 (시뮬레이션 중 필터 콜백이 읽기만 하도록).
	Asset->IsCollisionEnabled(0, 0);

	std::vector<FMatrix> ComponentSpace;
	ComputeComponentSpace(Bones, GetBoneSpaceTransforms(), ComponentSpace);
	const FMatrix World = GetWorldMatrix();
	const FVector Scale = GetWorldScale3D();
	// 같은 컴포넌트 바디끼리의 충돌을 CollisionDisableTable로 다시 판정하기 위한 묶음 번호 (0이 아니어야 한다).
	const uint32 Group = (GetUUID() % 0x00FFFFFEu) + 1u;

	// 1. 모든 바디
	for (int32 BodyIndex = 0; BodyIndex < Asset->SkeletalBodySetups.Num(); ++BodyIndex)
	{
		USkeletalBodySetup* Setup = Asset->SkeletalBodySetups[BodyIndex];
		const int32 BoneIndex = Setup ? FindBoneIndex(Setup->BoneName.ToString()) : -1;
		if (BoneIndex < 0)
		{
			KRS_LOG(Warning, "[Physics] {}: physics asset body for missing bone {} skipped.", GetName(), Setup ? Setup->BoneName.ToString() : FString("?"));
			continue;
		}
		auto* Body = new FBodyInstance();
		// 질량·감쇠는 본 바디 정의, 충돌·시뮬레이션은 컴포넌트 설정을 따른다 (UE와 같다).
		Body->CopyBodyInstancePropertiesFrom(Setup->DefaultInstance);
		Body->CollisionEnabled = BodyInstance.CollisionEnabled;
		Body->ObjectType = BodyInstance.ObjectType;
		Body->CollisionResponses = BodyInstance.CollisionResponses;
		Body->bNotifyRigidBodyCollision = BodyInstance.bNotifyRigidBodyCollision;
		Body->bSimulatePhysics = BodyInstance.bSimulatePhysics;
		Body->bEnableGravity = BodyInstance.bEnableGravity;
		if (BodyInstance.PhysMaterialOverride)
			Body->PhysMaterialOverride = BodyInstance.PhysMaterialOverride;
		Body->InstanceBodyIndex = BodyIndex;
		Body->InstanceBoneIndex = BoneIndex;
		Body->SelfCollisionGroup = Group;
		// 바디 프레임 = 본 프레임. 형상은 본 공간에 정의되어 있고 컴포넌트 스케일로 커진다.
		const physx::PxTransform Pose = RigidFromMatrix(ComponentSpace[BoneIndex] * World);
		const FTransform BodyTransform(PhysicsMath::FromPx(Pose.q), PhysicsMath::FromPx(Pose.p), Scale);
		if (!Body->InitBody(Setup, BodyTransform, this, Scene))
		{
			delete Body;
			KRS_LOG(Error, "[Physics] {}: body for bone {} failed.", GetName(), Setup->BoneName.ToString());
			return false;
		}
		Bodies.Add(Body);
	}

	// 2. 모든 바디가 생긴 뒤 관절
	for (int32 ConstraintIndex = 0; ConstraintIndex < Asset->ConstraintSetup.Num(); ++ConstraintIndex)
	{
		const UPhysicsConstraintTemplate* Template = Asset->ConstraintSetup[ConstraintIndex];
		if (!Template)
			continue;
		FBodyInstance* Child = FindBodyInstance(Template->DefaultInstance.ConstraintBone1);
		FBodyInstance* Parent = FindBodyInstance(Template->DefaultInstance.ConstraintBone2);
		if (!Child || !Parent || Template->DefaultInstance.ConstraintBone1 == NAME_None || Template->DefaultInstance.ConstraintBone2 == NAME_None)
			continue;
		auto* Constraint = new FConstraintInstance();
		Constraint->CopyConstraintParamsFrom(Template->DefaultInstance);
		Constraint->ConstraintIndex = ConstraintIndex;
		if (!Constraint->InitConstraint(Child, Parent, Scale.X))
		{
			delete Constraint;
			KRS_LOG(Error, "[Physics] {}: constraint {} failed.", GetName(), Template->DefaultInstance.JointName.ToString());
			return false;
		}
		Constraints.Add(Constraint);
	}
	return !Bodies.IsEmpty();
}

void USkeletalMeshComponent::TermArticulated()
{
	for (FConstraintInstance* Constraint : Constraints)
	{
		Constraint->TermConstraint();
		delete Constraint;
	}
	Constraints.Empty();
	for (FBodyInstance* Body : Bodies)
	{
		Body->TermBody();
		delete Body;
	}
	Bodies.Empty();
}

void USkeletalMeshComponent::UpdateKinematicBonesToAnim()
{
	if (Bodies.IsEmpty())
		return;
	std::vector<FMatrix> ComponentSpace;
	ComputeComponentSpace(GetBones(), GetBoneSpaceTransforms(), ComponentSpace);
	const FMatrix World = GetWorldMatrix();
	for (FBodyInstance* Body : Bodies)
	{
		if (Body->IsInstanceSimulatingPhysics() || Body->InstanceBoneIndex < 0 || Body->InstanceBoneIndex >= static_cast<int32>(ComponentSpace.size()))
			continue;
		const physx::PxTransform Pose = RigidFromMatrix(ComponentSpace[Body->InstanceBoneIndex] * World);
		Body->SetBodyTransform(FTransform(PhysicsMath::FromPx(Pose.q), PhysicsMath::FromPx(Pose.p), Body->GetScale3D()));
	}
}

void USkeletalMeshComponent::SyncComponentToRBPhysics()
{
	// FPhysScene은 바디마다 부른다. 같은 결과 회수에서는 한 번만 본 포즈를 갱신한다.
	const UWorld* World = GetWorld();
	const FPhysScene* Scene = World ? World->GetPhysicsScene() : nullptr;
	if (!IsRegistered() || !Scene || Scene->GetSimulationCount() == LastPhysicsSyncCount)
		return;
	LastPhysicsSyncCount = Scene->GetSimulationCount();
	// 시뮬레이션 바디가 있을 때만 새 바디 포즈를 섞는다. 모두 키네마틱이면 이번 Tick의 애니메이션 포즈가
	// 이미 맞으므로 다시 계산하지 않는다 (시뮬레이션을 막 끈 바디도 다음 애니메이션 Tick이 포즈를 돌려 놓는다).
	// 스키닝은 게임 월드 Tick 끝에서 한 번만 한다 (UpdateBoneTransformsDeferred).
	if (!Bodies.IsEmpty() && IsAnyBodySimulating())
		UpdateBoneTransformsDeferred();
}

void USkeletalMeshComponent::BlendPhysicsBones()
{
	const TArray<FBoneInfo>& Bones = GetBones();
	if (Bodies.IsEmpty() || Bones.IsEmpty() || ComponentSpaceTransforms.Num() != Bones.Num() || BoneSpaceTransforms.Num() != Bones.Num())
		return;
	std::vector<FBodyInstance*> BodyOfBone(Bones.Num(), nullptr);
	bool bAnySimulating = false;
	for (FBodyInstance* Body : Bodies)
		if (Body->InstanceBoneIndex >= 0 && Body->InstanceBoneIndex < Bones.Num() && Body->IsInstanceSimulatingPhysics() && Body->PhysicsBlendWeight > 0.0f)
		{
			BodyOfBone[Body->InstanceBoneIndex] = Body;
			bAnySimulating = true;
		}
	if (!bAnySimulating)
		return;

	// 부모부터 차례로: 시뮬레이션 바디가 있는 본은 바디 포즈(× 가중치), 없는 본은 최종 부모 포즈 위의 애니메이션 로컬.
	// 바디 포즈는 매번 절대값으로 읽고 애니메이션 포즈는 건드리지 않으므로 같은 변환이 두 번 쌓이지 않는다.
	const FMatrix ToComponent = GetWorldMatrix().Inverse();
	const FVector Scale = GetWorldScale3D();
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		const int32 Parent = Bones[Index].ParentIndex;
		const FMatrix Local = BoneSpaceTransforms[Index].GetLocalMatrix();
		const FMatrix FromAnimation = Parent < 0 ? Local : Local * ComponentSpaceTransforms[Parent];
		const FBodyInstance* Body = BodyOfBone[Index];
		if (!Body)
		{
			ComponentSpaceTransforms[Index] = FromAnimation;
			continue;
		}
		const physx::PxTransform Pose = Body->GetActor()->getGlobalPose();
		const FMatrix PhysicsWorld = FTransform(PhysicsMath::FromPx(Pose.q), PhysicsMath::FromPx(Pose.p), Scale).GetLocalMatrix();
		const FMatrix FromPhysics = PhysicsWorld * ToComponent;
		ComponentSpaceTransforms[Index] = Body->PhysicsBlendWeight >= 1.0f ? FromPhysics : BlendPose(FromAnimation, FromPhysics, Body->PhysicsBlendWeight);
	}
}

bool USkeletalMeshComponent::IsBoneChildOf(int32 BoneIndex, int32 ParentBoneIndex) const
{
	const TArray<FBoneInfo>& Bones = GetBones();
	for (int32 Index = BoneIndex >= 0 && BoneIndex < Bones.Num() ? Bones[BoneIndex].ParentIndex : -1; Index >= 0; Index = Bones[Index].ParentIndex)
		if (Index == ParentBoneIndex)
			return true;
	return false;
}

void USkeletalMeshComponent::SetAllBodiesSimulatePhysics(bool bNewSimulate)
{
	MarkBoneTransformsDirty();
	BodyInstance.bSimulatePhysics = bNewSimulate;
	for (FBodyInstance* Body : Bodies)
		Body->SetInstanceSimulatePhysics(bNewSimulate);
}

void USkeletalMeshComponent::SetAllBodiesBelowSimulatePhysics(const FName& InBoneName, bool bNewSimulate, bool bIncludeSelf)
{
	MarkBoneTransformsDirty();
	const int32 Root = FindBoneIndex(InBoneName.ToString());
	if (Root < 0)
		return;
	for (FBodyInstance* Body : Bodies)
		if ((bIncludeSelf && Body->InstanceBoneIndex == Root) || IsBoneChildOf(Body->InstanceBoneIndex, Root))
			Body->SetInstanceSimulatePhysics(bNewSimulate);
}

void USkeletalMeshComponent::SetAllBodiesPhysicsBlendWeight(float PhysicsBlendWeight)
{
	MarkBoneTransformsDirty();
	for (FBodyInstance* Body : Bodies)
		Body->PhysicsBlendWeight = std::clamp(PhysicsBlendWeight, 0.0f, 1.0f);
}

void USkeletalMeshComponent::SetAllBodiesBelowPhysicsBlendWeight(const FName& InBoneName, float PhysicsBlendWeight, bool bIncludeSelf)
{
	MarkBoneTransformsDirty();
	const int32 Root = FindBoneIndex(InBoneName.ToString());
	if (Root < 0)
		return;
	for (FBodyInstance* Body : Bodies)
		if ((bIncludeSelf && Body->InstanceBoneIndex == Root) || IsBoneChildOf(Body->InstanceBoneIndex, Root))
			Body->PhysicsBlendWeight = std::clamp(PhysicsBlendWeight, 0.0f, 1.0f);
}

bool USkeletalMeshComponent::IsAnyBodySimulating() const
{
	for (const FBodyInstance* Body : Bodies)
		if (Body->IsInstanceSimulatingPhysics())
			return true;
	return false;
}

void USkeletalMeshComponent::AddImpulseToBone(const FVector& Impulse, const FName& BoneName, bool bVelChange)
{
	for (FBodyInstance* Body : Bodies)
		if (BoneName == NAME_None || (Body->GetBodySetup() && Body->GetBodySetup()->BoneName == BoneName))
			Body->AddImpulse(Impulse, bVelChange);
}

bool USkeletalMeshComponent::BuildClothPose(
    const FClothingLODData& ClothLOD,
    TArray<FVector>& OutPositions) const
{
	const auto& Bones = GetBones();
	const int32 Count = ClothLOD.RestPositions.Num();

	if (Bones.IsEmpty() || ComponentSpaceTransforms.Num() != Bones.Num() ||
	    Count != ClothLOD.SkinWeights.Num())
	{
		return false;
	}

	TArray<FMatrix> RefToLocal;
	RefToLocal.SetNum(Bones.Num());

	for (int32 BoneIndex = 0; BoneIndex < Bones.Num(); ++BoneIndex)
	{
		RefToLocal[BoneIndex] = Bones[BoneIndex].InverseBindPose *
		                        ComponentSpaceTransforms[BoneIndex];
	}

	OutPositions.SetNum(Count);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector& RestPosition = ClothLOD.RestPositions[Index];
		const FSkinWeight& Skin = ClothLOD.SkinWeights[Index];
		FVector Position = FVector::ZeroVector;

		for (int32 Influence = 0; Influence < MaxBoneInfluences; ++Influence)
		{
			const float Weight = Skin.Weight[Influence];
			if (Weight <= 0.0f)
				continue;

			const int32 BoneIndex = Skin.BoneIndex[Influence];
			if (!RefToLocal.IsValidIndex(BoneIndex))
				return false;

			Position += RefToLocal[BoneIndex].TransformPosition(RestPosition) * Weight;
		}

		OutPositions[Index] = Position;
	}

	return true;
}
