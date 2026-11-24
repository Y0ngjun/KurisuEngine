#include "EnginePCH.h"
#include "Components/PhysicsConstraintComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Physics/BodyInstance.h"
#include "Engine/World.h"
#include "UObject/UObjectHash.h"
#include "UObject/Casts.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"

namespace
{
	// 저장해 둔 포인터·클래스·UUID로 아직 살아 있는 같은 객체인지 확인한다 (LuaWidgetRef와 같은 방식).
	UPrimitiveComponent* ResolveLive(UPrimitiveComponent* Component, const UClass* Class, uint32 UUID)
	{
		uint32 Live = 0;
		return Component && TryGetLiveObjectUUID(Component, Class, Live) && Live == UUID ? Component : nullptr;
	}
}

UPrimitiveComponent* UPhysicsConstraintComponent::FindNamedComponent(const FString& ActorName, const FName& ComponentName) const
{
	UWorld* World = GetWorld();
	if (ActorName.empty() || !World)
		return nullptr;
	for (ULevel* Level : World->GetLevels())
	{
		if (!Level)
			continue;
		for (AActor* Actor : Level->GetActors())
		{
			if (!Actor || Actor->GetName() != ActorName)
				continue;
			if (ComponentName == NAME_None)
			{
				if (auto* Root = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
					return Root;
			}
			for (UActorComponent* Component : Actor->GetComponents())
				if (auto* Primitive = Cast<UPrimitiveComponent>(Component))
					if (ComponentName == NAME_None || FName(Primitive->GetName()) == ComponentName)
						return Primitive;
		}
	}
	return nullptr;
}

void UPhysicsConstraintComponent::ResolveNamedComponents()
{
	// 실행 중에 직접 정한 대상이 없을 때만 저장된 이름으로 찾는다 (PIE 복제 월드에서도 같은 이름으로 찾아진다).
	if (Component1 || ConstraintActor1.empty())
		return;
	UPrimitiveComponent* First = FindNamedComponent(ConstraintActor1, ComponentName1);
	UPrimitiveComponent* Second = FindNamedComponent(ConstraintActor2, ComponentName2);
	if (!First || (!ConstraintActor2.empty() && !Second))
		return;
	Component1 = First;
	Component2 = Second;
	ComponentUUID1 = First->GetUUID();
	ComponentUUID2 = Second ? Second->GetUUID() : 0;
	ComponentClass1 = First->GetClass();
	ComponentClass2 = Second ? Second->GetClass() : nullptr;
	BoneName1 = ConstraintInstance.ConstraintBone1;
	BoneName2 = ConstraintInstance.ConstraintBone2;
}

UPhysicsConstraintComponent::UPhysicsConstraintComponent()
{
	// 바디가 나중에 생기거나 다시 만들어지면 Tick에서 다시 잇는다.
	PrimaryComponentTick.bCanEverTick = true;
}

UPrimitiveComponent* UPhysicsConstraintComponent::GetConstrainedComponent1() const
{
	return ResolveLive(Component1, ComponentClass1, ComponentUUID1);
}

UPrimitiveComponent* UPhysicsConstraintComponent::GetConstrainedComponent2() const
{
	return ResolveLive(Component2, ComponentClass2, ComponentUUID2);
}

FBodyInstance* UPhysicsConstraintComponent::GetBody(UPrimitiveComponent* Component, const FName& BoneName) const
{
	if (!Component || !Component->IsRegistered())
		return nullptr;
	return Component->FindBodyInstance(BoneName);
}

void UPhysicsConstraintComponent::SetConstrainedComponents(UPrimitiveComponent* InComponent1, FName InBoneName1, UPrimitiveComponent* InComponent2, FName InBoneName2)
{
	TermComponentConstraint();
	Component1 = InComponent1;
	Component2 = InComponent2;
	ComponentUUID1 = InComponent1 ? InComponent1->GetUUID() : 0;
	ComponentUUID2 = InComponent2 ? InComponent2->GetUUID() : 0;
	ComponentClass1 = InComponent1 ? InComponent1->GetClass() : nullptr;
	ComponentClass2 = InComponent2 ? InComponent2->GetClass() : nullptr;
	BoneName1 = InBoneName1;
	BoneName2 = InBoneName2;
	ConstraintInstance.ConstraintBone1 = InBoneName1;
	ConstraintInstance.ConstraintBone2 = InBoneName2;
	bBroken = false;
	InitComponentConstraint();
}

bool UPhysicsConstraintComponent::InitComponentConstraint()
{
	if (bBroken)
		return false;
	ResolveNamedComponents();
	UPrimitiveComponent* First = GetConstrainedComponent1();
	UPrimitiveComponent* Second = GetConstrainedComponent2();
	FBodyInstance* Body1 = GetBody(First, BoneName1);
	FBodyInstance* Body2 = Second ? GetBody(Second, BoneName2) : nullptr;
	// 두 번째 컴포넌트를 정했으면 그 바디가 있어야 한다. 없으면 월드 고정과 구분할 수 없다.
	if (!Body1 || (Component2 && !Body2))
		return false;
	const FTransform JointWorld(GetWorldRotation().Quaternion(), GetWorldLocation());
	ConstraintInstance.SetRefFramesFromWorld(JointWorld, Body1, Body2);
	ConstraintInstance.OnConstraintBroken = [this](FConstraintInstance& Broken)
	{
		bBroken = true;
		OnConstraintBroken.Broadcast(Broken.ConstraintIndex);
	};
	// 프레임은 이미 월드 크기이므로 스케일 1로 잇는다.
	return ConstraintInstance.InitConstraint(Body1, Body2, 1.0f);
}

void UPhysicsConstraintComponent::TermComponentConstraint()
{
	ConstraintInstance.TermConstraint();
}

void UPhysicsConstraintComponent::BreakConstraint()
{
	bBroken = true;
	TermComponentConstraint();
}

void UPhysicsConstraintComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	// 바디가 다시 만들어지면 FPhysScene이 관절을 먼저 해제한다. 다음 Tick에 새 바디로 다시 잇는다.
	if (!ConstraintInstance.IsValidConstraintInstance() && !bBroken && ShouldCreatePhysicsState())
		InitComponentConstraint();
}

void UPhysicsConstraintComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	if (Property.Name == "ConstraintInstance")
		ConstraintInstance.UpdateConstraintSettings();
}

void UPhysicsConstraintComponent::OnUnregister()
{
	TermComponentConstraint();
	Super::OnUnregister();
}

bool UPhysicsConstraintComponent::ShouldCreatePhysicsState() const
{
	const UWorld* World = GetWorld();
	return World && World->IsGameWorld() && World->HasBegunPlay() && (Component1 || !ConstraintActor1.empty());
}

bool UPhysicsConstraintComponent::OnCreatePhysicsState()
{
	return InitComponentConstraint();
}

void UPhysicsConstraintComponent::OnDestroyPhysicsState()
{
	TermComponentConstraint();
}
