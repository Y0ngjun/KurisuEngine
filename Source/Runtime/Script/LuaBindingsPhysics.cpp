#include "EnginePCH.h"
#include "LuaBindingsInternal.h"

#include "Script/LuaActorRef.h"
#include "Script/LuaScriptManager.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "GameFramework/Actor/SkeletalMeshActor.h"
#include "GameFramework/Actor/TextRenderActor.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/PhysicsConstraintComponent.h"
#include "Physics/PhysicalMaterial.h"
#include "Animation/AnimSequence.h"
#include "Render/SkeletalMesh.h"
#include "Render/Material.h"
#include "Asset/AssetManager.h"
#include "Engine/World.h"
#include "UObject/Casts.h"
#include "UObject/UObjectGlobals.h"

// 물리·스폰 Lua API. 데모나 게임 스크립트가 C++ 없이 바디를 만들고, 힘을 주고, 관절을 잇고, 충돌 이벤트를 받는다.
// 액터 메서드는 루트(없으면 첫) PrimitiveComponent에 적용한다.
namespace
{
	struct FLuaPhysicsBinding
	{
		AActor* Owner;          // 콜백을 건 스크립트의 액터
		FLuaComponentRef Target; // 델리게이트를 가진 컴포넌트
		enum class EKind : uint8
		{
			BeginOverlap,
			EndOverlap,
			Hit,
			ConstraintBroken
		} Kind;
		FDelegateHandle Handle;
	};
	TArray<FLuaPhysicsBinding> PhysicsBindings;

	UPrimitiveComponent* FindPrimitive(AActor& Actor)
	{
		if (auto* Root = Cast<UPrimitiveComponent>(Actor.GetRootComponent()))
			return Root;
		for (UActorComponent* Component : Actor.GetComponents())
			if (auto* Primitive = Cast<UPrimitiveComponent>(Component))
				return Primitive;
		return nullptr;
	}

	UPrimitiveComponent& Primitive(const FLuaActorRef& Ref)
	{
		if (UPrimitiveComponent* Found = FindPrimitive(Deref(Ref)))
			return *Found;
		throw sol::error(Deref(Ref).GetName() + " has no primitive component.");
	}

	template <typename T>
	T& ComponentOf(const FLuaActorRef& Ref, const char* What)
	{
		for (UActorComponent* Component : Deref(Ref).GetComponents())
			if (auto* Found = Cast<T>(Component))
				return *Found;
		throw sol::error(Deref(Ref).GetName() + " has no " + What + ".");
	}

	FTransform MakeTransform(const FVector& Location, sol::optional<FRotator> Rotation, sol::optional<FVector> Scale)
	{
		return FTransform(Rotation.value_or(FRotator(0, 0, 0)), Location, Scale.value_or(FVector(1, 1, 1)));
	}

	ECollisionEnabled ToCollisionEnabled(const std::string& Name)
	{
		if (Name == "NoCollision") return ECollisionEnabled::NoCollision;
		if (Name == "QueryOnly") return ECollisionEnabled::QueryOnly;
		if (Name == "PhysicsOnly") return ECollisionEnabled::PhysicsOnly;
		if (Name == "QueryAndPhysics") return ECollisionEnabled::QueryAndPhysics;
		throw sol::error("Unknown collision mode: " + Name);
	}

	ECollisionChannel ToChannel(const std::string& Name)
	{
		static const TMap<std::string, ECollisionChannel> Map = {
		    {"WorldStatic", ECC_WorldStatic}, {"WorldDynamic", ECC_WorldDynamic}, {"Pawn", ECC_Pawn},
		    {"Visibility", ECC_Visibility}, {"Camera", ECC_Camera}, {"PhysicsBody", ECC_PhysicsBody},
		    {"Vehicle", ECC_Vehicle}, {"Destructible", ECC_Destructible}, {"GameTraceChannel1", ECC_GameTraceChannel1},
		    {"GameTraceChannel2", ECC_GameTraceChannel2}, {"GameTraceChannel3", ECC_GameTraceChannel3},
		    {"GameTraceChannel4", ECC_GameTraceChannel4}};
		if (const ECollisionChannel* Found = Map.Find(Name))
			return *Found;
		throw sol::error("Unknown collision channel: " + Name);
	}

	ECollisionResponse ToResponse(const std::string& Name)
	{
		if (Name == "Ignore") return ECR_Ignore;
		if (Name == "Overlap") return ECR_Overlap;
		if (Name == "Block") return ECR_Block;
		throw sol::error("Unknown collision response: " + Name);
	}

	EAngularConstraintMotion ToAngular(const std::string& Name)
	{
		if (Name == "Free") return ACM_Free;
		if (Name == "Limited") return ACM_Limited;
		if (Name == "Locked") return ACM_Locked;
		throw sol::error("Unknown angular motion: " + Name);
	}

	ELinearConstraintMotion ToLinear(const std::string& Name)
	{
		if (Name == "Free") return LCM_Free;
		if (Name == "Limited") return LCM_Limited;
		if (Name == "Locked") return LCM_Locked;
		throw sol::error("Unknown linear motion: " + Name);
	}

	// 관절 옵션 테이블. 빈 필드는 기본값(구면 관절, 선형 고정)을 그대로 둔다.
	void ApplyConstraintOptions(FConstraintProfileProperties& P, const sol::table& Opt)
	{
		if (auto V = Opt.get<sol::optional<std::string>>("LinearX")) P.LinearLimit.XMotion = ToLinear(*V);
		if (auto V = Opt.get<sol::optional<std::string>>("LinearY")) P.LinearLimit.YMotion = ToLinear(*V);
		if (auto V = Opt.get<sol::optional<std::string>>("LinearZ")) P.LinearLimit.ZMotion = ToLinear(*V);
		if (auto V = Opt.get<sol::optional<float>>("LinearLimit")) P.LinearLimit.Limit = *V;
		if (auto V = Opt.get<sol::optional<std::string>>("Swing1")) P.ConeLimit.Swing1Motion = ToAngular(*V);
		if (auto V = Opt.get<sol::optional<std::string>>("Swing2")) P.ConeLimit.Swing2Motion = ToAngular(*V);
		if (auto V = Opt.get<sol::optional<std::string>>("Twist")) P.TwistLimit.TwistMotion = ToAngular(*V);
		if (auto V = Opt.get<sol::optional<float>>("Swing1Limit")) P.ConeLimit.Swing1LimitDegrees = *V;
		if (auto V = Opt.get<sol::optional<float>>("Swing2Limit")) P.ConeLimit.Swing2LimitDegrees = *V;
		if (auto V = Opt.get<sol::optional<float>>("TwistLimit")) P.TwistLimit.TwistLimitDegrees = *V;
		if (auto V = Opt.get<sol::optional<bool>>("DisableCollision")) P.bDisableCollision = *V;
		if (auto V = Opt.get<sol::optional<float>>("LinearBreakThreshold"))
		{
			P.bLinearBreakable = true;
			P.LinearBreakThreshold = *V;
		}
		if (auto V = Opt.get<sol::optional<float>>("AngularBreakThreshold"))
		{
			P.bAngularBreakable = true;
			P.AngularBreakThreshold = *V;
		}
	}

	// Lua 콜백을 부른다. 콜백을 건 액터가 지워졌으면 부르지 않는다.
	template <typename... Ts>
	void InvokeLua(const FLuaActorRef& OwnerRef, const sol::protected_function& Fn, const char* What, Ts&&... Args)
	{
		if (!OwnerRef.Get())
			return;
		sol::protected_function_result Result = Fn(std::forward<Ts>(Args)...);
		if (!Result.valid())
		{
			sol::error Err = Result;
			KRS_LOG(Error, "[Lua] {}: {}", What, Err.what());
		}
	}

	AActor* ScriptOwner(sol::this_environment& Env, const char* FuncName)
	{
		AActor* Owner = LuaBindingsDetail::OwnerFromEnv(Env);
		if (!Owner)
			throw sol::error(FString(FuncName) + " needs an Actor script context.");
		return Owner;
	}

	void RemoveBinding(const FLuaPhysicsBinding& Binding)
	{
		UActorComponent* Component = Binding.Target.Get();
		if (!Component)
			return;
		if (Binding.Kind == FLuaPhysicsBinding::EKind::ConstraintBroken)
		{
			if (auto* Constraint = Cast<UPhysicsConstraintComponent>(Component))
				Constraint->OnConstraintBroken.Remove(Binding.Handle);
			return;
		}
		auto* Prim = Cast<UPrimitiveComponent>(Component);
		if (!Prim)
			return;
		switch (Binding.Kind)
		{
		case FLuaPhysicsBinding::EKind::BeginOverlap: Prim->OnComponentBeginOverlap.Remove(Binding.Handle); break;
		case FLuaPhysicsBinding::EKind::EndOverlap: Prim->OnComponentEndOverlap.Remove(Binding.Handle); break;
		case FLuaPhysicsBinding::EKind::Hit: Prim->OnComponentHit.Remove(Binding.Handle); break;
		default: break;
		}
	}
}

namespace LuaBindingsDetail
{
	void ReleasePhysicsBindings(AActor* Owner)
	{
		for (int32 i = PhysicsBindings.Num() - 1; i >= 0; --i)
		{
			if (PhysicsBindings[i].Owner != Owner)
				continue;
			RemoveBinding(PhysicsBindings[i]);
			PhysicsBindings.RemoveAt(i);
		}
	}

	void RegisterPhysics(sol::state& Lua)
	{
		// ---------------- 스폰 ----------------
		Lua.set_function("SpawnMesh", [](sol::this_environment Env, const std::string& Mesh, const FVector& Location, sol::optional<FRotator> Rotation,
		                                 sol::optional<FVector> Scale, sol::optional<std::string> Name)
		                 {
			UWorld* World = WorldFromEnv(Env, "SpawnMesh");
			UStaticMesh* Asset = UAssetManager::GetAssetByPath<UStaticMesh>(Mesh);
			if (!Asset)
				throw sol::error("SpawnMesh: unknown static mesh " + Mesh);
			const FTransform Transform = MakeTransform(Location, Rotation, Scale);
			auto* Actor = World->SpawnActor<AStaticMeshActor>(Name ? FName(*Name) : NAME_None, &Transform);
			Actor->GetStaticMeshComponent()->SetStaticMesh(Asset);
			return FLuaActorRef::Make(Actor); });

		Lua.set_function("SpawnSkeletalMesh", [](sol::this_environment Env, const std::string& Mesh, const FVector& Location, sol::optional<FRotator> Rotation)
		                 {
			UWorld* World = WorldFromEnv(Env, "SpawnSkeletalMesh");
			USkeletalMesh* Asset = UAssetManager::GetAssetByPath<USkeletalMesh>(Mesh);
			if (!Asset)
				throw sol::error("SpawnSkeletalMesh: unknown skeletal mesh " + Mesh);
			const FTransform Transform = MakeTransform(Location, Rotation, sol::nullopt);
			auto* Actor = World->SpawnActor<ASkeletalMeshActor>(NAME_None, &Transform);
			Actor->GetSkeletalMeshComponent()->SetSkeletalMesh(Asset);
			return FLuaActorRef::Make(Actor); });

		Lua.set_function("SpawnText", [](sol::this_environment Env, const std::string& Text, const FVector& Location, sol::optional<float> Size)
		                 {
			UWorld* World = WorldFromEnv(Env, "SpawnText");
			const FTransform Transform = MakeTransform(Location, sol::nullopt, sol::nullopt);
			auto* Actor = World->SpawnActor<ATextRenderActor>(NAME_None, &Transform);
			Actor->GetTextRenderComponent()->SetText(Text);
			if (Size)
				Actor->GetTextRenderComponent()->SetTextSize(*Size);
			return FLuaActorRef::Make(Actor); });

		// 충돌 없는 Overlap 트리거 상자. Extent는 반 크기(cm).
		Lua.set_function("SpawnBoxTrigger", [](sol::this_environment Env, const FVector& Location, const FVector& Extent)
		                 {
			UWorld* World = WorldFromEnv(Env, "SpawnBoxTrigger");
			const FTransform Transform = MakeTransform(Location, sol::nullopt, sol::nullopt);
			auto* Actor = World->SpawnActor<AActor>(NAME_None, &Transform);
			auto* Box = Cast<UBoxComponent>(Actor->AddComponent(UBoxComponent::StaticClass(), NAME_None));
			Box->SetWorldLocation(Location, false);
			Box->BoxExtent = Extent;
			Box->PostEditChangeProperty(*Box->GetClass()->FindPropertyByName("BoxExtent"));
			return FLuaActorRef::Make(Actor); });

		// 두 액터의 바디를 관절로 잇는다. Parent가 nil이면 Child를 월드에 고정한다.
		// Rotation의 X축이 Twist 축이다. Options는 ApplyConstraintOptions 참고.
		Lua.set_function("SpawnConstraint", [](sol::this_environment Env, const FLuaActorRef& Child, sol::optional<FLuaActorRef> Parent,
		                                       const FVector& Location, sol::optional<FRotator> Rotation, sol::optional<sol::table> Options)
		                 {
			UWorld* World = WorldFromEnv(Env, "SpawnConstraint");
			const FRotator JointRotation = Rotation.value_or(FRotator(0, 0, 0));
			const FTransform Transform(JointRotation, Location, FVector(1, 1, 1));
			auto* Actor = World->SpawnActor<AActor>(NAME_None, &Transform);
			auto* Joint = Cast<UPhysicsConstraintComponent>(Actor->AddComponent(UPhysicsConstraintComponent::StaticClass(), NAME_None));
			Joint->SetWorldLocationAndRotation(Location, JointRotation, false);
			if (Options)
				ApplyConstraintOptions(Joint->ConstraintInstance.ProfileInstance, *Options);
			UPrimitiveComponent* ParentPrimitive = Parent && Parent->Get() ? &Primitive(*Parent) : nullptr;
			Joint->SetConstrainedComponents(&Primitive(Child), NAME_None, ParentPrimitive, NAME_None);
			return FLuaActorRef::Make(Actor); });

		// ---------------- 쿼리 · 월드 ----------------
		// 채널에 Block인 가장 가까운 바디. 맞으면 {Actor, ImpactPoint, ImpactNormal, Distance, BoneName}, 아니면 nil
		Lua.set_function("LineTrace", [](sol::this_environment Env, sol::this_state State, const FVector& Start, const FVector& End,
		                                 sol::optional<std::string> Channel) -> sol::object
		                 {
			UWorld* World = WorldFromEnv(Env, "LineTrace");
			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, Start, End, ToChannel(Channel.value_or("Visibility"))))
				return sol::lua_nil;
			sol::state_view Lua(State);
			sol::table Result = Lua.create_table();
			if (AActor* HitActor = Hit.GetActor())
				Result["Actor"] = FLuaActorRef::Make(HitActor);
			Result["ImpactPoint"] = Hit.ImpactPoint;
			Result["ImpactNormal"] = Hit.ImpactNormal;
			Result["Distance"] = Hit.Distance;
			Result["BoneName"] = Hit.BoneName.ToString();
			return Result; });

		// 축(정규화하지 않아도 된다)을 중심으로 Degrees만큼 돈 회전
		Lua.set_function("RotatorFromAxisAngle", [](const FVector& Axis, float Degrees)
		                 { return FQuat::MakeFromAxisAngle(Axis.GetSafeNormal(), Degrees * 3.14159265f / 180.0f).ToFRotator(); });

		Lua.set_function("SetTimeDilation", [](sol::this_environment Env, float Dilation)
		                 { WorldFromEnv(Env, "SetTimeDilation")->SetGlobalTimeDilation(Dilation); });

		// 플레이어 시점의 위치와 앞 방향
		Lua.set_function("GetPlayerView", [](sol::this_environment Env) -> std::tuple<sol::optional<FVector>, sol::optional<FVector>>
		                 {
			UWorld* World = WorldFromEnv(Env, "GetPlayerView");
			APlayerController* Controller = World->GetPlayerController();
			APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
			if (!Pawn || !Pawn->GetRootComponent())
				return {};
			FVector Origin = Pawn->GetActorLocation();
			FQuat View = Pawn->GetRootComponent()->GetWorldRotation().Quaternion();
			if (auto* Default = Cast<ADefaultPawn>(Pawn))
				if (UCameraComponent* Camera = Default->GetCameraComponent())
				{
					Origin = Camera->GetWorldLocation();
					View = Camera->GetWorldRotation().Quaternion();
				}
			return {Origin, View.RotateVector(FVector(1, 0, 0)).GetSafeNormal()}; });

		// 다른 Lua 파일을 지금 스크립트의 environment에서 실행한다 (공용 함수 모음용). 경로는 스크립트 경로 형식.
		Lua.set_function("Import", [](sol::this_environment ThisEnv, sol::this_state State, const std::string& Path)
		                 {
			if (!ThisEnv)
				throw sol::error("Import needs a script environment.");
			std::ifstream File(FLuaScriptManager::ToPath(FLuaScriptManager::NormalizeScriptPath(Path)), std::ios::binary);
			if (!File)
				throw sol::error("Import: cannot open " + Path);
			const std::string Code((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
			sol::state_view Lua(State);
			sol::environment& Env = ThisEnv;
			sol::protected_function_result Result = Lua.safe_script(Code, Env, sol::script_pass_on_error, "@" + Path);
			if (!Result.valid())
			{
				sol::error Err = Result;
				throw sol::error(FString("Import ") + Path + ": " + Err.what());
			} });

		// ---------------- 액터 메서드 ----------------
		sol::usertype<FLuaActorRef> ActorType = Lua["Actor"];

		ActorType["SetSimulatePhysics"] = [](const FLuaActorRef& Ref, bool bSimulate)
		{
			UPrimitiveComponent& C = Primitive(Ref);
			// 바디가 있으면 그대로 두고 키네마틱 여부만 바꾼다 (관절이 끊기지 않는다). 없으면 설정을 다시 적용한다.
			if (!C.GetBodyInstance().IsValidBodyInstance() || !C.GetBodyInstance().SetInstanceSimulatePhysics(bSimulate))
				C.SetSimulatePhysics(bSimulate);
		};
		ActorType["IsSimulatingPhysics"] = [](const FLuaActorRef& Ref)
		{ return Primitive(Ref).IsSimulatingPhysics(); };
		ActorType["SetMobility"] = [](const FLuaActorRef& Ref, const std::string& Mobility)
		{
			if (Mobility == "Static") Primitive(Ref).SetMobility(EComponentMobility::Static);
			else if (Mobility == "Movable") Primitive(Ref).SetMobility(EComponentMobility::Movable);
			else throw sol::error("Unknown mobility: " + Mobility);
		};
		ActorType["SetCollisionEnabled"] = [](const FLuaActorRef& Ref, const std::string& Mode)
		{ Primitive(Ref).SetCollisionEnabled(ToCollisionEnabled(Mode)); };
		ActorType["SetCollisionResponse"] = [](const FLuaActorRef& Ref, const std::string& Channel, const std::string& Response)
		{ Primitive(Ref).SetCollisionResponseToChannel(ToChannel(Channel), ToResponse(Response)); };
		ActorType["SetNotifyHit"] = [](const FLuaActorRef& Ref, bool bNotify)
		{ Primitive(Ref).SetNotifyRigidBodyCollision(bNotify); };
		ActorType["SetMass"] = [](const FLuaActorRef& Ref, float Kg)
		{ Primitive(Ref).SetMassOverrideInKg(Kg); };
		ActorType["SetGravityEnabled"] = [](const FLuaActorRef& Ref, bool bEnabled)
		{ Primitive(Ref).SetEnableGravity(bEnabled); };
		ActorType["SetLinearDamping"] = [](const FLuaActorRef& Ref, float Value)
		{ Primitive(Ref).SetLinearDamping(Value); };
		ActorType["SetAngularDamping"] = [](const FLuaActorRef& Ref, float Value)
		{ Primitive(Ref).SetAngularDamping(Value); };
		// 반복 횟수를 늘리면 무거운 사슬 같은 관절이 덜 늘어나고 덜 떤다.
		ActorType["SetSolverIterations"] = [](const FLuaActorRef& Ref, int Position, int Velocity)
		{
			FBodyInstance& Body = Primitive(Ref).GetBodyInstance();
			Body.PositionSolverIterationCount = static_cast<uint8>(std::clamp(Position, 1, 255));
			Body.VelocitySolverIterationCount = static_cast<uint8>(std::clamp(Velocity, 1, 255));
			Body.UpdateBodySettings();
		};
		// 이 바디 전용 물리 재질 (마찰·반발)
		ActorType["SetPhysicalMaterial"] = [](const FLuaActorRef& Ref, float Friction, float Restitution, sol::optional<float> StaticFriction)
		{
			UPrimitiveComponent& C = Primitive(Ref);
			auto* Material = NewObject<UPhysicalMaterial>(&C);
			Material->Friction = Friction;
			Material->StaticFriction = StaticFriction.value_or(Friction);
			Material->Restitution = Restitution;
			C.SetPhysMaterialOverride(Material);
		};
		ActorType["AddImpulse"] = [](const FLuaActorRef& Ref, const FVector& Impulse, sol::optional<bool> bVelChange)
		{ Primitive(Ref).AddImpulse(Impulse, bVelChange.value_or(false)); };
		ActorType["AddForce"] = [](const FLuaActorRef& Ref, const FVector& Force, sol::optional<bool> bAccelChange)
		{ Primitive(Ref).AddForce(Force, bAccelChange.value_or(false)); };
		ActorType["AddAngularImpulse"] = [](const FLuaActorRef& Ref, const FVector& ImpulseRadians, sol::optional<bool> bVelChange)
		{ Primitive(Ref).AddAngularImpulseInRadians(ImpulseRadians, bVelChange.value_or(false)); };
		ActorType["PhysicsVelocity"] = sol::property([](const FLuaActorRef& Ref)
		                                         { return Primitive(Ref).GetPhysicsLinearVelocity(); }, [](const FLuaActorRef& Ref, const FVector& V)
		                                         { Primitive(Ref).SetPhysicsLinearVelocity(V); });
		// 기본 재질의 인스턴스를 만들어 색만 바꾼다.
		ActorType["SetColor"] = [](const FLuaActorRef& Ref, float R, float G, float B, sol::optional<float> A)
		{
			UMaterial* Base = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
			UMaterial* Material = Base ? UMaterial::CreateInstance(Base) : nullptr;
			if (!Material)
				return;
			Material->BaseColor = FVector4(R, G, B, A.value_or(1.0f));
			Primitive(Ref).SetMaterial(0, Material);
		};

		// 글자
		ActorType["SetText"] = [](const FLuaActorRef& Ref, const std::string& Text)
		{ ComponentOf<UTextRenderComponent>(Ref, "TextRenderComponent").SetText(Text); };
		ActorType["SetTextSize"] = [](const FLuaActorRef& Ref, float Size)
		{ ComponentOf<UTextRenderComponent>(Ref, "TextRenderComponent").SetTextSize(Size); };

		// 스켈레탈 (래그돌)
		ActorType["PlayAnimation"] = [](const FLuaActorRef& Ref, const std::string& Path, sol::optional<bool> bLoop)
		{
			UAnimSequence* Sequence = UAssetManager::LoadAnimSequence(Path);
			if (!Sequence)
				throw sol::error("PlayAnimation: cannot load " + Path);
			ComponentOf<USkeletalMeshComponent>(Ref, "SkeletalMeshComponent").PlayAnimation(Sequence, bLoop.value_or(true));
		};
		ActorType["SetAllBodiesSimulatePhysics"] = [](const FLuaActorRef& Ref, bool bSimulate)
		{ ComponentOf<USkeletalMeshComponent>(Ref, "SkeletalMeshComponent").SetAllBodiesSimulatePhysics(bSimulate); };
		ActorType["SetAllBodiesBelowSimulatePhysics"] = [](const FLuaActorRef& Ref, const std::string& Bone, bool bSimulate)
		{ ComponentOf<USkeletalMeshComponent>(Ref, "SkeletalMeshComponent").SetAllBodiesBelowSimulatePhysics(FName(Bone), bSimulate); };
		ActorType["AddImpulseToBone"] = [](const FLuaActorRef& Ref, const FVector& Impulse, const std::string& Bone, sol::optional<bool> bVelChange)
		{ ComponentOf<USkeletalMeshComponent>(Ref, "SkeletalMeshComponent").AddImpulseToBone(Impulse, FName(Bone), bVelChange.value_or(false)); };
		ActorType["IsAnyBodySimulating"] = [](const FLuaActorRef& Ref)
		{ return ComponentOf<USkeletalMeshComponent>(Ref, "SkeletalMeshComponent").IsAnyBodySimulating(); };
		ActorType["NumBodies"] = sol::property([](const FLuaActorRef& Ref)
		                                   { return ComponentOf<USkeletalMeshComponent>(Ref, "SkeletalMeshComponent").Bodies.Num(); });
		ActorType["NumConstraints"] = sol::property([](const FLuaActorRef& Ref)
		                                        { return ComponentOf<USkeletalMeshComponent>(Ref, "SkeletalMeshComponent").Constraints.Num(); });

		// ---------------- 이벤트 ----------------
		// 콜백은 물리 처리 중에 불린다. 콜백 안에서 바디를 바꾸지 말고 값만 남겨 Tick에서 처리한다.
		// 콜백을 건 스크립트가 끝나거나 핫 리로드되면 자동으로 풀린다.
		ActorType["BindBeginOverlap"] = [](sol::this_environment Env, const FLuaActorRef& Ref, sol::protected_function Fn)
		{
			AActor* Owner = ScriptOwner(Env, "BindBeginOverlap");
			UPrimitiveComponent& C = Primitive(Ref);
			const FLuaActorRef OwnerRef = FLuaActorRef::Make(Owner);
			const FDelegateHandle Handle = C.OnComponentBeginOverlap.Add([OwnerRef, Fn](UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*)
			                                                             { InvokeLua(OwnerRef, Fn, "OnBeginOverlap", FLuaActorRef::Make(Other)); });
			PhysicsBindings.Add({Owner, FLuaComponentRef::Make(&C), FLuaPhysicsBinding::EKind::BeginOverlap, Handle});
		};
		ActorType["BindEndOverlap"] = [](sol::this_environment Env, const FLuaActorRef& Ref, sol::protected_function Fn)
		{
			AActor* Owner = ScriptOwner(Env, "BindEndOverlap");
			UPrimitiveComponent& C = Primitive(Ref);
			const FLuaActorRef OwnerRef = FLuaActorRef::Make(Owner);
			const FDelegateHandle Handle = C.OnComponentEndOverlap.Add([OwnerRef, Fn](UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*)
			                                                           { InvokeLua(OwnerRef, Fn, "OnEndOverlap", FLuaActorRef::Make(Other)); });
			PhysicsBindings.Add({Owner, FLuaComponentRef::Make(&C), FLuaPhysicsBinding::EKind::EndOverlap, Handle});
		};
		// Fn(Other, Hit): Hit = {ImpactPoint, ImpactNormal, BoneName(맞은 쪽 = 이 액터의 본), OtherVelocity}
		// 접촉이 이어지는 동안 매 프레임 온다 (UE와 같다). SetNotifyHit(true)가 필요하다.
		ActorType["BindHit"] = [](sol::this_environment Env, sol::this_state State, const FLuaActorRef& Ref, sol::protected_function Fn)
		{
			AActor* Owner = ScriptOwner(Env, "BindHit");
			UPrimitiveComponent& C = Primitive(Ref);
			const FLuaActorRef OwnerRef = FLuaActorRef::Make(Owner);
			lua_State* L = sol::main_thread(State, State);
			const FDelegateHandle Handle = C.OnComponentHit.Add(
			    [OwnerRef, Fn, L](UPrimitiveComponent*, AActor* Other, UPrimitiveComponent* OtherComp, FVector, const FHitResult& Hit)
			    {
				    if (!OwnerRef.Get())
					    return;
				    sol::state_view Lua(L);
				    sol::table Info = Lua.create_table();
				    Info["ImpactPoint"] = Hit.ImpactPoint;
				    Info["ImpactNormal"] = Hit.ImpactNormal;
				    Info["BoneName"] = Hit.MyBoneName.IsNone() ? FString() : Hit.MyBoneName.ToString();
				    Info["OtherVelocity"] = OtherComp ? OtherComp->GetPhysicsLinearVelocity() : FVector::ZeroVector;
				    InvokeLua(OwnerRef, Fn, "OnHit", FLuaActorRef::Make(Other), Info);
			    });
			PhysicsBindings.Add({Owner, FLuaComponentRef::Make(&C), FLuaPhysicsBinding::EKind::Hit, Handle});
		};
		// SpawnConstraint가 돌려준 관절 액터에 건다.
		ActorType["BindConstraintBroken"] = [](sol::this_environment Env, const FLuaActorRef& Ref, sol::protected_function Fn)
		{
			AActor* Owner = ScriptOwner(Env, "BindConstraintBroken");
			auto& Joint = ComponentOf<UPhysicsConstraintComponent>(Ref, "PhysicsConstraintComponent");
			const FLuaActorRef OwnerRef = FLuaActorRef::Make(Owner);
			const FDelegateHandle Handle = Joint.OnConstraintBroken.Add([OwnerRef, Fn](int32)
			                                                            { InvokeLua(OwnerRef, Fn, "OnConstraintBroken"); });
			PhysicsBindings.Add({Owner, FLuaComponentRef::Make(&Joint), FLuaPhysicsBinding::EKind::ConstraintBroken, Handle});
		};
	}
}
