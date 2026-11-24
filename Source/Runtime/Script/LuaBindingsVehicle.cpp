#include "EnginePCH.h"
#include "LuaBindingsInternal.h"

#include "Script/LuaActorRef.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WheeledVehicle.h"
#include "Components/WheeledVehicleMovementComponent.h"
#include "Engine/World.h"
#include "UObject/Casts.h"

// 차량 Lua API. 스크립트가 차를 만들고, 설정하고, 가속/브레이크/조향을 주고, 상태를 읽는다.
// 엔진은 메커니즘(설정을 받는 창구, 입력, 상태)만 준다. 차 종류(세단, 트럭 ...)와 조작 키, HUD는 게임 쪽 스크립트가 정한다.
// 차량 메서드는 액터가 가진 첫 UWheeledVehicleMovementComponent에 적용한다.
// 플레이어 키 입력은 엔진이 따로 받지 않는다. 스크립트가 IsKeyDown으로 읽어 SetThrottle 등을 부른다.
namespace
{
	UWheeledVehicleMovementComponent& Vehicle(const FLuaActorRef& Ref)
	{
		AActor& Actor = Deref(Ref);
		for (UActorComponent* Component : Actor.GetComponents())
			if (auto* Found = Cast<UWheeledVehicleMovementComponent>(Component))
				return *Found;
		throw sol::error(Actor.GetName() + " has no vehicle movement component.");
	}

	// 테이블에 키가 있으면 그 값으로 Out을 바꾼다. 키가 없으면 그대로 둔다.
	template <typename T>
	void Read(const sol::table& Table, const char* Key, T& Out)
	{
		if (sol::optional<T> Value = Table.get<sol::optional<T>>(Key))
			Out = *Value;
	}

	// 허용하지 않은 키가 있으면 경고한다 (오타가 조용히 무시되지 않게). 숫자 키는 배열 항목이라 건너뛴다.
	void WarnUnknownKeys(const sol::table& Table, std::initializer_list<const char*> Allowed, const char* Where)
	{
		for (const auto& Pair : Table)
		{
			if (!Pair.first.is<std::string>())
				continue;
			const std::string Key = Pair.first.as<std::string>();
			bool bKnown = false;
			for (const char* Name : Allowed)
				bKnown = bKnown || Key == Name;
			if (!bKnown)
				KRS_LOG(Warning, "[Lua] ConfigureVehicle: unknown key '{}' in {}", Key, Where);
		}
	}

	// {{x, y}, {x, y}, ...} 형태의 점 목록을 곡선으로 읽는다. 키가 없으면 그대로 둔다.
	void ReadCurve(const sol::table& Table, const char* Key, FVehicleCurve& Curve)
	{
		sol::optional<sol::table> Points = Table.get<sol::optional<sol::table>>(Key);
		if (!Points)
			return;
		Curve.Keys.Empty();
		const size_t Count = Points->size();
		for (size_t Index = 1; Index <= Count; ++Index)
		{
			sol::optional<sol::table> Pair = (*Points)[Index];
			sol::optional<float> X = Pair ? Pair->get<sol::optional<float>>(1) : sol::nullopt;
			sol::optional<float> Y = Pair ? Pair->get<sol::optional<float>>(2) : sol::nullopt;
			if (!X || !Y)
				throw sol::error(std::string(Key) + ": each point must be {x, y}.");
			Curve.AddKey(*X, *Y);
		}
	}

	// 바퀴 하나(또는 한 쌍)의 설정. 단위는 cm, kg, N·m, 도다.
	void ApplyWheelTable(FVehicleWheelData& Wheel, const sol::table& Table)
	{
		WarnUnknownKeys(Table,
		                {"Radius", "Width", "Mass", "DampingRate", "SteerAngle", "Handbrake", "ForceOffset", "Travel", "MaxRaise", "MaxDrop",
		                 "Frequency", "DampingRatio", "BrakeTorque", "HandbrakeTorque", "LatStiffMaxLoad", "LatStiff", "LongStiff", "FrictionScale"},
		                "a wheel table");
		Read(Table, "Radius", Wheel.ShapeRadius);
		Read(Table, "Width", Wheel.ShapeWidth);
		Read(Table, "Mass", Wheel.Mass);
		Read(Table, "DampingRate", Wheel.DampingRate);
		Read(Table, "SteerAngle", Wheel.SteerAngle);
		Read(Table, "Handbrake", Wheel.bAffectedByHandbrake);
		Read(Table, "ForceOffset", Wheel.SuspensionForceOffset);
		// Travel은 눌림과 늘어남 한계를 한꺼번에 정한다. 따로 정하려면 MaxRaise, MaxDrop을 쓴다.
		if (sol::optional<float> Travel = Table.get<sol::optional<float>>("Travel"))
			Wheel.SuspensionMaxRaise = Wheel.SuspensionMaxDrop = *Travel;
		Read(Table, "MaxRaise", Wheel.SuspensionMaxRaise);
		Read(Table, "MaxDrop", Wheel.SuspensionMaxDrop);
		Read(Table, "Frequency", Wheel.SuspensionNaturalFrequency);
		Read(Table, "DampingRatio", Wheel.SuspensionDampingRatio);
		Read(Table, "BrakeTorque", Wheel.MaxBrakeTorque);
		Read(Table, "HandbrakeTorque", Wheel.MaxHandBrakeTorque);
		Read(Table, "LatStiffMaxLoad", Wheel.LatStiffMaxLoad);
		Read(Table, "LatStiff", Wheel.LatStiffValue);
		Read(Table, "LongStiff", Wheel.LongStiffValue);
		Read(Table, "FrictionScale", Wheel.FrictionScale);
	}

	EVehicleDifferential4W ToDifferentialType(const std::string& Name)
	{
		if (Name == "LimitedSlip_4W") return EVehicleDifferential4W::LimitedSlip_4W;
		if (Name == "LimitedSlip_FrontDrive") return EVehicleDifferential4W::LimitedSlip_FrontDrive;
		if (Name == "LimitedSlip_RearDrive") return EVehicleDifferential4W::LimitedSlip_RearDrive;
		if (Name == "Open_4W") return EVehicleDifferential4W::Open_4W;
		if (Name == "Open_FrontDrive") return EVehicleDifferential4W::Open_FrontDrive;
		if (Name == "Open_RearDrive") return EVehicleDifferential4W::Open_RearDrive;
		throw sol::error("Unknown differential type: " + Name +
		                 " (LimitedSlip_4W, LimitedSlip_FrontDrive, LimitedSlip_RearDrive, Open_4W, Open_FrontDrive, Open_RearDrive)");
	}

	// ConfigureVehicle의 본체. 테이블에 있는 값만 바꾼다. 형식은 Content/Scripts/Vehicle/Presets.lua 참고.
	void ApplyVehicleTable(UWheeledVehicleMovementComponent& Movement, const sol::table& Table)
	{
		WarnUnknownKeys(Table,
		                {"Mass", "COMOffset", "InertiaTensorScale", "Wheels", "Engine", "Transmission", "Differential", "SteeringCurve",
		                 "AckermannAccuracy", "IdleBrake", "ThresholdLongitudinalSpeed", "LowSubSteps", "HighSubSteps", "MinLongSlipDenominator",
		                 "LimitSuspensionExpansionVelocity", "Color"},
		                "the vehicle table");
		Read(Table, "Mass", Movement.Mass);
		Read(Table, "COMOffset", Movement.COMOffset);
		Read(Table, "InertiaTensorScale", Movement.InertiaTensorScale);
		Read(Table, "AckermannAccuracy", Movement.AckermannAccuracy);
		Read(Table, "IdleBrake", Movement.IdleBrakeInput);
		Read(Table, "ThresholdLongitudinalSpeed", Movement.ThresholdLongitudinalSpeed);
		Read(Table, "LowSubSteps", Movement.LowForwardSpeedSubStepCount);
		Read(Table, "HighSubSteps", Movement.HighForwardSpeedSubStepCount);
		Read(Table, "MinLongSlipDenominator", Movement.MinLongSlipDenominator);
		Read(Table, "LimitSuspensionExpansionVelocity", Movement.bLimitSuspensionExpansionVelocity);
		ReadCurve(Table, "SteeringCurve", Movement.SteeringCurve);

		// 바퀴: All -> Front/Rear -> [1]~[4] 순서로 덮어쓴다. 1~4는 앞왼쪽, 앞오른쪽, 뒤왼쪽, 뒤오른쪽이다.
		if (sol::optional<sol::table> Wheels = Table.get<sol::optional<sol::table>>("Wheels"))
		{
			WarnUnknownKeys(*Wheels, {"All", "Front", "Rear"}, "the Wheels table");
			if (Movement.WheelSetups.Num() != 4)
				throw sol::error("Wheels: the vehicle must have exactly 4 wheels.");
			auto ApplyTo = [&](const char* Key, std::initializer_list<int32> Indices)
			{
				if (sol::optional<sol::table> Group = Wheels->get<sol::optional<sol::table>>(Key))
					for (const int32 Index : Indices)
						ApplyWheelTable(Movement.WheelSetups[Index].Wheel, *Group);
			};
			ApplyTo("All", {0, 1, 2, 3});
			ApplyTo("Front", {0, 1});
			ApplyTo("Rear", {2, 3});
			for (int32 Index = 0; Index < 4; ++Index)
				if (sol::optional<sol::table> Single = Wheels->get<sol::optional<sol::table>>(Index + 1))
					ApplyWheelTable(Movement.WheelSetups[Index].Wheel, *Single);
		}

		if (sol::optional<sol::table> Engine = Table.get<sol::optional<sol::table>>("Engine"))
		{
			WarnUnknownKeys(*Engine, {"MOI", "MaxRPM", "TorqueCurve", "DampingRateFullThrottle", "DampingRateZeroThrottleClutchEngaged",
			                          "DampingRateZeroThrottleClutchDisengaged"},
			                "the Engine table");
			FVehicleEngineData& Data = Movement.EngineSetup;
			Read(*Engine, "MOI", Data.MOI);
			Read(*Engine, "MaxRPM", Data.MaxRPM);
			ReadCurve(*Engine, "TorqueCurve", Data.TorqueCurve);
			Read(*Engine, "DampingRateFullThrottle", Data.DampingRateFullThrottle);
			Read(*Engine, "DampingRateZeroThrottleClutchEngaged", Data.DampingRateZeroThrottleClutchEngaged);
			Read(*Engine, "DampingRateZeroThrottleClutchDisengaged", Data.DampingRateZeroThrottleClutchDisengaged);
		}

		if (sol::optional<sol::table> Transmission = Table.get<sol::optional<sol::table>>("Transmission"))
		{
			WarnUnknownKeys(*Transmission, {"Gears", "UpRatio", "DownRatio", "FinalRatio", "ReverseRatio", "SwitchTime", "AutoLatency",
			                                "ClutchStrength", "NeutralUpRatio", "AutoGears"},
			                "the Transmission table");
			FVehicleTransmissionData& Data = Movement.TransmissionSetup;
			// Gears는 기어비 숫자 목록. UpRatio, DownRatio는 모든 단에 같은 자동 변속 기준을 준다.
			if (sol::optional<sol::table> Gears = Transmission->get<sol::optional<sol::table>>("Gears"))
			{
				FVehicleGearData Template;
				Read(*Transmission, "UpRatio", Template.UpRatio);
				Read(*Transmission, "DownRatio", Template.DownRatio);
				Data.ForwardGears.Empty();
				const size_t Count = Gears->size();
				for (size_t Index = 1; Index <= Count; ++Index)
				{
					FVehicleGearData Gear = Template;
					sol::optional<float> Ratio = Gears->get<sol::optional<float>>(Index);
					if (!Ratio)
						throw sol::error("Gears: every entry must be a number.");
					Gear.Ratio = *Ratio;
					Data.ForwardGears.Add(Gear);
				}
			}
			Read(*Transmission, "FinalRatio", Data.FinalRatio);
			Read(*Transmission, "ReverseRatio", Data.ReverseGearRatio);
			Read(*Transmission, "SwitchTime", Data.GearSwitchTime);
			Read(*Transmission, "AutoLatency", Data.GearAutoBoxLatency);
			Read(*Transmission, "ClutchStrength", Data.ClutchStrength);
			Read(*Transmission, "NeutralUpRatio", Data.NeutralGearUpRatio);
			Read(*Transmission, "AutoGears", Data.bUseGearAutoBox);
		}

		if (sol::optional<sol::table> Differential = Table.get<sol::optional<sol::table>>("Differential"))
		{
			WarnUnknownKeys(*Differential, {"Type", "FrontRearSplit", "FrontLeftRightSplit", "RearLeftRightSplit", "CentreBias", "FrontBias", "RearBias"},
			                "the Differential table");
			FVehicleDifferential4WData& Data = Movement.DifferentialSetup;
			if (sol::optional<std::string> Type = Differential->get<sol::optional<std::string>>("Type"))
				Data.DifferentialType = ToDifferentialType(*Type);
			Read(*Differential, "FrontRearSplit", Data.FrontRearSplit);
			Read(*Differential, "FrontLeftRightSplit", Data.FrontLeftRightSplit);
			Read(*Differential, "RearLeftRightSplit", Data.RearLeftRightSplit);
			Read(*Differential, "CentreBias", Data.CentreBias);
			Read(*Differential, "FrontBias", Data.FrontBias);
			Read(*Differential, "RearBias", Data.RearBias);
		}
	}
}

namespace LuaBindingsDetail
{
	void RegisterVehicle(sol::state& Lua)
	{
		// SpawnVehicle(위치, [회전], [액터 이름]) -> 액터. 설정은 만든 뒤 ConfigureVehicle로 한다.
		Lua.set_function("SpawnVehicle", [](sol::this_environment Env, const FVector& Location, sol::optional<FRotator> Rotation, sol::optional<std::string> Name)
		                 {
			UWorld* World = WorldFromEnv(Env, "SpawnVehicle");
			const FTransform Transform(Rotation.value_or(FRotator(0, 0, 0)), Location, FVector(1, 1, 1));
			auto* Actor = World->SpawnActor<AWheeledVehicle>(Name ? FName(*Name) : NAME_None, &Transform);
			return FLuaActorRef::Make(Actor); });

		sol::usertype<FLuaActorRef> ActorType = Lua["Actor"];

		// 차량 설정을 테이블로 바꾼다 (질량, 바퀴, 엔진, 변속기, 디퍼렌셜, 조향 곡선 ...). 테이블에 있는 값만 바꾼다.
		// bResetFirst가 true면 먼저 엔진 기본값으로 되돌린 뒤 적용한다 (이전 설정의 흔적이 남지 않는다).
		// 이미 만들어진 차량은 새 설정으로 다시 만들고 똑바로 세운다.
		ActorType["ConfigureVehicle"] = [](const FLuaActorRef& Ref, const sol::table& Table, sol::optional<bool> bResetFirst)
		{
			UWheeledVehicleMovementComponent& Movement = Vehicle(Ref);
			if (bResetFirst.value_or(false))
				Movement.ResetSetupToDefaults();
			ApplyVehicleTable(Movement, Table);
			Movement.NotifySetupChanged();
			// Color = { R, G, B } (0~1): 차체 도색. 물리 설정이 아니라 액터의 모양이라 여기서 따로 적용한다.
			if (sol::optional<sol::table> Color = Table.get<sol::optional<sol::table>>("Color"))
				if (auto* Car = Cast<AWheeledVehicle>(&Deref(Ref)))
					Car->SetBodyColor(FLinearColor(Color->get_or(1, 1.0f), Color->get_or(2, 1.0f), Color->get_or(3, 1.0f), 1.0f));
		};

		// 차체 도색 색을 바꾼다 (0~1).
		ActorType["SetBodyColor"] = [](const FLuaActorRef& Ref, float R, float G, float B)
		{
			auto* Car = Cast<AWheeledVehicle>(&Deref(Ref));
			if (!Car)
				throw sol::error("SetBodyColor works on a wheeled vehicle.");
			Car->SetBodyColor(FLinearColor(R, G, B, 1.0f));
		};

		// 입력: 가속/브레이크 0~1, 조향 -1(왼쪽)~+1(오른쪽). 값은 입력 보간(5.2)을 거쳐 차량에 들어간다.
		ActorType["SetThrottle"] = [](const FLuaActorRef& Ref, float Value)
		{ Vehicle(Ref).SetThrottleInput(Value); };
		ActorType["SetBrake"] = [](const FLuaActorRef& Ref, float Value)
		{ Vehicle(Ref).SetBrakeInput(Value); };
		ActorType["SetSteering"] = [](const FLuaActorRef& Ref, float Value)
		{ Vehicle(Ref).SetSteeringInput(Value); };
		ActorType["SetHandbrake"] = [](const FLuaActorRef& Ref, bool bOn)
		{ Vehicle(Ref).SetHandbrakeInput(bOn); };

		// 뒤집힌 차를 그 자리에서 세운다.
		ActorType["ResetVehicle"] = [](const FLuaActorRef& Ref)
		{ Vehicle(Ref).ResetVehicle(); };

		// 상태: 전진 속도(km/h, 뒤로 가면 음수), 기어(-1 후진, 0 중립, 1~ 전진), 엔진 회전수와 최대 회전수(RPM), 뒤집힘 여부
		ActorType["GetVehicleSpeed"] = [](const FLuaActorRef& Ref)
		{ return Vehicle(Ref).GetForwardSpeed() * 0.036f; };
		ActorType["GetVehicleGear"] = [](const FLuaActorRef& Ref)
		{ return Vehicle(Ref).GetCurrentGear(); };
		ActorType["GetVehicleRPM"] = [](const FLuaActorRef& Ref)
		{ return Vehicle(Ref).GetEngineRotationSpeed(); };
		ActorType["GetVehicleMaxRPM"] = [](const FLuaActorRef& Ref)
		{ return Vehicle(Ref).EngineSetup.MaxRPM; };
		ActorType["IsVehicleFlipped"] = [](const FLuaActorRef& Ref)
		{ return Vehicle(Ref).IsFlipped(); };
	}
}
