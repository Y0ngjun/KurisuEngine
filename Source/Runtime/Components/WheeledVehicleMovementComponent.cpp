#include "EnginePCH.h"
#include "Components/WheeledVehicleMovementComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Physics/BodyInstance.h"
#include "Physics/PhysScene.h"
#include "Physics/PhysXVehicleManager.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "UObject/Casts.h"
#include "Physics/PhysicsMath.h"
#include "Collision/CollisionTypes.h"

#include <PxPhysicsAPI.h>
#include <algorithm>
#include <cmath>

namespace
{
	// 엔진 월드는 cm 단위다. 설정의 토크/관성/감쇠는 미터 기준(N·m, kg·m²)이라 길이가 두 번 들어가는 값은 100²을 곱한다.
	// 스프링 강도 k(kg/s²)와 댐핑 c(kg/s)는 길이가 들어가지 않아 변환이 필요 없다.
	constexpr float MetersSquaredToCmSquared = 10000.0f;
	constexpr float DegreesToRadians = 3.14159265358979f / 180.0f;
	constexpr float RpmToRadPerSec = 2.0f * 3.14159265358979f / 60.0f;
	constexpr float RadPerSecToRpm = 60.0f / (2.0f * 3.14159265358979f);
	constexpr float CmPerSecToKmPerHour = 0.036f; // 1 cm/s = 0.036 km/h

	// 현재 값이 목표 값으로 속도 제한을 두고 다가간다 (UE4 InterpInputValue).
	// 값이 0에서 멀어지는 쪽이면 RiseRate, 0으로 돌아오는 쪽이면 FallRate를 쓴다.
	float InterpInputValue(const FVehicleInputRate& Rate, float Current, float Target, float DeltaTime)
	{
		const float Delta = Target - Current;
		const bool bRising = (Delta > 0.0f) == (Current > 0.0f);
		const float MaxDelta = DeltaTime * (bRising ? Rate.RiseRate : Rate.FallRate);
		return Current + (std::clamp)(Delta, -MaxDelta, MaxDelta);
	}

	// UE4 기어 번호(-1 후진, 0 중립, 1~ 전진)와 PhysX 기어 번호(0 후진, 1 중립, 2~ 전진) 변환
	int32 ToGearNumber(physx::PxU32 PxGear)
	{
		if (PxGear == physx::PxVehicleGearsData::eREVERSE)
			return -1;
		if (PxGear == physx::PxVehicleGearsData::eNEUTRAL)
			return 0;
		return static_cast<int32>(PxGear - physx::PxVehicleGearsData::eFIRST) + 1;
	}
}

UWheeledVehicleMovementComponent::UWheeledVehicleMovementComponent()
{
	// UE4 차량 컴포넌트와 같이 매 프레임 입력과 상태를 갱신한다 (이후 단계에서 채운다).
	PrimaryComponentTick.bCanEverTick = true;

	// 기본 바퀴 4개. 섀시 상자(450 x 180 x 80 cm)의 네 모서리 근처에 둔다 (섀시 로컬 위치).
	WheelSetups.Empty();
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FWheelSetup Setup;
		const bool bFront = Index < 2;
		const bool bLeft = (Index % 2) == 0;
		Setup.AdditionalOffset = FVector(bFront ? 150.0f : -150.0f, bLeft ? -90.0f : 90.0f, -40.0f);
		WheelSetups.Add(Setup);
	}
	// 나머지 설정(조향 곡선, 앞뒤 바퀴의 조향/핸드브레이크 등)은 기본값 함수가 채운다.
	ResetSetupToDefaults();
}

void UWheeledVehicleMovementComponent::SetUpdatedComponent(USceneComponent* NewComponent)
{
	Super::SetUpdatedComponent(NewComponent);
	UpdatedPrimitive = Cast<UPrimitiveComponent>(UpdatedComponent);
}

FPhysXVehicleManager* UWheeledVehicleMovementComponent::GetVehicleManager() const
{
	const UWorld* World = GetWorld();
	const FPhysScene* Scene = World ? World->GetPhysicsScene() : nullptr;
	return Scene ? Scene->GetVehicleManager() : nullptr;
}

bool UWheeledVehicleMovementComponent::ShouldCreatePhysicsState() const
{
	const UWorld* World = GetWorld();
	return World && World->IsGameWorld() && World->HasBegunPlay() && GetVehicleManager();
}

bool UWheeledVehicleMovementComponent::OnCreatePhysicsState()
{
	FPhysXVehicleManager* Manager = GetVehicleManager();
	if (!Manager)
		return false;
	// 다시 만들 때 실패해도 PhysX 차량이 없는 차가 관리자에 남지 않게, 먼저 빼고 성공했을 때만 다시 등록한다.
	Manager->RemoveVehicle(this);
	if (!UpdatedPrimitive)
	{
		KRS_LOG(Error, "[Vehicle] {}: the root component must be a primitive component to be the chassis.", GetName());
		return false;
	}
	// 컴포넌트 등록 순서와 상관없이 섀시 바디가 먼저 만들어져 있어야 한다 (이미 만들어졌으면 아무 일도 하지 않는다).
	UpdatedPrimitive->CreatePhysicsState();
	FBodyInstance& Chassis = UpdatedPrimitive->GetBodyInstance();
	if (!Chassis.IsValidBodyInstance() || !Chassis.IsInstanceSimulatingPhysics())
	{
		KRS_LOG(Error, "[Vehicle] {}: the chassis needs a simulating physics body (collision enabled, simulate physics on).", GetName());
		return false;
	}
	if (!UpdateMassProperties())
	{
		KRS_LOG(Error, "[Vehicle] {}: invalid mass settings (Mass, COMOffset, InertiaTensorScale).", GetName());
		return false;
	}
	if (!SetupVehicle())
	{
		KRS_LOG(Error, "[Vehicle] {}: vehicle setup failed; see the messages above.", GetName());
		return false;
	}
	// 이후 단계: 여기서 PxVehicleDrive4W를 만든 뒤 등록한다 (UE4 CreateVehicle -> AddVehicle).
	Manager->AddVehicle(this);
	return true;
}

void UWheeledVehicleMovementComponent::OnDestroyPhysicsState()
{
	// 월드가 먼저 정리돼 관리자가 이미 없을 수 있다. 관리자가 남아 있을 때만 해제한다.
	if (FPhysXVehicleManager* Manager = GetVehicleManager())
		Manager->RemoveVehicle(this);
	// 관리자 목록에서 뺀 다음에 차량을 해제한다 (관리자가 해제된 차량을 갱신하지 않도록).
	DestroyVehicle();
}

bool UWheeledVehicleMovementComponent::UpdateMassProperties()
{
	if (!UpdatedPrimitive)
		return false;
	FBodyInstance& Chassis = UpdatedPrimitive->GetBodyInstance();
	Chassis.COMNudge = COMOffset;
	Chassis.InertiaTensorScale = InertiaTensorScale;
	// 질량을 정하면서 질량 중심과 관성도 함께 다시 계산한다.
	return Chassis.SetMassOverride(Mass, true);
}

bool UWheeledVehicleMovementComponent::SetupVehicle()
{
	using namespace physx;
	constexpr int32 NumWheels = 4;

	// 물리 상태를 다시 만들 때도 불린다. 이전 차량이 남아 있으면 먼저 없앤다.
	DestroyVehicle();

	// ---------------------------------------------------------------- 검증
	PxRigidDynamic* ChassisBody = nullptr;
	if (UpdatedPrimitive)
	{
		if (PxRigidActor* ChassisActor = UpdatedPrimitive->GetBodyInstance().GetActor())
			ChassisBody = ChassisActor->is<PxRigidDynamic>();
	}
	if (!ChassisBody)
	{
		KRS_LOG(Error, "[Vehicle] {}: the chassis has no dynamic physics body.", GetName());
		return false;
	}
	if (WheelSetups.Num() != NumWheels)
	{
		KRS_LOG(Error, "[Vehicle] {}: a 4-wheel vehicle needs exactly 4 WheelSetups (front left, front right, rear left, rear right), but there are {}.", GetName(), WheelSetups.Num());
		return false;
	}
	const FVehicleCurve& TorqueCurve = EngineSetup.TorqueCurve;
	const int32 NumTorquePoints = TorqueCurve.Keys.Num();
	if (NumTorquePoints < 1 || NumTorquePoints > PxVehicleEngineData::eMAX_NB_ENGINE_TORQUE_CURVE_ENTRIES)
	{
		KRS_LOG(Error, "[Vehicle] {}: the engine torque curve needs 1 to {} points, but has {}.", GetName(), static_cast<int32>(PxVehicleEngineData::eMAX_NB_ENGINE_TORQUE_CURVE_ENTRIES), NumTorquePoints);
		return false;
	}
	if (EngineSetup.MaxRPM <= 0.0f || EngineSetup.MOI <= 0.0f)
	{
		KRS_LOG(Error, "[Vehicle] {}: engine MaxRPM and MOI must be positive.", GetName());
		return false;
	}
	float PeakTorqueNm = 0.0f;
	for (int32 Index = 0; Index < NumTorquePoints; ++Index)
	{
		const FVehicleCurveKey& Key = TorqueCurve.Keys[Index];
		const bool bInRange = Key.Time >= 0.0f && Key.Time <= EngineSetup.MaxRPM;
		const bool bAscending = Index == 0 || Key.Time > TorqueCurve.Keys[Index - 1].Time;
		if (!bInRange || !bAscending || Key.Value < 0.0f)
		{
			KRS_LOG(Error, "[Vehicle] {}: torque curve point {} is invalid (RPM must increase from 0 to MaxRPM, torque must not be negative).", GetName(), Index);
			return false;
		}
		PeakTorqueNm = (std::max)(PeakTorqueNm, Key.Value);
	}
	if (PeakTorqueNm <= 0.0f)
	{
		KRS_LOG(Error, "[Vehicle] {}: the engine torque curve has no positive torque.", GetName());
		return false;
	}
	const int32 NumForwardGears = TransmissionSetup.ForwardGears.Num();
	const int32 MaxForwardGears = static_cast<int32>(PxVehicleGearsData::eGEARSRATIO_COUNT) - 2;
	if (NumForwardGears < 1 || NumForwardGears > MaxForwardGears)
	{
		KRS_LOG(Error, "[Vehicle] {}: the transmission needs 1 to {} forward gears, but has {}.", GetName(), MaxForwardGears, NumForwardGears);
		return false;
	}

	// ---------------------------------------------------------------- 위치와 sprung mass
	// 바퀴 위치는 섀시 로컬(액터 기준)이다. 본 방식은 7장에서 추가한다 (3.4.2).
	// 이 PhysX 함수들이 액터 기준 좌표와 질량 중심을 받아 질량 중심 기준 값을 만든다.
	PxVec3 WheelPositions[NumWheels];
	for (int32 Index = 0; Index < NumWheels; ++Index)
		WheelPositions[Index] = PhysicsMath::ToPx(WheelSetups[Index].AdditionalOffset);
	const PxVec3 CenterOfMass = ChassisBody->getCMassLocalPose().p;
	const float TotalMass = ChassisBody->getMass();

	float SprungMasses[NumWheels];
	// 중력 방향은 -Z (축 번호 2)
	PxVehicleComputeSprungMasses(NumWheels, WheelPositions, CenterOfMass, TotalMass, 2, SprungMasses);

	// ---------------------------------------------------------------- 바퀴
	PxVehicleWheelsSimData* WheelsSimData = PxVehicleWheelsSimData::allocate(NumWheels);
	if (!WheelsSimData)
	{
		KRS_LOG(Error, "[Vehicle] {}: failed to allocate wheel simulation data.", GetName());
		return false;
	}
	WheelsSimData->setChassisMass(TotalMass);

	for (int32 Index = 0; Index < NumWheels; ++Index)
	{
		const FVehicleWheelData& Wheel = WheelSetups[Index].Wheel;
		const float SprungMass = SprungMasses[Index];

		PxVehicleWheelData WheelData;
		WheelData.mRadius = Wheel.ShapeRadius;
		WheelData.mWidth = Wheel.ShapeWidth;
		WheelData.mMass = Wheel.Mass;
		// 원판 관성 I = 1/2 m R². 반지름이 cm라서 kg·cm² 단위가 된다.
		WheelData.mMOI = 0.5f * Wheel.Mass * Wheel.ShapeRadius * Wheel.ShapeRadius;
		WheelData.mDampingRate = Wheel.DampingRate * MetersSquaredToCmSquared;
		WheelData.mMaxBrakeTorque = Wheel.MaxBrakeTorque * MetersSquaredToCmSquared;
		WheelData.mMaxHandBrakeTorque = Wheel.bAffectedByHandbrake ? Wheel.MaxHandBrakeTorque * MetersSquaredToCmSquared : 0.0f;
		WheelData.mMaxSteer = Wheel.SteerAngle * DegreesToRadians;
		WheelData.mToeAngle = 0.0f;
		WheelsSimData->setWheelData(Index, WheelData);

		// 1.3.4: k = m·ω², c = ζ·2·√(k·m)
		const float OmegaSquared = Wheel.SuspensionNaturalFrequency * Wheel.SuspensionNaturalFrequency;
		PxVehicleSuspensionData SuspensionData;
		SuspensionData.mSprungMass = SprungMass;
		SuspensionData.mSpringStrength = SprungMass * OmegaSquared;
		SuspensionData.mSpringDamperRate = Wheel.SuspensionDampingRatio * 2.0f * std::sqrt(SuspensionData.mSpringStrength * SprungMass);
		SuspensionData.mMaxCompression = Wheel.SuspensionMaxRaise;
		SuspensionData.mMaxDroop = Wheel.SuspensionMaxDrop;
		WheelsSimData->setSuspensionData(Index, SuspensionData);

		// 횡방향 강성은 정지 하중 대비 값이고, 종방향 강성은 중력 단위당 값(kg)이라 길이 단위 변환이 필요 없다.
		PxVehicleTireData TireData;
		TireData.mLatStiffX = Wheel.LatStiffMaxLoad;
		TireData.mLatStiffY = Wheel.LatStiffValue;
		TireData.mLongitudinalStiffnessPerUnitGravity = Wheel.LongStiffValue;
		// 마찰 배율: 최종 마찰은 (표면 마찰) x (슬립 곡선 값)이라, 곡선의 세 점 높이에 배율을 곱하면 타이어 전체 마찰이 그만큼 바뀐다.
		// 표면별 마찰은 8장에서 다룬다 (지금은 모든 표면이 같은 기본 표면이다).
		for (int32 Point = 0; Point < 3; ++Point)
			TireData.mFrictionVsSlipGraph[Point][1] *= Wheel.FrictionScale;
		WheelsSimData->setTireData(Index, TireData);

		// 서스펜션 레이캐스트 필터 (UE 휠 트레이스와 같은 규칙, 6.2).
		//  word0: 이 트레이스의 채널(Vehicle). 물체가 Vehicle 채널을 Block하면 맞는다.
		//  word2: 무시할 물체 종류 마스크. 다른 차량(Vehicle)과 폰(Pawn) 위로는 달리지 않는다.
		PxFilterData SuspensionQueryFilter;
		SuspensionQueryFilter.word0 = 1u << static_cast<uint32>(ECC_Vehicle);
		SuspensionQueryFilter.word1 = 0;
		SuspensionQueryFilter.word2 = (1u << static_cast<uint32>(ECC_Vehicle)) | (1u << static_cast<uint32>(ECC_Pawn));
		SuspensionQueryFilter.word3 = 0;
		WheelsSimData->setSceneQueryFilterData(Index, SuspensionQueryFilter);

		// 위치는 모두 질량 중심 기준이다. 서스펜션은 아래(-Z)로 움직인다.
		const PxVec3 WheelCentre = WheelPositions[Index] - CenterOfMass;
		const PxVec3 ForcePoint = WheelCentre + PxVec3(0.0f, 0.0f, Wheel.SuspensionForceOffset);
		WheelsSimData->setWheelCentreOffset(Index, WheelCentre);
		WheelsSimData->setSuspTravelDirection(Index, PxVec3(0.0f, 0.0f, -1.0f));
		WheelsSimData->setSuspForceAppPointOffset(Index, ForcePoint);
		WheelsSimData->setTireForceAppPointOffset(Index, ForcePoint);
		// 지금 섀시에는 바퀴 도형이 없다. 기본 매핑(바퀴 i = 도형 i)을 끈다.
		WheelsSimData->setWheelShapeMapping(Index, -1);
	}

	// 안정성 설정 (8장). 길이/속도 단위가 cm라서 기본값도 PhysX 기본(5m/s, 4m/s)의 100배다.
	WheelsSimData->setSubStepCount(ThresholdLongitudinalSpeed,
	                               static_cast<PxU32>((std::max)(LowForwardSpeedSubStepCount, 1)),
	                               static_cast<PxU32>((std::max)(HighForwardSpeedSubStepCount, 1)));
	WheelsSimData->setMinLongSlipDenominator((std::max)(MinLongSlipDenominator, 1.0f));
	if (bLimitSuspensionExpansionVelocity)
		WheelsSimData->setFlags(PxVehicleWheelsSimFlags(PxVehicleWheelsSimFlag::eLIMIT_SUSPENSION_EXPANSION_VELOCITY));

	// ---------------------------------------------------------------- 엔진, 변속기, 클러치
	PxVehicleDriveSimData4W DriveSimData;

	PxVehicleEngineData Engine;
	Engine.mMOI = EngineSetup.MOI * MetersSquaredToCmSquared;
	Engine.mPeakTorque = PeakTorqueNm * MetersSquaredToCmSquared;
	Engine.mMaxOmega = EngineSetup.MaxRPM * RpmToRadPerSec;
	Engine.mDampingRateFullThrottle = EngineSetup.DampingRateFullThrottle * MetersSquaredToCmSquared;
	Engine.mDampingRateZeroThrottleClutchEngaged = EngineSetup.DampingRateZeroThrottleClutchEngaged * MetersSquaredToCmSquared;
	Engine.mDampingRateZeroThrottleClutchDisengaged = EngineSetup.DampingRateZeroThrottleClutchDisengaged * MetersSquaredToCmSquared;
	// 엔진 생성자가 넣어 둔 기본 점을 지우고, 정규화한 점(RPM/MaxRPM, 토크/최대토크)으로 채운다.
	Engine.mTorqueCurve.clear();
	for (int32 Index = 0; Index < NumTorquePoints; ++Index)
	{
		const FVehicleCurveKey& Key = TorqueCurve.Keys[Index];
		Engine.mTorqueCurve.addPair(Key.Time / EngineSetup.MaxRPM, Key.Value / PeakTorqueNm);
	}
	DriveSimData.setEngineData(Engine);

	// 기어 배열: [후진, 중립, 1단, 2단, ...]. 후진 기어비는 음수다.
	PxVehicleGearsData Gears;
	Gears.mFinalRatio = TransmissionSetup.FinalRatio;
	Gears.mNbRatios = static_cast<PxU32>(NumForwardGears + 2);
	Gears.mSwitchTime = TransmissionSetup.GearSwitchTime;
	for (PxU32 Index = 0; Index < PxVehicleGearsData::eGEARSRATIO_COUNT; ++Index)
		Gears.mRatios[Index] = 0.0f;
	Gears.mRatios[PxVehicleGearsData::eREVERSE] = -TransmissionSetup.ReverseGearRatio;
	Gears.mRatios[PxVehicleGearsData::eNEUTRAL] = 0.0f;
	for (int32 Index = 0; Index < NumForwardGears; ++Index)
		Gears.mRatios[PxVehicleGearsData::eFIRST + Index] = TransmissionSetup.ForwardGears[Index].Ratio;
	DriveSimData.setGearsData(Gears);

	PxVehicleAutoBoxData AutoBox;
	AutoBox.setUpRatios(PxVehicleGearsData::eNEUTRAL, TransmissionSetup.NeutralGearUpRatio);
	for (int32 Index = 0; Index < NumForwardGears; ++Index)
	{
		const auto Gear = static_cast<PxVehicleGearsData::Enum>(PxVehicleGearsData::eFIRST + Index);
		AutoBox.setUpRatios(Gear, TransmissionSetup.ForwardGears[Index].UpRatio);
		AutoBox.setDownRatios(Gear, TransmissionSetup.ForwardGears[Index].DownRatio);
	}
	AutoBox.setLatency(TransmissionSetup.GearAutoBoxLatency);
	DriveSimData.setAutoBoxData(AutoBox);

	PxVehicleClutchData Clutch;
	Clutch.mStrength = TransmissionSetup.ClutchStrength * MetersSquaredToCmSquared;
	DriveSimData.setClutchData(Clutch);

	// ---------------------------------------------------------------- 디퍼렌셜, 조향 기하
	PxVehicleDifferential4WData Differential;
	Differential.mType = static_cast<PxVehicleDifferential4WData::Enum>(static_cast<int32>(DifferentialSetup.DifferentialType));
	Differential.mFrontRearSplit = DifferentialSetup.FrontRearSplit;
	Differential.mFrontLeftRightSplit = DifferentialSetup.FrontLeftRightSplit;
	Differential.mRearLeftRightSplit = DifferentialSetup.RearLeftRightSplit;
	Differential.mCentreBias = DifferentialSetup.CentreBias;
	Differential.mFrontBias = DifferentialSetup.FrontBias;
	Differential.mRearBias = DifferentialSetup.RearBias;
	DriveSimData.setDiffData(Differential);

	// 축거(앞뒤 차축 거리)와 윤거(좌우 바퀴 거리)는 바퀴 위치에서 구한다 (1.6.1).
	PxVehicleAckermannGeometryData Ackermann;
	Ackermann.mAccuracy = AckermannAccuracy;
	Ackermann.mFrontWidth = std::abs(WheelPositions[0].y - WheelPositions[1].y);
	Ackermann.mRearWidth = std::abs(WheelPositions[2].y - WheelPositions[3].y);
	Ackermann.mAxleSeparation = std::abs((WheelPositions[0].x + WheelPositions[1].x) * 0.5f - (WheelPositions[2].x + WheelPositions[3].x) * 0.5f);
	const bool bValidGeometry = Ackermann.mFrontWidth > 0.0f && Ackermann.mRearWidth > 0.0f && Ackermann.mAxleSeparation > 0.0f;
	if (!bValidGeometry)
	{
		KRS_LOG(Error, "[Vehicle] {}: the wheel positions give a zero axle separation or track width; check WheelSetups AdditionalOffset.", GetName());
		WheelsSimData->free();
		return false;
	}
	DriveSimData.setAckermannGeometryData(Ackermann);

	// ---------------------------------------------------------------- 번역 결과 확인 (3-3에서 차량 생성에 쓴다)
	KRS_LOG(Info, "[Vehicle] {}: chassis mass {:.1f} kg, center of mass ({:.1f}, {:.1f}, {:.1f}) cm.", GetName(), TotalMass, CenterOfMass.x, CenterOfMass.y, CenterOfMass.z);
	float SprungMassSum = 0.0f;
	for (int32 Index = 0; Index < NumWheels; ++Index)
	{
		const PxVehicleSuspensionData& Suspension = WheelsSimData->getSuspensionData(Index);
		const PxVehicleWheelData& WheelData = WheelsSimData->getWheelData(Index);
		SprungMassSum += Suspension.mSprungMass;
		KRS_LOG(Info, "[Vehicle]   wheel {}: sprung mass {:.1f} kg, k {:.1f}, c {:.1f}, wheel MOI {:.1f} kg*cm^2, max steer {:.1f} deg.",
		        Index, Suspension.mSprungMass, Suspension.mSpringStrength, Suspension.mSpringDamperRate, WheelData.mMOI, WheelData.mMaxSteer / DegreesToRadians);
	}
	KRS_LOG(Info, "[Vehicle]   sprung mass sum {:.1f} kg (chassis {:.1f} kg).", SprungMassSum, TotalMass);
	KRS_LOG(Info, "[Vehicle]   engine: peak torque {:.0f} N*m ({:.0f} in cm units), max {:.0f} RPM ({:.1f} rad/s), {} torque points.",
	        PeakTorqueNm, Engine.mPeakTorque, EngineSetup.MaxRPM, Engine.mMaxOmega, NumTorquePoints);
	KRS_LOG(Info, "[Vehicle]   gears: reverse {:.2f}, forward {}, final ratio {:.2f}. Ackermann: front width {:.1f}, rear width {:.1f}, axle separation {:.1f} cm.",
	        Gears.mRatios[PxVehicleGearsData::eREVERSE], NumForwardGears, Gears.mFinalRatio, Ackermann.mFrontWidth, Ackermann.mRearWidth, Ackermann.mAxleSeparation);

	// ---------------------------------------------------------------- PhysX 차량 생성
	FPhysScene* Scene = GetWorld() ? GetWorld()->GetPhysicsScene() : nullptr;
	PxPhysics* Physics = Scene ? Scene->GetPhysics() : nullptr;
	PxVehicleDrive4W* Vehicle = Physics ? PxVehicleDrive4W::allocate(NumWheels) : nullptr;
	if (!Vehicle)
	{
		KRS_LOG(Error, "[Vehicle] {}: failed to allocate the PhysX vehicle.", GetName());
		WheelsSimData->free();
		return false;
	}
	// setup이 데이터를 복사하므로 아래에서 바퀴 시뮬레이션 데이터는 바로 해제해도 된다.
	// 네 바퀴가 모두 구동 가능한 바퀴이므로 구동하지 않는 바퀴는 0개다.
	Vehicle->setup(Physics, ChassisBody, *WheelsSimData, DriveSimData, 0);
	Vehicle->setToRestState();
	// 기어: 자동 변속 여부는 설정을 따르고, 1단 상태로 시작한다.
	Vehicle->mDriveDynData.setUseAutoGears(TransmissionSetup.bUseGearAutoBox);
	Vehicle->mDriveDynData.forceGearChange(PxVehicleGearsData::eFIRST);
	WheelsSimData->free();

	PVehicleDrive = Vehicle;
	KRS_LOG(Info, "[Vehicle] {}: PhysX 4-wheel vehicle created (auto gears {}).", GetName(), TransmissionSetup.bUseGearAutoBox ? "on" : "off");
	return true;
}

void UWheeledVehicleMovementComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	ApplyMovementInput(ConsumeInputVector());
	if (DeltaTime > 0.0f)
		ProcessInput(DeltaTime);
}

void UWheeledVehicleMovementComponent::ApplyMovementInput(const FVector& WorldInput)
{
	// UE4 차량 템플릿이 MoveForward/MoveRight 축을 SetThrottleInput/SetSteeringInput에 잇는 것을 대신한다.
	// 컨트롤러는 Pawn의 GetMovementInputRotation 기준 앞/오른쪽 방향으로 입력을 쌓으므로, 같은 축에 내적하면 축 값이 그대로 나온다.
	const bool bHasInput = WorldInput.SizeSquared() > 1.0e-6f;
	// 입력이 없으면 스크립트(SetThrottleInput 등) 값을 덮어쓰지 않는다. 키를 막 뗀 프레임에만 한 번 0으로 돌린다.
	if (!bHasInput && !bAppliedMovementInput)
		return;
	bAppliedMovementInput = bHasInput;

	float Forward = 0.0f;
	float Right = 0.0f;
	if (bHasInput && PawnOwner)
	{
		const FQuat Rotation = PawnOwner->GetMovementInputRotation().Quaternion();
		Forward = (std::clamp)(FVector::Dot(WorldInput, Rotation.GetForwardVector()), -1.0f, 1.0f);
		Right = (std::clamp)(FVector::Dot(WorldInput, Rotation.GetRightVector()), -1.0f, 1.0f);
	}
	// W: 가속, S: 브레이크 (bReverseAsBrake면 정지 후 후진), A/D: 조향
	SetThrottleInput((std::max)(Forward, 0.0f));
	SetBrakeInput((std::max)(-Forward, 0.0f));
	SetSteeringInput(Right);
}

void UWheeledVehicleMovementComponent::SetThrottleInput(float Throttle)
{
	RawThrottleInput = (std::clamp)(Throttle, 0.0f, 1.0f);
}

void UWheeledVehicleMovementComponent::SetBrakeInput(float Brake)
{
	RawBrakeInput = (std::clamp)(Brake, 0.0f, 1.0f);
}

void UWheeledVehicleMovementComponent::SetSteeringInput(float Steering)
{
	RawSteeringInput = (std::clamp)(Steering, -1.0f, 1.0f);
}

void UWheeledVehicleMovementComponent::SetHandbrakeInput(bool bNewHandbrake)
{
	bRawHandbrakeInput = bNewHandbrake;
}

void UWheeledVehicleMovementComponent::SetTargetGear(int32 GearNum, bool bImmediate)
{
	using namespace physx;
	if (!PVehicleDrive)
		return;
	const int32 NumForward = TransmissionSetup.ForwardGears.Num();
	PxU32 PxGear = PxVehicleGearsData::eNEUTRAL;
	if (GearNum < 0)
		PxGear = PxVehicleGearsData::eREVERSE;
	else if (GearNum > 0)
		PxGear = static_cast<PxU32>(PxVehicleGearsData::eFIRST + (std::min)(GearNum, NumForward) - 1);
	if (bImmediate)
		PVehicleDrive->mDriveDynData.forceGearChange(PxGear);
	else
		PVehicleDrive->mDriveDynData.startGearChange(PxGear);
}

void UWheeledVehicleMovementComponent::SetUseAutoGears(bool bUseAuto)
{
	// 설정에도 남겨서 차량을 다시 만들어도 유지한다.
	TransmissionSetup.bUseGearAutoBox = bUseAuto;
	if (PVehicleDrive)
		PVehicleDrive->mDriveDynData.setUseAutoGears(bUseAuto);
}

bool UWheeledVehicleMovementComponent::GetUseAutoGears() const
{
	return PVehicleDrive ? PVehicleDrive->mDriveDynData.getUseAutoGears() : TransmissionSetup.bUseGearAutoBox;
}

int32 UWheeledVehicleMovementComponent::GetCurrentGear() const
{
	return PVehicleDrive ? ToGearNumber(PVehicleDrive->mDriveDynData.getCurrentGear()) : 0;
}

int32 UWheeledVehicleMovementComponent::GetTargetGear() const
{
	return PVehicleDrive ? ToGearNumber(PVehicleDrive->mDriveDynData.getTargetGear()) : 0;
}

float UWheeledVehicleMovementComponent::GetForwardSpeed() const
{
	if (!UpdatedPrimitive || !UpdatedPrimitive->GetBodyInstance().IsValidBodyInstance())
		return 0.0f;
	const FVector Forward = UpdatedPrimitive->GetWorldRotation().Quaternion().GetForwardVector();
	return UpdatedPrimitive->GetPhysicsLinearVelocity().Dot(Forward);
}

float UWheeledVehicleMovementComponent::GetEngineRotationSpeed() const
{
	return PVehicleDrive ? PVehicleDrive->mDriveDynData.getEngineRotationSpeed() * RadPerSecToRpm : 0.0f;
}

float UWheeledVehicleMovementComponent::GetUpDotWorldUp() const
{
	if (!UpdatedPrimitive)
		return 1.0f;
	return UpdatedPrimitive->GetWorldRotation().Quaternion().GetUpVector().Dot(FVector(0.0f, 0.0f, 1.0f));
}

bool UWheeledVehicleMovementComponent::IsFlipped() const
{
	// 위쪽 축이 수평에서 많이 벗어나 바퀴로 설 수 없는 상태 (약 75도 이상 기울면 뒤집힌 것으로 본다)
	return GetUpDotWorldUp() < 0.25f;
}

void UWheeledVehicleMovementComponent::ResetVehicle()
{
	using namespace physx;
	if (!PVehicleDrive || !UpdatedPrimitive)
		return;
	FBodyInstance& Chassis = UpdatedPrimitive->GetBodyInstance();
	if (!Chassis.IsValidBodyInstance())
		return;

	// 위치와 방향(Yaw)은 유지하고 기울기만 펴서, 바퀴가 닿을 수 있게 조금 띄운다.
	const FVector Location = UpdatedPrimitive->GetWorldLocation() + FVector(0.0f, 0.0f, ResetLiftHeight);
	const FRotator Upright(0.0f, UpdatedPrimitive->GetWorldRotation().Yaw, 0.0f);
	Chassis.SetBodyTransform(FTransform(Upright.Quaternion(), Location), true);
	Chassis.SetLinearVelocity(FVector::ZeroVector);
	Chassis.SetAngularVelocityInRadians(FVector::ZeroVector);

	// 바퀴 회전, 엔진 회전수, 서스펜션 상태를 정지 상태로 되돌린다. 기어는 1단으로 시작한다.
	PVehicleDrive->setToRestState();
	PVehicleDrive->mDriveDynData.forceGearChange(PxVehicleGearsData::eFIRST);

	RawThrottleInput = RawBrakeInput = RawSteeringInput = 0.0f;
	bRawHandbrakeInput = false;
	ThrottleInput = BrakeInput = SteeringInput = HandbrakeInput = 0.0f;
	Chassis.WakeInstance();
}

void UWheeledVehicleMovementComponent::ResetSetupToDefaults()
{
	// 엔진 기본값. 어떤 차 종류가 되는지는 게임이 정한다 (엔진은 차종을 모른다).
	Mass = 1500.0f;
	COMOffset = FVector::ZeroVector;
	InertiaTensorScale = FVector(1.0f, 1.0f, 1.0f);
	EngineSetup = FVehicleEngineData();
	TransmissionSetup = FVehicleTransmissionData();
	DifferentialSetup = FVehicleDifferential4WData();
	AckermannAccuracy = 1.0f;
	IdleBrakeInput = 0.0f;
	ThresholdLongitudinalSpeed = 500.0f;
	LowForwardSpeedSubStepCount = 3;
	HighForwardSpeedSubStepCount = 1;
	MinLongSlipDenominator = 400.0f;
	bLimitSuspensionExpansionVelocity = false;

	// 속도(km/h)가 빠를수록 조향을 줄인다.
	SteeringCurve.Keys.Empty();
	SteeringCurve.AddKey(0.0f, 1.0f);
	SteeringCurve.AddKey(40.0f, 0.7f);
	SteeringCurve.AddKey(120.0f, 0.6f);

	// 바퀴: 위치는 그대로 두고 나머지를 기본값으로. 앞바퀴는 조향하고, 뒷바퀴는 핸드브레이크를 받는다.
	for (int32 Index = 0; Index < WheelSetups.Num(); ++Index)
	{
		const bool bFront = Index < 2;
		FVehicleWheelData& Wheel = WheelSetups[Index].Wheel;
		Wheel = FVehicleWheelData();
		Wheel.SteerAngle = bFront ? 50.0f : 0.0f;
		Wheel.bAffectedByHandbrake = !bFront;
	}
}

void UWheeledVehicleMovementComponent::NotifySetupChanged()
{
	++SetupRevision;
	// 이미 만들어진 차량은 새 설정으로 다시 만들고 똑바로 세운다.
	if (IsPhysicsStateCreated())
	{
		RecreatePhysicsState();
		ResetVehicle();
	}
}

bool UWheeledVehicleMovementComponent::CanUpdateVehicle() const
{
	return PVehicleDrive && UpdatedPrimitive && UpdatedPrimitive->GetBodyInstance().IsValidBodyInstance() &&
	       UpdatedPrimitive->GetBodyInstance().IsInScene();
}

void UWheeledVehicleMovementComponent::UpdateWheelStates(const physx::PxWheelQueryResult* Results, int32 Count)
{
	// 섀시가 잠들면 PhysX는 그 차량의 바퀴 결과를 건드리지 않아 기본값(자세 = 원점)이 그대로 남는다.
	// 그걸 받아들이면 멈춘 차의 바퀴가 차체 한가운데로 모인다. 서스펜션 레이캐스트가 실제로 수행된 결과는
	// 레이 방향이 0이 아니므로, 하나도 없으면 이번 갱신은 없었던 것으로 보고 마지막 상태를 유지한다.
	bool bUpdated = false;
	for (int32 Index = 0; Index < Count; ++Index)
		bUpdated = bUpdated || Results[Index].suspLineDir.magnitudeSquared() > 0.0f;
	if (!bUpdated)
		return;

	WheelStates.SetNum(Count);
	int32 WheelsOnGround = 0;
	float JounceSum = 0.0f;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FVehicleWheelState& State = WheelStates[Index];
		State.bInAir = Results[Index].isInAir;
		State.SuspensionJounce = Results[Index].suspJounce;
		State.SteerAngleDegrees = Results[Index].steerAngle / DegreesToRadians;
		State.LocalPose = PhysicsMath::FromPxPose(Results[Index].localPose);
		if (!State.bInAir)
			++WheelsOnGround;
		JounceSum += State.SuspensionJounce;
	}
	DebugWheelsOnGround = WheelsOnGround;
	DebugAverageSuspensionJounce = Count > 0 ? JounceSum / static_cast<float>(Count) : 0.0f;
	DebugFrontLeftSteerAngle = Count > 0 ? WheelStates[0].SteerAngleDegrees : 0.0f;
}

void UWheeledVehicleMovementComponent::ProcessInput(float DeltaTime)
{
	using namespace physx;
	if (!PVehicleDrive)
	{
		DebugForwardSpeedKmh = 0.0f;
		DebugGear = 0;
		DebugEngineRPM = 0.0f;
		return;
	}
	PxVehicleDriveDynData& DriveDyn = PVehicleDrive->mDriveDynData;
	const float ForwardSpeed = GetForwardSpeed();
	const float SpeedKmh = std::abs(ForwardSpeed) * CmPerSecToKmPerHour;

	// ---- 목표 입력: 날 입력에서 시작해 후진 처리를 거친다 ----
	float TargetThrottle = RawThrottleInput;
	float TargetBrake = RawBrakeInput;
	if (bReverseAsBrake && DriveDyn.getUseAutoGears())
	{
		const bool bReversing = DriveDyn.getTargetGear() == PxVehicleGearsData::eREVERSE;
		if (!bReversing)
		{
			// 전진 중에는 브레이크 페달이 브레이크다. 정지한 뒤에도 브레이크만 누르고 있으면 후진으로 바꾼다.
			if (RawBrakeInput > 0.0f && RawThrottleInput <= 0.0f && ForwardSpeed <= StopThreshold)
			{
				DriveDyn.startGearChange(PxVehicleGearsData::eREVERSE);
				TargetThrottle = RawBrakeInput;
				TargetBrake = 0.0f;
			}
		}
		else if (RawThrottleInput > 0.0f && RawBrakeInput <= 0.0f)
		{
			// 후진 중 가속 페달: 뒤로 움직이는 중이면 브레이크, 멈춰 있으면 1단으로 바꿔 전진
			if (ForwardSpeed >= -StopThreshold)
			{
				DriveDyn.startGearChange(PxVehicleGearsData::eFIRST);
				TargetBrake = 0.0f;
			}
			else
			{
				TargetThrottle = 0.0f;
				TargetBrake = RawThrottleInput;
			}
		}
		else
		{
			// 후진 중에는 브레이크 페달이 뒤로 가는 가속 페달이다.
			TargetThrottle = RawBrakeInput;
			TargetBrake = 0.0f;
		}
	}

	// 아무 페달도 안 누르면 약한 브레이크를 건다 (경사에서 저절로 굴러가는 것을 막는다). 기본 0이라 동작이 바뀌지 않는다.
	if (TargetThrottle <= 0.0f && TargetBrake <= 0.0f)
		TargetBrake = IdleBrakeInput;

	// 속도(km/h)별 조향 배율 (1.6.2). 곡선이 비어 있으면 그대로 쓴다.
	const float SteeringScale = SteeringCurve.Keys.Num() > 0 ? SteeringCurve.Eval(SpeedKmh) : 1.0f;
	const float TargetSteering = RawSteeringInput * SteeringScale;
	const float TargetHandbrake = bRawHandbrakeInput ? 1.0f : 0.0f;

	// ---- 보간 (5.2): 키보드의 0/1 입력이 한 프레임에 튀지 않게 한다 ----
	ThrottleInput = InterpInputValue(ThrottleInputRate, ThrottleInput, TargetThrottle, DeltaTime);
	BrakeInput = InterpInputValue(BrakeInputRate, BrakeInput, TargetBrake, DeltaTime);
	HandbrakeInput = InterpInputValue(HandbrakeInputRate, HandbrakeInput, TargetHandbrake, DeltaTime);
	SteeringInput = InterpInputValue(SteeringInputRate, SteeringInput, TargetSteering, DeltaTime);

	// ---- PhysX 차량에 입력 (조향은 왼쪽/오른쪽 두 입력으로 나눈다) ----
	DriveDyn.setAnalogInput(PxVehicleDrive4WControl::eANALOG_INPUT_ACCEL, ThrottleInput);
	DriveDyn.setAnalogInput(PxVehicleDrive4WControl::eANALOG_INPUT_BRAKE, BrakeInput);
	DriveDyn.setAnalogInput(PxVehicleDrive4WControl::eANALOG_INPUT_HANDBRAKE, HandbrakeInput);
	DriveDyn.setAnalogInput(PxVehicleDrive4WControl::eANALOG_INPUT_STEER_LEFT, SteeringInput < 0.0f ? -SteeringInput : 0.0f);
	DriveDyn.setAnalogInput(PxVehicleDrive4WControl::eANALOG_INPUT_STEER_RIGHT, SteeringInput > 0.0f ? SteeringInput : 0.0f);

	// 입력이 있는데 섀시가 잠들어 있으면 깨운다 (정지한 차는 PhysX가 재운다).
	const bool bHasInput = TargetThrottle > 0.0f || TargetBrake > 0.0f || TargetHandbrake > 0.0f || std::abs(TargetSteering) > 0.0f;
	if (bHasInput && UpdatedPrimitive && !UpdatedPrimitive->IsAnyRigidBodyAwake())
		UpdatedPrimitive->WakeRigidBody();

	// ---- 확인용 표시 ----
	DebugForwardSpeedKmh = ForwardSpeed * CmPerSecToKmPerHour;
	DebugGear = GetCurrentGear();
	DebugEngineRPM = GetEngineRotationSpeed();
}

void UWheeledVehicleMovementComponent::DestroyVehicle()
{
	if (!PVehicleDrive)
		return;
	// 섀시 바디가 이미 해제됐어도 안전하다 (PhysX 샘플도 액터를 먼저 release한 뒤 free한다).
	PVehicleDrive->free();
	PVehicleDrive = nullptr;
	KRS_LOG(Info, "[Vehicle] {}: PhysX vehicle destroyed.", GetName());
}
