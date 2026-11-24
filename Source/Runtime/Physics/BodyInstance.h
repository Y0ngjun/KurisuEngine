#pragma once
#include "UObject/Class.h"
#include "Math/Transform.h"
#include "Physics/PhysicalMaterial.h"
#include "Collision/CollisionTypes.h"
#include <vector>
#include "BodyInstance.generated.h"
class FPhysScene;
class UPrimitiveComponent;
class UBodySetup;
class UPhysicalMaterial;
namespace physx
{
	class PxRigidActor;
	class PxShape;
}

UENUM()
enum class ECollisionEnabled : uint8
{
	NoCollision,
	QueryOnly,
	PhysicsOnly,
	QueryAndPhysics
};
UENUM()
enum class EPhysicsType : uint8
{
	Default,
	Kinematic,
	Simulated
};
enum class EPhysicsBodyType : uint8
{
	Static,
	Kinematic,
	Simulated
};

// Properties are serialized; native actor/scene pointers are never serialized.
// SDK보다 먼저 해제해야 한다. 소유 씬 종료 시에도 TermBody가 호출된다.
USTRUCT()
struct FBodyInstance
{
	GENERATED_BODY()
public:
	FBodyInstance() = default;
	~FBodyInstance();
	FBodyInstance(const FBodyInstance&) = delete;
	FBodyInstance& operator=(const FBodyInstance&) = delete;
	FBodyInstance(FBodyInstance&&) = delete;
	FBodyInstance& operator=(FBodyInstance&&) = delete;

	UPROPERTY(EditAnywhere)
	bool bSimulatePhysics = true;
	UPROPERTY(EditAnywhere)
	ECollisionEnabled CollisionEnabled = ECollisionEnabled::NoCollision;
	UPROPERTY(EditAnywhere)
	bool bEnableGravity = true;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float LinearDamping = 0.01f;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float AngularDamping = 0.0f;
	UPROPERTY(EditAnywhere)
	bool bOverrideMass = true;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.001", Delta = "0.1"))
	float MassInKgOverride = 1.0f;
	// UE FBodyInstance::COMNudge. 형상에서 계산한 질량 중심을 바디 로컬 좌표로 옮긴다 (cm). 차량은 낮춰서 뒤집힘을 줄인다.
	UPROPERTY(EditAnywhere)
	FVector COMNudge = FVector::ZeroVector;
	// UE FBodyInstance::InertiaTensorScale. 관성 주축별 배율. 클수록 그 축의 회전이 둔해진다. 기본 (1, 1, 1).
	UPROPERTY(EditAnywhere)
	FVector InertiaTensorScale = FVector(1.0f, 1.0f, 1.0f);
	UPROPERTY(EditAnywhere)
	UPhysicalMaterial* PhysMaterialOverride = nullptr;
	// UE Collision Presets. Custom이 아니면 CollisionEnabled·ObjectType·CollisionResponses를 이 프리셋에서 가져온다
	// (UCollisionProfile). 세 값 중 하나를 직접 바꾸면 Custom이 된다.
	UPROPERTY(EditAnywhere)
	FName CollisionProfileName = FName("Custom");
	// UE FBodyInstance의 충돌 필터. 이 바디의 채널과, 다른 바디/Trace 채널에 대한 응답.
	// 두 바디의 상호 응답은 양쪽 응답 중 약한 쪽이다 (Block > Overlap > Ignore).
	UPROPERTY(EditAnywhere)
	ECollisionChannel ObjectType = ECC_WorldDynamic;
	UPROPERTY(EditAnywhere)
	FCollisionResponseContainer CollisionResponses;
	// 켜면 시뮬레이션 접촉(Block)에서 OnComponentHit을 받는다 (UE: Simulation Generates Hit Events).
	UPROPERTY(EditAnywhere)
	bool bNotifyRigidBodyCollision = false;
	// 접촉·관절 해를 구하는 반복 횟수 (UE와 같은 기본값). 관절로 이어진 래그돌처럼 질량 차이가 큰 바디 사슬은 늘려야 덜 늘어난다.
	UPROPERTY(EditAnywhere, meta = (ClampMin = "1", ClampMax = "255"))
	int32 PositionSolverIterationCount = 8;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "1", ClampMax = "255"))
	int32 VelocitySolverIterationCount = 1;
	void CopyBodyInstancePropertiesFrom(const FBodyInstance& Other);
	ECollisionResponse GetResponseToChannel(ECollisionChannel Channel) const { return CollisionResponses.GetResponse(Channel); }
	// 아래 함수는 값만 바꾼다. 실행 중인 바디에는 UpdateBodySettings로 반영한다.
	// 프리셋을 적용한다. Custom이면 현재 값을 그대로 둔다. 모르는 이름이면 false (값을 바꾸지 않는다).
	bool SetCollisionProfileName(const FName& ProfileName);
	const FName& GetCollisionProfileName() const { return CollisionProfileName; }
	bool IsCustomCollisionProfile() const;
	// 저장된 프리셋 이름으로 값을 다시 채운다 (불러온 뒤 프리셋 정의가 바뀌었어도 이름을 따른다, UE LoadProfileData).
	void LoadProfileData();
	// 직접 값을 바꿀 때 부른다: 프리셋을 Custom으로 바꾼다 (UE InvalidateCollisionProfileName).
	void InvalidateCollisionProfileName();
	void SetObjectType(ECollisionChannel Channel) { if (Channel != ECC_MAX) { ObjectType = Channel; InvalidateCollisionProfileName(); } }
	void SetResponseToChannel(ECollisionChannel Channel, ECollisionResponse Response) { CollisionResponses.SetResponse(Channel, Response); InvalidateCollisionProfileName(); }
	void SetResponseToAllChannels(ECollisionResponse Response) { CollisionResponses.SetAllChannels(Response); InvalidateCollisionProfileName(); }
	bool InitBody(UBodySetup* Setup, const FTransform& Transform, UPrimitiveComponent* Component, FPhysScene& Scene);
	bool RecreateBody(UBodySetup* Setup, const FTransform& Transform, UPrimitiveComponent* Component, FPhysScene& Scene);
	UBodySetup* GetBodySetup() const { return BodySetup; }
	void TermBody();
	bool IsValidBodyInstance() const { return Actor != nullptr; }
	bool IsInScene() const;
	bool AddToScene();
	void RemoveFromScene();

	// attach 성공 시 Actor가 Shape 참조 하나를 보유한다. 호출자의 참조는 소비하지 않는다.
	// 생성자가 가진 참조는 호출자가 release한다. TermBody는 Actor의 참조를 반환한다.
	bool AttachShape(physx::PxShape& Shape);
	bool DetachShape(physx::PxShape& Shape);

	// SDK 접근용 비소유 포인터. 외부에서 release하거나 씬을 옮기거나 userData를 바꾸지 않는다.
	physx::PxRigidActor* GetActor() const { return Actor; }
	UPrimitiveComponent* GetOwnerComponent() const { return OwnerComponent; }
	FPhysScene* GetOwnerScene() const { return OwnerScene; }
	// 생성 시 형상 크기에 적용한 스케일 (UE FBodyInstance::Scale3D)
	const FVector& GetScale3D() const { return Scale3D; }
	// 실행 중 시뮬레이션/키네마틱 전환 (UE SetInstanceSimulatePhysics). 바디를 다시 만들지 않는다.
	// Static 바디는 바꿀 수 없다.
	bool SetInstanceSimulatePhysics(bool bSimulate);

	// ---- 스켈레탈 다중 바디 (UE FBodyInstance::InstanceBodyIndex/InstanceBoneIndex) ----
	// PhysicsAsset의 SkeletalBodySetups 인덱스와 메시 본 인덱스. 단일 바디 컴포넌트는 -1.
	int32 InstanceBodyIndex = -1;
	int32 InstanceBoneIndex = -1;
	// 시뮬레이션 결과를 본 포즈에 섞는 비율. 0이면 애니메이션, 1이면 물리 (UE PhysicsBlendWeight).
	float PhysicsBlendWeight = 1.0f;
	// 같은 값(0 제외)을 가진 바디끼리의 충돌은 소유 컴포넌트의 CollisionDisableTable로 다시 판정한다.
	uint32 SelfCollisionGroup = 0;
	EPhysicsBodyType GetBodyType() const;
	bool IsInstanceSimulatingPhysics() const;
	bool UpdateBodySettings();
	bool SetEnableGravity(bool bEnabled);
	bool SetLinearDamping(float Value);
	bool SetAngularDamping(float Value);
	bool SetMassOverride(float MassInKg, bool bOverride = true);
	bool UpdateMassProperties();
	// World space: cm/s, radians/s, kg*cm/s^2 and kg*cm^2/s^2.
	void AddForce(const FVector& Force, bool bAccelChange = false);
	void AddImpulse(const FVector& Impulse, bool bVelChange = false);
	void AddTorqueInRadians(const FVector& Torque, bool bAccelChange = false);
	void AddAngularImpulseInRadians(const FVector& Impulse, bool bVelChange = false);
	void SetLinearVelocity(const FVector& Velocity, bool bAddToCurrent = false);
	void SetAngularVelocityInRadians(const FVector& Velocity, bool bAddToCurrent = false);
	FVector GetLinearVelocity() const;
	FVector GetAngularVelocityInRadians() const;
	float GetBodyMass() const;
	void WakeInstance();
	void PutInstanceToSleep();
	bool IsInstanceAwake() const;
	void SetBodyTransform(const FTransform& Transform, bool bTeleport = false);
	EPhysicsBodyType ResolveBodyType(const UBodySetup* Setup, const UPrimitiveComponent* Component) const;

private:
	friend class UPrimitiveComponent;
	friend class FPhysScene;
	void ApplyPendingForces();
	void PrepareForces(float DeltaTime, int Steps);
	void ClearPendingForces();
	bool HasValidSettings() const;
	void SaveAppliedSettings();
	void RestoreAppliedSettings();
	FVector PendingForce = FVector::ZeroVector;
	FVector PendingAcceleration = FVector::ZeroVector;
	FVector PendingTorque = FVector::ZeroVector;
	FVector PendingAngularAcceleration = FVector::ZeroVector;
	FVector ForceImpulse = FVector::ZeroVector;
	FVector AccelerationDelta = FVector::ZeroVector;
	FVector TorqueImpulse = FVector::ZeroVector;
	FVector AngularAccelerationDelta = FVector::ZeroVector;
	UBodySetup* BodySetup = nullptr;
	bool AppliedSimulate = true;
	float AppliedMass = 1.0f;
	UPhysicalMaterial* AppliedMaterial = nullptr;
	ECollisionEnabled AppliedCollision = ECollisionEnabled::NoCollision;
	bool AppliedGravity = true;
	bool AppliedOverrideMass = true;
	float AppliedLinearDamping = 0.01f;
	float AppliedAngularDamping = 0;
	FName AppliedProfileName;
	ECollisionChannel AppliedObjectType = ECC_WorldDynamic;
	FCollisionResponseContainer AppliedResponses;
	bool AppliedNotifyRigidBodyCollision = false;
	// 형상의 Simulation/Query 필터 데이터를 현재 설정으로 갱신한다. 바뀌었으면 씬의 쌍 필터링을 다시 한다.
	void UpdateFilterData();
	UPrimitiveComponent* OwnerComponent = nullptr; // 비소유. 컴포넌트가 이 바디를 소유한다.
	FPhysScene* OwnerScene = nullptr;
	FVector Scale3D = FVector(1, 1, 1);
	physx::PxRigidActor* Actor = nullptr;
	std::vector<ECollisionEnabled> ShapeCollisionModes;
	std::vector<bool> ShapeMassContribution;
};
