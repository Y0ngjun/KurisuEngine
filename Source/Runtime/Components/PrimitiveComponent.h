#pragma once

#include "SceneComponent.h"
#include "../Render/Shader.h"
#include "../Render/Mesh.h"
#include "Render/RenderPacket.h"
#include "Render/GeometryGenerator.h"
#include "Collision/HitResult.h"
#include "Core/Delegate/Delegate.h"
#include "Physics/BodyInstance.h"
#include "PrimitiveComponent.generated.h"

class FPrimitiveSceneProxy;
class AActor;
class UPrimitiveComponent;

// (Self, Other, OtherComp). 담당: C
DECLARE_DELEGATE(FComponentOverlapSignature, UPrimitiveComponent*, AActor*, UPrimitiveComponent*);
// UE: (HitComponent, OtherActor, OtherComp, NormalImpulse, Hit). Hit.HitComponent는 상대 컴포넌트다.
DECLARE_DELEGATE(FComponentHitSignature, UPrimitiveComponent*, AActor*, UPrimitiveComponent*, FVector, const FHitResult&);
// UE: (WakingComponent, BoneName)
DECLARE_DELEGATE(FComponentSleepSignature, UPrimitiveComponent*, FName);

struct FOverlapInfo
{
	UPrimitiveComponent* OtherComp = nullptr;
};

enum class EPrimitiveType
{
	Sphere,
	Cube,
	Cone,
	Plane
};

UCLASS()
class UPrimitiveComponent : public USceneComponent
{
	GENERATED_BODY()

	// Material은 여기서 등록하지 않는다. 컴포넌트마다 머티리얼을 다루는 방식이 달라서
	// (빌보드는 단일 머티리얼, 스태틱 메시는 슬롯별 덮어쓰기) 각자 등록한다.
public:
	UPrimitiveComponent();
	virtual ~UPrimitiveComponent();

	virtual void BeginPlay() override;
	// 물리 결과 회수 후 호출. 물리 바디가 없는 Primitive는 처리하지 않는다.
	virtual void SyncComponentToRBPhysics();
	virtual UBodySetup* GetBodySetup() const { return nullptr; }
	const FBodyInstance& GetBodyInstance() const { return BodyInstance; }
	FBodyInstance& GetBodyInstance() { return BodyInstance; }
	// UE GetBodyInstance(BoneName). 단일 바디는 본 이름을 무시하고 실행 중 바디를, 스켈레탈은 그 본의 바디를 준다.
	virtual FBodyInstance* FindBodyInstance(const FName& BoneName);
	// 같은 컴포넌트의 두 바디가 서로 충돌하는지 (스켈레탈: PhysicsAsset CollisionDisableTable). 물리 필터가 부른다.
	virtual bool IsBodyPairCollisionEnabled(int32 BodyIndexA, int32 BodyIndexB) const { return true; }
	void SetPhysicsEnabled(bool bEnabled);
	// 켜져 있으면 충돌 설정(CollisionEnabled)만 메시 에셋의 BodySetup을 따른다 (UE: bUseDefaultCollision).
	bool UsesDefaultCollision() const { return bUseDefaultCollision; }
	void SetUseDefaultCollision(bool bUseDefault);
	void ApplyDefaultCollision();
	bool IsPhysicsEnabled() const { return GetCollisionEnabled() == ECollisionEnabled::PhysicsOnly || GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics; }
	bool IsQueryCollisionEnabled() const { return GetCollisionEnabled() == ECollisionEnabled::QueryOnly || GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics; }
	void SetPhysMaterialOverride(UPhysicalMaterial* Material);
	ECollisionEnabled GetCollisionEnabled() const;
	void SetCollisionEnabled(ECollisionEnabled Value);
	// UE Collision Presets. "BlockAll", "OverlapAllDynamic", "Pawn", "PhysicsActor", "Ragdoll", "Trigger" 등 (UCollisionProfile).
	// 활성화·Object Type·채널 응답을 한 번에 정한다. 그 셋 중 하나를 직접 바꾸면 "Custom"이 된다.
	void SetCollisionProfileName(const FName& ProfileName);
	const FName& GetCollisionProfileName() const;
	// 채널 설정. bUseDefaultCollision이 켜져 있으면 메시 에셋 BodySetup의 값을 따른다.
	// Set 계열은 UE처럼 직접 정한 값으로 전환한다 (bUseDefaultCollision = false).
	ECollisionChannel GetCollisionObjectType() const;
	ECollisionResponse GetCollisionResponseToChannel(ECollisionChannel Channel) const;
	void SetCollisionObjectType(ECollisionChannel Channel);
	void SetCollisionResponseToChannel(ECollisionChannel Channel, ECollisionResponse Response);
	void SetCollisionResponseToAllChannels(ECollisionResponse Response);
	void SetNotifyRigidBodyCollision(bool bNewNotify);
	virtual void SetMobility(EComponentMobility Value) override;
	void SetSimulatePhysics(bool bSimulate);
	bool IsSimulatingPhysics() const { return BodyInstance.IsInstanceSimulatingPhysics(); }
	void SetEnableGravity(bool bEnabled) { BodyInstance.SetEnableGravity(bEnabled); }
	bool IsGravityEnabled() const { return BodyInstance.bEnableGravity; }
	void SetLinearDamping(float Value) { BodyInstance.SetLinearDamping(Value); }
	void SetAngularDamping(float Value) { BodyInstance.SetAngularDamping(Value); }
	void SetMassOverrideInKg(float Value, bool bOverride = true) { BodyInstance.SetMassOverride(Value, bOverride); }
	float GetMass() const { return BodyInstance.GetBodyMass(); }
	void AddForce(const FVector& Force, bool bAccelChange = false) { BodyInstance.AddForce(Force, bAccelChange); }
	void AddImpulse(const FVector& Impulse, bool bVelChange = false) { BodyInstance.AddImpulse(Impulse, bVelChange); }
	void AddTorqueInRadians(const FVector& Torque, bool bAccelChange = false) { BodyInstance.AddTorqueInRadians(Torque, bAccelChange); }
	void AddAngularImpulseInRadians(const FVector& Impulse, bool bVelChange = false) { BodyInstance.AddAngularImpulseInRadians(Impulse, bVelChange); }
	void SetPhysicsLinearVelocity(const FVector& Value, bool bAddToCurrent = false) { BodyInstance.SetLinearVelocity(Value, bAddToCurrent); }
	void SetPhysicsAngularVelocityInRadians(const FVector& Value, bool bAddToCurrent = false) { BodyInstance.SetAngularVelocityInRadians(Value, bAddToCurrent); }
	FVector GetPhysicsLinearVelocity() const { return BodyInstance.GetLinearVelocity(); }
	FVector GetPhysicsAngularVelocityInRadians() const { return BodyInstance.GetAngularVelocityInRadians(); }
	void WakeRigidBody() { BodyInstance.WakeInstance(); }
	void PutRigidBodyToSleep() { BodyInstance.PutInstanceToSleep(); }
	bool IsAnyRigidBodyAwake() const { return BodyInstance.IsInstanceAwake(); }
	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	// Todo: subuv
	// void SubmitToRenderQueue(TQueue<FRenderPacket>& RenderQueue);
	virtual void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext);

	virtual const FStaticMeshData* GetMeshData() const { return nullptr; }

	// Outline처럼 컴포넌트를 통째로 한 번 더 그릴 때 쓰는 GPU 메시.
	// 메시가 없거나(텍스트) View에 따라 형상이 정해지는 컴포넌트(빌보드·파티클)는
	// 컴포넌트 월드 행렬만으로 같은 그림을 못 만들므로 nullptr을 돌려준다.
	virtual UStaticMesh* GetRenderMesh() const { return nullptr; }

	virtual int32 GetNumMaterials() const { return 0; }
	virtual UMaterial* GetMaterial(int32 SlotIndex) const { return nullptr; }
	virtual void SetMaterial(int32 SlotIndex, UMaterial* InMaterial) {}
	//
	// FShader* GetShader() const { return Shader.get(); };

	void SetVisible(bool bInVisible) { SetVisibility(bInVisible); }

	virtual bool LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit);
	virtual bool LineTraceComponentLocal(const FRay& LocalRay, float& OutT);
	virtual FBox CalcLocalBounds() const override
	{
		const FStaticMeshData* Data = GetMeshData();
		return Data ? Data->AABB : Super::CalcLocalBounds();
	}

	virtual FPrimitiveSceneProxy* CreateSceneProxy();
	FPrimitiveSceneProxy* GetSceneProxy() const { return SceneProxy; }

	void MarkRenderStateDirty() override;

	virtual void OnTransformDirty() override;

	virtual bool MoveComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit = nullptr) override;

	virtual void PostEditChangeProperty(const FProperty& Property) override;

	void SetCastShadow(bool NewValue);
	void SetReceiveShadow(bool NewValue);

	UPROPERTY(EditAnywhere)
	bool bCastShadow = true;
	UPROPERTY(EditAnywhere)
	bool bReceiveShadow = true;
	UPROPERTY(EditAnywhere)
	bool bReceivesDecals = true;

	// ---- 충돌 이벤트 (담당: C) ----
	FComponentOverlapSignature OnComponentBeginOverlap;
	FComponentOverlapSignature OnComponentEndOverlap;
	FComponentHitSignature OnComponentHit;
	// 물리 바디가 깨어나거나 잠들 때 (PhysX onWake/onSleep)
	FComponentSleepSignature OnComponentWake;
	FComponentSleepSignature OnComponentSleep;

	UPROPERTY(EditAnywhere)
	bool bGenerateOverlapEvents = true;
	// CPU 이동 Sweep(UWorld::SweepSingle의 bBlockingOnly: 스프링 암 등)에서 막는 대상인지. UE에는 없는 엔진 고유 플래그다.
	// PhysX 충돌·이벤트·쿼리는 이 값이 아니라 BodyInstance의 ObjectType/CollisionResponses를 따른다.
	UPROPERTY(EditAnywhere)
	bool bBlockComponent = false;

	bool IsOverlappingComponent(const UPrimitiveComponent* Other) const;
	const TArray<FOverlapInfo>& GetOverlapInfos() const { return OverlapInfos; }

	// UWorld::UpdatePhysicsOverlaps가 겹침 상태가 바뀐 프레임에 호출한다
	void BeginComponentOverlap(UPrimitiveComponent* OtherComp);
	void EndComponentOverlap(UPrimitiveComponent* OtherComp);
	// 도형 경로는 접촉 정보 없이, 물리 경로(FPhysScene)는 접촉점·법선·충격량과 함께 호출한다.
	void DispatchHit(UPrimitiveComponent* OtherComp);
	void DispatchHit(UPrimitiveComponent* OtherComp, const FVector& NormalImpulse, const FHitResult& Hit);

protected:
	virtual bool ShouldCreatePhysicsState() const override;
	virtual bool OnCreatePhysicsState() override;
	virtual void OnDestroyPhysicsState() override;
	UPROPERTY(EditAnywhere)
	bool bUseDefaultCollision = false;
	UPROPERTY(EditAnywhere)
	FBodyInstance BodyInstance;
	virtual void OnUnregister() override;
	TArray<FOverlapInfo> OverlapInfos;

	virtual bool ShouldCreateRenderState() const override
	{
		return true;
	}

	virtual void CreateRenderState() override;
	virtual void DestroyRenderState() override;

	bool TraceMesh(const FRay& WorldRay, const FStaticMeshData& Mesh, const FMatrix& WorldMatrix, FHitResult& OutResult);
	bool TraceMeshLocal(const FRay& LocalRay, const FStaticMeshData& Mesh, float& OutT);

	// 바디를 둘 월드 Transform. 기본은 컴포넌트 월드 Transform(스케일 포함)이고 스케일은 형상 크기에 적용된다.
	virtual FTransform GetPhysicsBodyTransform() const;
	// 형상 크기에 월드 스케일을 미리 굽는 컴포넌트(도형). 스케일이 바뀌면 바디를 다시 만든다.
	virtual bool BakesScaleIntoBodySetup() const { return false; }

private:
	bool IsSupportedPhysicsRoot() const;
	// 루트가 아니어도 시뮬레이션하지 않는 바디(Static/Kinematic)는 컴포넌트를 따라가므로 만들 수 있다.
	// 시뮬레이션 바디는 결과를 컴포넌트에 되돌려야 하므로 부모 없는 루트만 허용한다.
	bool CanHavePhysicsBody() const;
	void UpdatePhysicsTransform();
	void RefreshPhysicsSettings();
	FVector PhysicsScale = FVector(1, 1, 1);
	EComponentMobility PhysicsMobility = EComponentMobility::Movable;
	bool bApplyingPhysicsTransform = false;
	friend class FScene;
	FPrimitiveSceneProxy* SceneProxy = nullptr;
};
