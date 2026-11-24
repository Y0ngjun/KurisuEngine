#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "Components/MovementComponent.h"
#include "Components/TextRenderComponent.h"
#include "Math/Transform.h"
#include "Render/Renderer.h"
#include "Math/Frustum.h"
#include "Math/BVH.h"
#include "Engine/Scene.h"
#include "Camera/CameraActor.h"
#include "Engine/EngineTypes.h"
#include "Render/SceneRenderData.h"
#include "Script/CoroutineScheduler.h"
#include "Physics/Cloth/ClothCollision.h"
#include "Physics/Cloth/ClothingAsset.h"

#include <functional>
#include "World.generated.h"

class ULevel;
class FLineBatcher;
class UBillboardComponent;
class APawn;
class APlayerController;
class FPhysScene;
class FClothScene;
class FPhysicsSystem;
class USkinnedMeshComponent;
class USkeletalMeshComponent;
class ASkeletalMeshActor;

struct FViewContext;

UCLASS()
class UWorld : public UObject
{
	GENERATED_BODY()

public:
	UWorld();
	virtual ~UWorld();

	// PhysicsSystem이 없으면 엔진(GEngine)의 물리 SDK를 쓴다. 엔진 없이 월드를 만드는 도구·검사에서 SDK를 넘긴다.
	bool Init(FPhysicsSystem* PhysicsSystem = nullptr);

	// 이 월드의 디버그 선(충돌 도형, 라이트 범위, 본 등)을 모아 그리는 도구. Init에서 만든다.
	// UE의 UWorld::LineBatcher와 같은 자리이고 UE에서도 public 멤버다.
	// UE는 선을 쌓아 두면 씬과 함께 그려지지만, 이 엔진은 쓰는 쪽이 BeginFrame, Add..., OnRender 순서로 직접 그린다.
	TUniquePtr<FLineBatcher> LineBatcher;
	/*UPrimitiveComponent* SpawnPrimitive(FClass* Class);*/
	AActor* DuplicateActor(AActor* Source);
	AActor* SpawnActor(UClass* Class, FName InName = NAME_None, const FTransform* Transform = nullptr);

	static bool AreAABBsOverlapping(const FBox& A, const FBox& B)
	{
		return A.Min.X <= B.Max.X && A.Max.X >= B.Min.X &&
		       A.Min.Y <= B.Max.Y && A.Max.Y >= B.Min.Y &&
		       A.Min.Z <= B.Max.Z && A.Max.Z >= B.Min.Z;
	}

	template <class T>
	T* SpawnActor(FName InName = NAME_None, const FTransform* Transform = nullptr)
	{
		return CastChecked<T>(SpawnActor(T::StaticClass(), InName, Transform));
	}

	void Tick(float RealDeltaTime);

	// 모든 액터 Tick과 물리가 끝난 뒤 1회. 물리 바디(도형 포함)의 Overlap 이벤트를 만든다.
	// UE처럼 시뮬레이션이 아니라 Scene Query로 현재 겹침을 구해 지난 프레임과 비교하고 End → Begin 순으로 보낸다.
	// 서로 Overlap 응답이고 양쪽 다 bGenerateOverlapEvents·Query 충돌이 켜진, 다른 액터의 쌍만 보고한다.
	void UpdatePhysicsOverlaps();

	// ---- PhysX Scene Query (UE WorldCollision.h). 위치·거리는 월드 cm ----
	// 채널에 Block 응답인 가장 가까운 바디를 찾는다.
	bool LineTraceSingleByChannel(FHitResult& OutHit, const FVector& Start, const FVector& End, ECollisionChannel TraceChannel,
	                              const FCollisionQueryParams& Params = FCollisionQueryParams()) const;
	// Overlap 응답인 바디를 거리순으로 모으고, 첫 Block이 있으면 마지막에 넣는다. Block이 있으면 true.
	bool LineTraceMultiByChannel(TArray<FHitResult>& OutHits, const FVector& Start, const FVector& End, ECollisionChannel TraceChannel,
	                             const FCollisionQueryParams& Params = FCollisionQueryParams()) const;
	bool SweepSingleByChannel(FHitResult& OutHit, const FVector& Start, const FVector& End, const FQuat& Rot, ECollisionChannel TraceChannel,
	                          const FCollisionShape& Shape, const FCollisionQueryParams& Params = FCollisionQueryParams()) const;
	bool SweepMultiByChannel(TArray<FHitResult>& OutHits, const FVector& Start, const FVector& End, const FQuat& Rot, ECollisionChannel TraceChannel,
	                         const FCollisionShape& Shape, const FCollisionQueryParams& Params = FCollisionQueryParams()) const;
	// Block 또는 Overlap 응답인 바디를 컴포넌트당 하나씩 모은다. Block 응답이 하나라도 있으면 true.
	bool OverlapMultiByChannel(TArray<FOverlapResult>& OutOverlaps, const FVector& Pos, const FQuat& Rot, ECollisionChannel TraceChannel,
	                           const FCollisionShape& Shape, const FCollisionQueryParams& Params = FCollisionQueryParams()) const;

	void ClearWorld();

	void CreateMainCamera();

	// 카메라 Get/Set
	void SetMainCamera(ACameraActor* Camera);
	ACameraActor* GetMainCamera() const { return MainCamera; }

	// Level
	ULevel* GetPersistentLevel() const { return PersistentLevel; }
	const TArray<ULevel*>& GetLevels() const { return Levels; }
	void SetPersistentLevel(ULevel* InLevel) { PersistentLevel = InLevel; }

	ULevel* GetCurrentLevel() const { return CurrentLevel; }
	void SetCurrentLevel(ULevel* InLevel) { CurrentLevel = InLevel; }

	int32 GetActorNum();

	// Tick 도중이면 정리(EndPlay, 컴포넌트/틱 해제)만 즉시 하고 메모리 해제는 Tick 끝으로 미룬다.
	bool DestroyActor(AActor* Actor);
	bool IsPendingDestroy(const AActor* Actor) const;

	using FOnWorldActorDestroyed = std::function<void(UWorld*, AActor*)>;
	static void SetOnActorDestroyedCallback(FOnWorldActorDestroyed InCallback);

	// View별 Billboard 행렬 공급자는 이 동기 호출 동안만 사용하며 저장하지 않는다.
	using FBillboardTraceTransform = FMatrix (*)(const UBillboardComponent&, const void*);
	// 현재 World의 Component에 Ray를 전달하고 가장 가까운 유효 교차를 반환한다.
	// 렌더 메시·빌보드 대상 CPU 피킹이다 (에디터 선택용). 물리 바디 쿼리는 LineTraceSingleByChannel을 쓴다.
	bool LineTraceSingle(const FRay& WorldRay, FHitResult& OutHit,
	                     FBillboardTraceTransform ResolveBillboard = nullptr, const void* ViewContext = nullptr);
	// bBlockingOnly: bBlockComponent인 도형/메시만 검사한다. 기본값은 기존 투사체 동작을 유지한다.
	// 렌더 메시 AABB·도형 대상 CPU 판정이다 (이동·카메라·투사체가 사용). 물리 바디 쿼리는 SweepSingleByChannel을 쓴다.
	bool SweepSingle(const FRay& WorldRay, float MaxDistance, float Radius, FHitResult& OutHit, AActor* IgnoreActor = nullptr,
	                 bool bBlockingOnly = false);

	// 파티클 충돌용. 에미터마다 한 번 Bounds와 겹치는 메시 프록시를 모아 두고,
	// 파티클마다는 그 후보에만 SweepAgainstCandidates로 검사한다 (BVH 순회·트랜스폼 갱신을 파티클 수만큼 하지 않는다)
	void GatherSweepCandidates(const FBox& Bounds, const AActor* IgnoreActor, TArray<FPrimitiveSceneProxy*>& OutCandidates);
	// SweepSingle의 메시 경로와 같은 판정(반지름만큼 키운 AABB). 시작점이 이미 안이면 bStartPenetrating
	static bool SweepAgainstCandidates(const TArray<FPrimitiveSceneProxy*>& Candidates, const FRay& WorldRay,
	                                   float MaxDistance, float Radius, FHitResult& OutHit);

	void BeginPlay();
	void EndPlay();
	bool HasBegunPlay() const { return bBegunPlay; }
	APawn* GetPlayerPawn() const { return PlayerPawn; }
	APlayerController* GetPlayerController() const { return PlayerController; }
	bool IsPaused() const { return bIsPaused; }
	void SetPaused(bool bPaused) { bIsPaused = bPaused; }

	FScene& GetScene() { return Scene; }
	// 이 월드가 소유한 물리 씬. Editor/Preview에도 존재하지만 Game/PIE만 시뮬레이션한다.
	FPhysScene* GetPhysicsScene() const { return PhysicsScene.get(); }
	FTickTaskManager& GetTickTaskManager() { return TickTaskManager; }
	FCoroutineScheduler& GetCoroutineScheduler() { return CoroutineScheduler; }

	void SetWorldType(EWorldType InType) { WorldType = InType; }
	EWorldType GetWorldType() const { return WorldType; }

	static UWorld* CreateWorld(const EWorldType InWorldType, bool bInformEngineOfWorld, FName WorldName = NAME_None,
	                           FPhysicsSystem* PhysicsSystem = nullptr); /*, UPackage* InWorldPackage = NULL, bool bAddToRoot = true, ERHIFeatureLevel::Type InFeatureLevel = ERHIFeatureLevel::Num, const InitializationValues* InIVS = nullptr, bool bInSkipInitWorld = false);*/

	bool IsPlayInEditor() const { return WorldType == EWorldType::PIE; }
	bool IsGameWorld() const { return WorldType == EWorldType::Game || WorldType == EWorldType::PIE; }

	static UWorld* GetDuplicatedWorldForPIE(UWorld* InWorld);

	virtual void PostDuplicate(bool bDuplicateForPIE) override;

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	// Slomo-hitstop
	float GetEffectiveTimeDilation() const;
	void StartHitStop(float Duration, float Dilation);
	// UE AWorldSettings::TimeDilation. 게임 시간(액터 Tick·물리) 배율. 1이면 실제 속도.
	void SetGlobalTimeDilation(float Dilation) { GlobalTimeDilation = Dilation < MinTimeDilation ? MinTimeDilation : (Dilation > MaxTimeDilation ? MaxTimeDilation : Dilation); }
	float GetGlobalTimeDilation() const { return GlobalTimeDilation; }

	// 렌더러가 이 월드를 그릴 때 뷰마다 카메라 위치를 남긴다.
	// Tick은 렌더보다 먼저 돌므로, Tick 안에서 읽으면 직전 프레임에 그린 카메라들이다
	void AddRenderedViewLocation(const FVector& ViewLocation);
	const TArray<FVector>& GetViewLocationsRenderedLastFrame() const { return ViewLocationsRenderedLastFrame; }

	// 게임 월드 Tick 동안 바뀐 스키닝 결과는 Tick 끝에서 컴포넌트당 한 번만 렌더로 보낸다.
	// 애니메이션(PrePhysics)과 물리 결과 반영(물리 직후)이 한 프레임에 두 번 스키닝하지 않게 한다.
	bool CanDeferSkinnedMeshUpdates() const { return bDeferSkinnedMeshUpdates; }
	void QueueSkinnedMeshRenderUpdate(USkinnedMeshComponent* Component);
	void DequeueSkinnedMeshRenderUpdate(USkinnedMeshComponent* Component);

	void RegisterClothComponent(USkeletalMeshComponent& Component);
	void UnregisterClothComponent(USkeletalMeshComponent& Component);

private:
	// UPROPERTY가 아니므로 PIE 복제 시 원본 씬 포인터를 복사하지 않는다.
	// 다른 월드 멤버보다 나중에 파괴되도록 먼저 선언한다.
	TUniquePtr<FPhysScene> PhysicsScene;
	TUniquePtr<FClothScene> ClothScene;

	struct FRegisteredClothSection
	{
		int32 SectionIndex = INDEX_NONE;
		uint64 InstanceId = 0;

		FClothConfig Config;
		FClothCollisionBinding CollisionBinding;
		FClothCollisionFrame CollisionFrame;

		bool bNeedsReset = true;
		bool bInputsReady = false;
	};

	struct FRegisteredClothComponent
	{
		USkeletalMeshComponent* Component = nullptr;
		TArray<FRegisteredClothSection> Sections;
	};

	void GameWorldTick(float DeltaTime);
	void EditorWorldTick(float DeltaTime);
	void PreviewWorldTick(float DeltaTime);

	void TickCloth(float DeltaTime);

	// 월드가 들고 있는 플레이어/카메라 포인터가 사라질 액터를 가리키지 않게 끊는다.
	void ReleaseWorldReferences(AActor* Actor);
	// Tick 중 Destroy된 액터들의 메모리를 실제로 해제한다.
	void FlushPendingDestroyActors();
	// GameWorldTick이 끝난 뒤 쌓인 스키닝 갱신을 한 번에 보낸다.
	void FlushSkinnedMeshRenderUpdates();

	EWorldType WorldType = EWorldType::None;

	// 등록된 Tick 함수만 실행한다. Actor보다 먼저 사라져도 남은 함수와의 연결을 스스로 끊는다.
	FTickTaskManager TickTaskManager;
	FCoroutineScheduler CoroutineScheduler;

	TQueue<AActor*> BeginPlayList;

	// Tick 도중 Destroy된 액터. 콜백/이터레이터가 해제된 메모리를 밟지 않도록 Tick 끝에서 delete한다.
	TArray<AActor*> PendingDestroyActors;
	bool bIsTicking = false;

	TArray<USkinnedMeshComponent*> PendingSkinnedMeshUpdates;
	bool bDeferSkinnedMeshUpdates = false;

	// 메인 카메라
	ACameraActor* MainCamera = nullptr;
	APawn* PlayerPawn = nullptr;
	APlayerController* PlayerController = nullptr;
	bool bIsPaused = false;
	bool bBegunPlay = false;

	ULevel* PersistentLevel = nullptr;
	ULevel* CurrentLevel = nullptr;
	TArray<ULevel*> Levels;

	FScene Scene;

	// Slomo-hitstop
	float GlobalTimeDilation = 1.0f;
	float HitStopTimeRemaining = 0.0f; // 실제 시간 (초)
	float HitStopDilation = 1.0f;
	float TimeSeconds = 0.0f;     // 게임 시간 (dilated 누적)
	float RealTimeSeconds = 0.0f; // 실제 시간 누적

	// Tick이 돌 때마다 증가. 기록이 어느 Tick 뒤에 그려진 것인지 구분한다
	uint32 TickCount = 0;
	uint32 ViewLocationsTickCount = 0;
	TArray<FVector> ViewLocationsRenderedLastFrame;
	// Tick 없이 렌더만 계속되는 월드(PIE 중 에디터 월드 등)에서 목록이 끝없이 늘지 않게 막는다
	static constexpr int32 MaxRenderedViewLocations = 16;

	static constexpr float MinTimeDilation = 0.0001f;
	static constexpr float MaxTimeDilation = 20.0f;

	void InitializeClothScene();
	ASkeletalMeshActor* CapeTestActor = nullptr;
	bool bResetCapeCloth = true;

	TArray<FRegisteredClothComponent> RegisteredClothComponents;
	uint64 NextClothInstanceId = 1;
};
