#pragma once

#include "PawnMovementComponent.h"
#include "Components/VehicleSetupData.h"
#include "Math/Transform.h"
#include "WheeledVehicleMovementComponent.generated.h"

class FPhysXVehicleManager;
class UPrimitiveComponent;
namespace physx
{
	class PxVehicleDrive4W;
	struct PxWheelQueryResult;
}

// 바퀴 하나의 매 갱신 결과 (UE4 FWheelStatus 대응). 차량 갱신이 끝날 때마다 관리자가 채운다.
struct FVehicleWheelState
{
	// 서스펜션이 지면에 닿지 못하면 공중이다.
	bool bInAir = true;
	// 서스펜션 압축량 (cm). 정지 자세에서 얼마나 눌렸는지.
	float SuspensionJounce = 0.0f;
	// 지금 바퀴의 조향각 (도)
	float SteerAngleDegrees = 0.0f;
	// 섀시 루트 기준 바퀴 자세: 서스펜션 이동 + 조향 + 구르는 회전이 모두 합쳐진 값 (UE4가 애님 인스턴스로 본에 넘기는 값).
	// 화면의 바퀴 메시가 이 자세를 그대로 따라가면 된다 (7장).
	FTransform LocalPose;
};

// UE4 UWheeledVehicleMovementComponent 대응 (PhysX 시절). 바퀴 달린 차량의 모든 설정과 입력, 시뮬레이션 상태를 가진다.
// 물리 상태를 만들 때 섀시 바디에 질량 설정을 적용하고 씬의 차량 관리자(FPhysXVehicleManager)에 등록한다. 없앨 때는 해제한다.
// 섀시는 UpdatedComponent(액터의 루트)이며, 시뮬레이션하는 PrimitiveComponent여야 한다.
// 실제 차량(PxVehicleDrive4W) 생성과 갱신은 이후 단계에서 채운다.
UCLASS()
class UWheeledVehicleMovementComponent : public UPawnMovementComponent
{
	GENERATED_BODY()
public:
	UWheeledVehicleMovementComponent();

	// UE4와 같은 단위: 질량 kg, 위치 cm.
	// 섀시 질량 (kg). 서스펜션이 받치는 질량(sprung mass) 계산의 기준이 된다.
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.001"))
	float Mass = 1500.0f;
	// 질량 중심을 섀시 형상의 중심에서 옮기는 양 (cm, 섀시 로컬). 아래로(Z 음수) 옮기면 덜 뒤집힌다.
	UPROPERTY(EditAnywhere)
	FVector COMOffset = FVector::ZeroVector;
	// 관성 주축별 배율. 클수록 그 축의 회전(구르기, 끄덕임, 요)이 둔해진다.
	UPROPERTY(EditAnywhere)
	FVector InertiaTensorScale = FVector(1.0f, 1.0f, 1.0f);

	// 바퀴 4개 (UE4 WheelSetups). 순서는 앞왼쪽, 앞오른쪽, 뒤왼쪽, 뒤오른쪽 고정이다 (3.4.1).
	UPROPERTY(EditAnywhere)
	TArray<FWheelSetup> WheelSetups;
	UPROPERTY(EditAnywhere)
	FVehicleEngineData EngineSetup;
	UPROPERTY(EditAnywhere)
	FVehicleDifferential4WData DifferentialSetup;
	UPROPERTY(EditAnywhere)
	FVehicleTransmissionData TransmissionSetup;
	// 속도(km/h)별 조향 배율 (1.6.2). 빠를수록 작게 둔다.
	UPROPERTY(EditAnywhere)
	FVehicleCurve SteeringCurve;
	// 0이면 안쪽/바깥쪽 바퀴가 같은 각도, 1이면 Ackermann 기하를 그대로 따른다 (1.6.1).
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0", ClampMax = "1"))
	float AckermannAccuracy = 1.0f;

	// ---- 입력 설정 (5장) ----
	// 날 입력이 실제 입력(ThrottleInput 등)으로 따라가는 속도 (UE4 ThrottleInputRate 등)
	UPROPERTY(EditAnywhere)
	FVehicleInputRate ThrottleInputRate = FVehicleInputRate(6.0f, 10.0f);
	UPROPERTY(EditAnywhere)
	FVehicleInputRate BrakeInputRate = FVehicleInputRate(6.0f, 10.0f);
	UPROPERTY(EditAnywhere)
	FVehicleInputRate HandbrakeInputRate = FVehicleInputRate(12.0f, 12.0f);
	UPROPERTY(EditAnywhere)
	FVehicleInputRate SteeringInputRate = FVehicleInputRate(2.5f, 5.0f);
	// 켜면 브레이크 페달이 "정지 중에는 후진"이 된다 (자동 변속일 때만). 끄면 브레이크는 항상 브레이크다.
	UPROPERTY(EditAnywhere)
	bool bReverseAsBrake = true;
	// 이 속도(cm/s) 이하면 정지한 것으로 보고 전진/후진을 바꾼다.
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float StopThreshold = 10.0f;

	// ---- 안정성 설정 (8장) ----
	// 이 속도(cm/s)보다 느린 차는 서브스텝을 더 잘게 쪼개 계산한다 (저속 떨림 방지). 기본값은 PhysX 기본(초속 5m)과 같다.
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float ThresholdLongitudinalSpeed = 500.0f;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "1", ClampMax = "20"))
	int32 LowForwardSpeedSubStepCount = 3;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "1", ClampMax = "20"))
	int32 HighForwardSpeedSubStepCount = 1;
	// 종방향 슬립비(1.4.2)의 분모가 이 속도(cm/s) 아래로 내려가지 않게 한다. 차가 멈추지 못하고 떨 때 올린다.
	UPROPERTY(EditAnywhere, meta = (ClampMin = "1"))
	float MinLongSlipDenominator = 400.0f;
	// 켜면 서스펜션이 늘어나는 속도를 제한해 착지와 이륙이 더 자연스럽지만, 조향 제어를 쉽게 잃을 수 있다 (PhysX 문서).
	UPROPERTY(EditAnywhere)
	bool bLimitSuspensionExpansionVelocity = false;
	// 가속과 브레이크를 모두 안 눌렀을 때 걸리는 브레이크 (0 ~ 1). 경사에서 저절로 굴러가는 것을 막는다.
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0", ClampMax = "1"))
	float IdleBrakeInput = 0.0f;
	// ResetVehicle이 차를 일으켜 세울 때 위로 띄우는 높이 (cm)
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float ResetLiftHeight = 100.0f;

	// ---- 실행 중 상태 (읽기 전용, 저장하지 않음) ----
	// 날 입력을 보간한 값. 이 값이 PhysX에 들어간다. 조향은 -1(왼쪽) ~ +1(오른쪽).
	UPROPERTY(VisibleAnywhere, Transient)
	float ThrottleInput = 0.0f;
	UPROPERTY(VisibleAnywhere, Transient)
	float BrakeInput = 0.0f;
	UPROPERTY(VisibleAnywhere, Transient)
	float SteeringInput = 0.0f;
	UPROPERTY(VisibleAnywhere, Transient)
	float HandbrakeInput = 0.0f;
	// 확인용 표시: 전진 속도(km/h), 현재 기어(-1 후진, 0 중립, 1~ 전진), 엔진 회전수(RPM)
	UPROPERTY(VisibleAnywhere, Transient)
	float DebugForwardSpeedKmh = 0.0f;
	UPROPERTY(VisibleAnywhere, Transient)
	int32 DebugGear = 0;
	UPROPERTY(VisibleAnywhere, Transient)
	float DebugEngineRPM = 0.0f;
	// 확인용 표시: 지면에 닿은 바퀴 수 (0~4), 서스펜션 평균 압축량(cm), 앞왼쪽 바퀴의 현재 조향각(도)
	UPROPERTY(VisibleAnywhere, Transient)
	int32 DebugWheelsOnGround = 0;
	UPROPERTY(VisibleAnywhere, Transient)
	float DebugAverageSuspensionJounce = 0.0f;
	UPROPERTY(VisibleAnywhere, Transient)
	float DebugFrontLeftSteerAngle = 0.0f;

	virtual void TickComponent(float DeltaTime) override;

	// ---- 입력 API (UE4와 같은 이름). 값은 다음 Tick에 보간되어 차량에 들어간다. ----
	// 가속 페달 0 ~ 1
	void SetThrottleInput(float Throttle);
	// 브레이크 페달 0 ~ 1
	void SetBrakeInput(float Brake);
	// 조향 -1(왼쪽) ~ +1(오른쪽). 속도별 조향 곡선(SteeringCurve)이 곱해진다.
	void SetSteeringInput(float Steering);
	void SetHandbrakeInput(bool bNewHandbrake);
	// 기어를 바꾼다. GearNum은 -1 후진, 0 중립, 1 이상 전진. bImmediate가 거짓이면 변속 시간(GearSwitchTime)이 걸린다.
	void SetTargetGear(int32 GearNum, bool bImmediate);
	void SetUseAutoGears(bool bUseAuto);
	bool GetUseAutoGears() const;
	// 현재 기어 (-1 후진, 0 중립, 1 이상 전진). 차량이 없으면 0.
	int32 GetCurrentGear() const;
	int32 GetTargetGear() const;
	// 섀시가 앞(+X)으로 가는 속도 (cm/s). 뒤로 가면 음수.
	float GetForwardSpeed() const;
	// 엔진 회전수 (RPM). 차량이 없으면 0.
	float GetEngineRotationSpeed() const;
	// 질량, 서스펜션, 타이어, 엔진, 변속기, 디퍼렌셜, 조향 설정을 엔진 기본값으로 되돌린다. 바퀴 위치와 섀시 크기는 그대로다.
	// 어떤 차 종류가 되는지는 게임이 정한다 (엔진은 "세단", "트럭" 같은 차종을 모른다). 설정만 바꾸고 차량은 건드리지 않으므로,
	// 이어서 NotifySetupChanged를 불러야 실행 중인 차에 반영된다.
	void ResetSetupToDefaults();
	// 설정을 통째로 바꾼 뒤 부른다. 설정 번호를 올려 화면 바퀴 같은 파생물이 다시 맞추게 하고,
	// 차량이 이미 만들어져 있으면 새 설정으로 다시 만들어 똑바로 세운다 (UE의 RecreatePhysicsState).
	void NotifySetupChanged();
	// 설정이 통째로 바뀔 때마다 늘어나는 번호. 설정에서 파생된 것(화면 바퀴 크기 등)이 다시 맞춰야 하는지 알 수 있다.
	uint32 GetSetupRevision() const { return SetupRevision; }

	// 섀시의 위쪽 축과 월드 위쪽의 내적 (1이면 똑바로, 0이면 옆으로 누움, -1이면 뒤집힘). 차량이 없으면 1.
	float GetUpDotWorldUp() const;
	// 차가 뒤집혔거나 옆으로 누워서 스스로 못 일어나는 상태인가
	bool IsFlipped() const;
	// 차를 그 자리에서 똑바로 세운다 (UE 차량 템플릿의 "차량 되돌리기"). 속도, 입력, 바퀴/엔진 상태를 초기화하고
	// ResetLiftHeight만큼 위로 띄운다. 방향(Yaw)은 유지한다.
	void ResetVehicle();

	virtual void SetUpdatedComponent(USceneComponent* NewComponent) override;
	// 섀시 PrimitiveComponent. UpdatedComponent가 PrimitiveComponent가 아니면 nullptr.
	UPrimitiveComponent* GetUpdatedPrimitive() const { return UpdatedPrimitive; }
	// PhysX 차량이 만들어져 있는가. 물리 상태가 만들어지면 참이다.
	bool IsVehicleCreated() const { return PVehicleDrive != nullptr; }
	// 관리자가 갱신할 PhysX 차량. 소유권은 이 컴포넌트에 있다. 없으면 nullptr.
	physx::PxVehicleDrive4W* GetPhysXVehicle() const { return PVehicleDrive; }
	// 섀시 바디가 씬에 살아 있어서 차량을 갱신해도 되는가 (월드 종료 때 섀시가 먼저 사라진 차량은 갱신하면 안 된다).
	bool CanUpdateVehicle() const;
	// 관리자가 차량 갱신 직후 바퀴 결과를 넘겨 준다 (바퀴 4개).
	void UpdateWheelStates(const physx::PxWheelQueryResult* Results, int32 Count);
	// 바퀴별 최근 결과 (앞왼쪽, 앞오른쪽, 뒤왼쪽, 뒤오른쪽)
	const TArray<FVehicleWheelState>& GetWheelStates() const { return WheelStates; }

	// 이 컴포넌트가 속한 월드의 차량 관리자. 월드나 물리 씬이 없으면 nullptr.
	FPhysXVehicleManager* GetVehicleManager() const;

protected:
	// UE4와 같이 게임 월드에서 플레이가 시작된 뒤에만 차량 물리 상태를 만든다.
	virtual bool ShouldCreatePhysicsState() const override;
	virtual bool OnCreatePhysicsState() override;
	virtual void OnDestroyPhysicsState() override;

	// 섀시 바디에 질량, 질량 중심, 관성 배율을 적용한다 (UE4 UpdateMassProperties).
	bool UpdateMassProperties();

	// 사용자 설정(WheelSetups, EngineSetup, ...)을 PhysX 차량 데이터로 번역한다 (UE4 SetupVehicle).
	// 설정이 잘못됐으면 이유를 로그로 남기고 false. 섀시 질량이 먼저 적용돼 있어야 한다.
	// 번역한 데이터로 PxVehicleDrive4W를 만들어 PVehicleDrive에 둔다. 이미 만든 차량이 있으면 먼저 없앤다.
	bool SetupVehicle();
	// PhysX 차량을 해제한다. 만들어진 차량이 없으면 아무 일도 하지 않는다.
	void DestroyVehicle();

	// 날 입력을 보간하고 속도별 조향, 후진 처리를 거쳐 PhysX 차량의 입력으로 넣는다 (UE4 UpdateSimulation의 입력 부분).
	void ProcessInput(float DeltaTime);
	// 컨트롤러가 AddMovementInput으로 쌓은 이동 입력(WASD)을 가속/브레이크/조향 입력으로 바꾼다.
	// Character가 배치만 하면 WASD로 움직이는 것과 같은 경로다.
	void ApplyMovementInput(const FVector& WorldInput);
	// 직전 프레임에 이동 입력을 적용했는가. 키를 뗄 때 한 번만 입력을 0으로 돌리는 데 쓴다.
	bool bAppliedMovementInput = false;

	// 날 입력 (SetThrottleInput 등이 정한 값)
	float RawThrottleInput = 0.0f;
	float RawBrakeInput = 0.0f;
	float RawSteeringInput = 0.0f;
	bool bRawHandbrakeInput = false;

	TArray<FVehicleWheelState> WheelStates;
	uint32 SetupRevision = 0;

	UPrimitiveComponent* UpdatedPrimitive = nullptr;
	// UE4 PVehicleDrive 대응. 물리 상태가 만들어진 동안에만 있고, 이 컴포넌트가 소유하며 DestroyVehicle이 해제한다.
	// 차량은 섀시 PxRigidDynamic을 가리키므로, 섀시 바디가 다시 만들어지면(RecreatePhysicsState) 차량도 다시 만든다.
	physx::PxVehicleDrive4W* PVehicleDrive = nullptr;
};
