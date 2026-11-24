#pragma once

#include "Core/Types.h"
#include "Containers/Array.h"
#include "Math/Vector.h"
#include "UObject/Class.h"
#include "VehicleSetupData.generated.h"

// 차량 설정 구조체 모음 (UE4 WheeledVehicleMovementComponent.h / VehicleWheel.h의 FVehicleEngineData 등 대응).
// 이 값들은 사용자가 보는 설정이다. 단위는 UE4와 같다: 길이 cm, 질량 kg, 토크 N·m, 회전 속도 RPM, 각도 도(degree).
// PhysX가 쓰는 값(스프링 강도 k, 댐핑 c, cm 단위 토크 등)으로의 변환은 SetupVehicle(3-2)이 한다.

// 곡선 한 점. X는 곡선마다 다르다 (엔진: RPM, 조향: 속도 km/h).
USTRUCT()
struct FVehicleCurveKey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	float Time = 0.0f;
	UPROPERTY(EditAnywhere)
	float Value = 0.0f;
};

// 점들을 직선으로 이은 곡선 (UE4 FRuntimeFloatCurve 대응). 점은 Time이 작은 순서로 둔다.
// PhysX의 룩업 테이블이 직선 보간이라 같은 방식으로 맞춘다.
USTRUCT()
struct FVehicleCurve
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	TArray<FVehicleCurveKey> Keys;

	void AddKey(float Time, float Value)
	{
		FVehicleCurveKey Key;
		Key.Time = Time;
		Key.Value = Value;
		Keys.Add(Key);
	}

	// 범위 밖은 양 끝 값을 유지한다. 점이 없으면 0.
	float Eval(float Time) const
	{
		const int32 Count = Keys.Num();
		if (Count == 0)
			return 0.0f;
		if (Time <= Keys[0].Time)
			return Keys[0].Value;
		for (int32 Index = 1; Index < Count; ++Index)
		{
			if (Time <= Keys[Index].Time)
			{
				const FVehicleCurveKey& A = Keys[Index - 1];
				const FVehicleCurveKey& B = Keys[Index];
				const float Span = B.Time - A.Time;
				const float Alpha = Span > 0.0f ? (Time - A.Time) / Span : 1.0f;
				return A.Value + (B.Value - A.Value) * Alpha;
			}
		}
		return Keys[Count - 1].Value;
	}
};

// ---------------------------------------------------------------- 바퀴

// 바퀴 한 개의 설정 (UE4 UVehicleWheel 대응). 서스펜션/타이어/브레이크를 모두 가진다.
USTRUCT()
struct FVehicleWheelData
{
	GENERATED_BODY()

	// ---- 휠 ----
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.1"))
	float ShapeRadius = 30.0f; // cm
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.1"))
	float ShapeWidth = 20.0f; // cm
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.001"))
	float Mass = 20.0f; // kg. 바퀴 관성 계산에 쓴다 (I = 0.5 * m * R^2)
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float DampingRate = 0.25f; // 바퀴 회전 감쇠 (kg·m²/s)
	// 최대 조향각 (도). 앞바퀴만 0보다 크게 둔다. 실제 조향각은 이 값에 조향 곡선 배율을 곱한다.
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float SteerAngle = 0.0f;
	UPROPERTY(EditAnywhere)
	bool bAffectedByHandbrake = false;

	// ---- 서스펜션 (1.3) ----
	// 서스펜션이 섀시에 힘을 주는 높이를 바퀴 중심에서 옮기는 양 (cm). 0이면 바퀴 중심. 아래로 옮길수록 기울기가 줄어든다.
	UPROPERTY(EditAnywhere)
	float SuspensionForceOffset = 0.0f;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float SuspensionMaxRaise = 10.0f; // 눌리는 한계 (cm)
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float SuspensionMaxDrop = 10.0f; // 늘어나는 한계 (cm)
	// 고유진동수 ω (1.3.4). 클수록 딱딱하다.
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.01"))
	float SuspensionNaturalFrequency = 7.0f;
	// 댐핑비 ζ (1.3.4). 0이면 계속 출렁이고 1이면 출렁임 없이 복귀한다.
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float SuspensionDampingRatio = 1.0f;

	// ---- 브레이크 ----
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float MaxBrakeTorque = 1500.0f; // N·m
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float MaxHandBrakeTorque = 3000.0f; // N·m

	// ---- 타이어 (1.4) ----
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float LatStiffMaxLoad = 2.0f; // 횡방향 강성이 최대가 되는 하중 (정지 하중의 배수)
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float LatStiffValue = 17.0f; // 그 하중에서의 횡방향 강성
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float LongStiffValue = 1000.0f; // 종방향 강성
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float FrictionScale = 1.0f; // 마찰계수 배율 (표면 마찰에 곱한다)
};

// 4륜차의 바퀴 한 자리 (UE4 FWheelSetup 대응). WheelSetups는 항상 4개: 앞왼쪽, 앞오른쪽, 뒤왼쪽, 뒤오른쪽 순서다 (3.4.1).
USTRUCT()
struct FWheelSetup
{
	GENERATED_BODY()

	// UE4는 스켈레탈 메시의 본 위치에서 바퀴 위치를 읽는다. 지금 섀시는 상자(본 없음)라서 비워 두고,
	// AdditionalOffset을 섀시 로컬 위치(cm)로 직접 쓴다. 7장에서 본 방식으로 전환한다.
	UPROPERTY(EditAnywhere)
	FName BoneName;
	UPROPERTY(EditAnywhere)
	FVector AdditionalOffset = FVector::ZeroVector;
	UPROPERTY(EditAnywhere)
	FVehicleWheelData Wheel;
};

// ---------------------------------------------------------------- 엔진 / 변속기 / 디퍼렌셜

// 엔진 설정 (UE4 FVehicleEngineData 대응, 1.5)
USTRUCT()
struct FVehicleEngineData
{
	GENERATED_BODY()

	// 엔진 회전 관성 (kg·m²). 작으면 RPM이 빨리 오른다.
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.001"))
	float MOI = 1.0f;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "1"))
	float MaxRPM = 4500.0f;
	// RPM별 토크 (N·m). Time = RPM, Value = 토크. PhysX는 점 8개까지만 받는다.
	UPROPERTY(EditAnywhere)
	FVehicleCurve TorqueCurve;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float DampingRateFullThrottle = 0.15f;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float DampingRateZeroThrottleClutchEngaged = 2.0f;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float DampingRateZeroThrottleClutchDisengaged = 0.35f;

	FVehicleEngineData()
	{
		TorqueCurve.AddKey(0.0f, 400.0f);
		TorqueCurve.AddKey(1890.0f, 500.0f);
		TorqueCurve.AddKey(4500.0f, 400.0f);
	}
};

// 기어 한 단 (UE4 FVehicleGearData 대응)
USTRUCT()
struct FVehicleGearData
{
	GENERATED_BODY()

	// 기어비 G (1.5.1). 클수록 힘은 세고 속도는 느리다.
	UPROPERTY(EditAnywhere)
	float Ratio = 1.0f;
	// 자동 변속: 엔진 회전수가 MaxRPM의 이 비율 아래로 내려가면 한 단 내린다 (0~1)
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float DownRatio = 0.5f;
	// 자동 변속: 이 비율 위로 올라가면 한 단 올린다 (0~1)
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float UpRatio = 0.65f;
};

// 변속기 설정 (UE4 FVehicleTransmissionData 대응, 1.5)
USTRUCT()
struct FVehicleTransmissionData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	bool bUseGearAutoBox = true;
	// 변속에 걸리는 시간 (초). 이 동안 동력이 끊긴다.
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float GearSwitchTime = 0.5f;
	// 자동 변속이 연달아 일어나지 않게 두는 대기 시간
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float GearAutoBoxLatency = 2.0f;
	// 최종감속비 (1.5.1). 모든 단에 곱해진다.
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.01"))
	float FinalRatio = 4.0f;
	UPROPERTY(EditAnywhere)
	TArray<FVehicleGearData> ForwardGears;
	// 후진 기어비. 양수로 쓰고 PhysX에 넘길 때 부호를 바꾼다.
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.01"))
	float ReverseGearRatio = 3.0f;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float NeutralGearUpRatio = 0.15f;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float ClutchStrength = 10.0f;

	FVehicleTransmissionData()
	{
		const float Ratios[] = {3.5f, 2.2f, 1.5f, 1.1f, 0.9f};
		for (const float Ratio : Ratios)
		{
			FVehicleGearData Gear;
			Gear.Ratio = Ratio;
			ForwardGears.Add(Gear);
		}
	}
};

// 입력 값이 목표 값을 따라가는 속도 (UE4 FVehicleInputRate 대응). 단위는 초당 변화량이다.
// 키보드처럼 0 또는 1로 튀는 입력을 부드럽게 만든다 (5.2).
USTRUCT()
struct FVehicleInputRate
{
	GENERATED_BODY()

	// 입력이 0에서 멀어지는 방향(커지는 쪽)으로 움직일 때의 속도
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float RiseRate = 6.0f;
	// 입력이 0으로 돌아오는 방향(작아지는 쪽)으로 움직일 때의 속도
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0"))
	float FallRate = 10.0f;

	FVehicleInputRate() = default;
	FVehicleInputRate(float InRiseRate, float InFallRate)
	    : RiseRate(InRiseRate)
	    , FallRate(InFallRate)
	{
	}
};

// 4륜차 구동 방식 (UE4 EVehicleDifferential4W 대응, 1.5.3)
UENUM()
enum class EVehicleDifferential4W : uint8
{
	LimitedSlip_4W,
	LimitedSlip_FrontDrive,
	LimitedSlip_RearDrive,
	Open_4W,
	Open_FrontDrive,
	Open_RearDrive
};

// 디퍼렌셜 설정 (UE4 FVehicleDifferential4WData 대응, 1.5)
USTRUCT()
struct FVehicleDifferential4WData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	EVehicleDifferential4W DifferentialType = EVehicleDifferential4W::LimitedSlip_RearDrive;
	// 4륜일 때 토크를 앞(0)과 뒤(1) 사이에서 나눈다. 0.5면 반반.
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0", ClampMax = "1"))
	float FrontRearSplit = 0.5f;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0", ClampMax = "1"))
	float FrontLeftRightSplit = 0.5f;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0", ClampMax = "1"))
	float RearLeftRightSplit = 0.5f;
	// Limited Slip에서 좌우/앞뒤 바퀴 속도 차이가 이 배수를 넘으면 헛도는 쪽으로 가는 토크를 줄인다.
	UPROPERTY(EditAnywhere, meta = (ClampMin = "1"))
	float CentreBias = 1.3f;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "1"))
	float FrontBias = 1.3f;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "1"))
	float RearBias = 1.3f;
};
