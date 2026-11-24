#pragma once

#include "UObject/Class.h"
#include "Math/Transform.h"
#include "Core/NameTypes.h"
#include <functional>
#include "ConstraintInstance.generated.h"

class FPhysScene;
struct FBodyInstance;
namespace physx
{
	class PxD6Joint;
}

// UE PhysicsEngine/ConstraintTypes.h
UENUM()
enum class EAngularConstraintMotion : uint8
{
	ACM_Free,
	ACM_Limited,
	ACM_Locked
};
using enum EAngularConstraintMotion;

UENUM()
enum class ELinearConstraintMotion : uint8
{
	LCM_Free,
	LCM_Limited,
	LCM_Locked
};
using enum ELinearConstraintMotion;

UENUM()
enum class EConstraintFrame : uint8
{
	Frame1, // Body1(자식) 공간
	Frame2  // Body2(부모) 공간
};

// 세 이동 축과 공통 한계 거리(cm)
USTRUCT()
struct FLinearConstraint
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float Limit = 0.0f;
	UPROPERTY(EditAnywhere)
	ELinearConstraintMotion XMotion = LCM_Locked;
	UPROPERTY(EditAnywhere)
	ELinearConstraintMotion YMotion = LCM_Locked;
	UPROPERTY(EditAnywhere)
	ELinearConstraintMotion ZMotion = LCM_Locked;
};

// Swing1은 관절 프레임 Z축, Swing2는 Y축 회전이다 (UE와 같다). 한계는 각 방향 반각(도).
USTRUCT()
struct FConeConstraint
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0", ClampMax = "179"))
	float Swing1LimitDegrees = 45.0f;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0", ClampMax = "179"))
	float Swing2LimitDegrees = 45.0f;
	UPROPERTY(EditAnywhere)
	EAngularConstraintMotion Swing1Motion = ACM_Free;
	UPROPERTY(EditAnywhere)
	EAngularConstraintMotion Swing2Motion = ACM_Free;
};

// Twist는 관절 프레임 X축(PriAxis) 회전이다. 한계는 ±TwistLimitDegrees.
USTRUCT()
struct FTwistConstraint
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0", ClampMax = "179"))
	float TwistLimitDegrees = 45.0f;
	UPROPERTY(EditAnywhere)
	EAngularConstraintMotion TwistMotion = ACM_Free;
};

// 스프링 드라이브 하나. 가속도 기준(질량 무관)이다.
USTRUCT()
struct FConstraintDrive
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float Stiffness = 50.0f;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float Damping = 1.0f;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float MaxForce = 0.0f; // 0이면 무제한
	UPROPERTY(EditAnywhere)
	bool bEnablePositionDrive = false;
};

// 이동 드라이브: Frame1이 Frame2 기준 PositionTarget으로 가도록 당긴다.
USTRUCT()
struct FLinearDriveConstraint
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere)
	FVector PositionTarget = FVector::ZeroVector;
	UPROPERTY(EditAnywhere)
	FConstraintDrive XDrive;
	UPROPERTY(EditAnywhere)
	FConstraintDrive YDrive;
	UPROPERTY(EditAnywhere)
	FConstraintDrive ZDrive;
};

// 회전 드라이브(SLERP): Frame1이 Frame2 기준 OrientationTarget으로 돌도록 당긴다. 잠긴 회전 축이 없어야 한다.
USTRUCT()
struct FAngularDriveConstraint
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere)
	FRotator OrientationTarget = FRotator(0, 0, 0);
	UPROPERTY(EditAnywhere)
	FConstraintDrive SlerpDrive;
};

// UE FConstraintProfileProperties: 한계·드라이브·파괴·충돌 설정 묶음
USTRUCT()
struct FConstraintProfileProperties
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere)
	FLinearConstraint LinearLimit;
	UPROPERTY(EditAnywhere)
	FConeConstraint ConeLimit;
	UPROPERTY(EditAnywhere)
	FTwistConstraint TwistLimit;
	UPROPERTY(EditAnywhere)
	FLinearDriveConstraint LinearDrive;
	UPROPERTY(EditAnywhere)
	FAngularDriveConstraint AngularDrive;
	// 연결된 두 바디끼리는 충돌하지 않는다 (래그돌 이웃 바디)
	UPROPERTY(EditAnywhere)
	bool bDisableCollision = true;
	UPROPERTY(EditAnywhere)
	bool bLinearBreakable = false;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float LinearBreakThreshold = 300.0f;
	UPROPERTY(EditAnywhere)
	bool bAngularBreakable = false;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float AngularBreakThreshold = 500.0f;
	// 오차가 커지면 위치를 강제로 맞춘다. 래그돌이 늘어나거나 폭주하지 않게 한다.
	UPROPERTY(EditAnywhere)
	bool bEnableProjection = true;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float ProjectionLinearTolerance = 5.0f;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float ProjectionAngularTolerance = 180.0f;
};

// UE FConstraintInstance. 두 바디를 잇는 관절의 정의(직렬화)와 실행 중 PxD6Joint(비직렬화).
// 관절 프레임은 각 바디의 로컬 공간에 둔다: Frame1 = Body1(자식), Frame2 = Body2(부모, 없으면 월드).
// X축(PriAxis)이 Twist 축, Y축(SecAxis)이 Swing2 축, Z축이 Swing1 축이다.
// 바디보다 먼저 해제되어야 한다. 바디가 먼저 사라지면 FPhysScene이 이 관절을 먼저 해제한다.
USTRUCT()
struct FConstraintInstance
{
	GENERATED_BODY()
public:
	FConstraintInstance() = default;
	~FConstraintInstance();
	FConstraintInstance(const FConstraintInstance&) = delete;
	FConstraintInstance& operator=(const FConstraintInstance&) = delete;

	UPROPERTY(EditAnywhere)
	FName JointName;
	// Body1은 자식 본, Body2는 부모 본 (UE와 같다)
	UPROPERTY(EditAnywhere)
	FName ConstraintBone1;
	UPROPERTY(EditAnywhere)
	FName ConstraintBone2;
	UPROPERTY(EditAnywhere)
	FVector Pos1 = FVector::ZeroVector;
	UPROPERTY(EditAnywhere)
	FVector PriAxis1 = FVector(1, 0, 0);
	UPROPERTY(EditAnywhere)
	FVector SecAxis1 = FVector(0, 1, 0);
	UPROPERTY(EditAnywhere)
	FVector Pos2 = FVector::ZeroVector;
	UPROPERTY(EditAnywhere)
	FVector PriAxis2 = FVector(1, 0, 0);
	UPROPERTY(EditAnywhere)
	FVector SecAxis2 = FVector(0, 1, 0);
	UPROPERTY(EditAnywhere)
	FConstraintProfileProperties ProfileInstance;

	// 정의(위 속성)만 복사한다. 실행 중 관절은 복사하지 않는다.
	void CopyConstraintParamsFrom(const FConstraintInstance& Other);

	// 프레임 설정. Transform은 해당 바디 로컬 공간의 관절 위치·방향(스케일 무시).
	void SetRefFrame(EConstraintFrame Frame, const FTransform& RefFrame);
	FTransform GetRefFrame(EConstraintFrame Frame) const;
	// 두 바디의 현재 월드 포즈와 관절의 월드 Transform으로 두 프레임을 정한다. Body2가 없으면 Frame2는 월드 공간.
	void SetRefFramesFromWorld(const FTransform& JointWorld, const FBodyInstance* Body1, const FBodyInstance* Body2);

	// 한계 설정 (UE 이름). 실행 중이면 바로 반영한다.
	void SetLinearXLimit(ELinearConstraintMotion Motion, float Limit);
	void SetLinearYLimit(ELinearConstraintMotion Motion, float Limit);
	void SetLinearZLimit(ELinearConstraintMotion Motion, float Limit);
	void SetAngularSwing1Limit(EAngularConstraintMotion Motion, float LimitDegrees);
	void SetAngularSwing2Limit(EAngularConstraintMotion Motion, float LimitDegrees);
	void SetAngularTwistLimit(EAngularConstraintMotion Motion, float LimitDegrees);
	void SetDisableCollision(bool bDisable);
	void SetAngularOrientationTarget(const FQuat& Target);
	void SetLinearPositionTarget(const FVector& Target);

	// Body2가 nullptr이면 월드에 고정한다. Scale은 프레임 위치와 이동 한계에 곱한다(컴포넌트 균일 스케일).
	bool InitConstraint(FBodyInstance* Body1, FBodyInstance* Body2, float Scale = 1.0f);
	void TermConstraint();
	bool IsValidConstraintInstance() const { return ConstraintHandle != nullptr; }
	bool IsBroken() const;
	// 실행 중 관절에 현재 속성(한계·드라이브·파괴·충돌)을 다시 적용한다.
	bool UpdateConstraintSettings();

	FBodyInstance* GetBody1() const { return Body1; }
	FBodyInstance* GetBody2() const { return Body2; }
	physx::PxD6Joint* GetConstraintHandle() const { return ConstraintHandle; }
	// 두 바디 사이 현재 상대 회전 (Frame2 기준 Frame1). 각도 검사·디버그용.
	FQuat GetCurrentRelativeRotation() const;

	// 파괴되었을 때 FPhysScene이 안전한 시점에 부른다 (UE OnConstraintBrokenDelegate).
	std::function<void(FConstraintInstance&)> OnConstraintBroken;
	// 소유자가 붙이는 번호 (스켈레탈 컴포넌트의 Constraints 인덱스 등)
	int32 ConstraintIndex = -1;

private:
	friend class FPhysScene;
	friend class FSimulationEventCallback;
	FBodyInstance* Body1 = nullptr;
	FBodyInstance* Body2 = nullptr;
	FPhysScene* OwnerScene = nullptr;
	physx::PxD6Joint* ConstraintHandle = nullptr;
	float LastScale = 1.0f;
};
