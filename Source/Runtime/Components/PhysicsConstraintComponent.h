#pragma once

#include "SceneComponent.h"
#include "Physics/ConstraintInstance.h"
#include "Core/Delegate/Delegate.h"
#include "PhysicsConstraintComponent.generated.h"

class UPrimitiveComponent;
class UPhysicsConstraintComponent;

// UE FConstraintBrokenSignature: (부서진 관절 인덱스)
DECLARE_DELEGATE(FConstraintBrokenSignature, int32);

// UE UPhysicsConstraintComponent. 이 컴포넌트의 월드 Transform이 관절 위치·방향이다 (X = Twist 축).
// 두 컴포넌트(스켈레탈이면 본까지)의 바디를 잇고, 두 번째가 없으면 첫 번째를 월드에 고정한다.
// 연결할 컴포넌트는 실행 중에 SetConstrainedComponents로 정한다. 저장하지 않는다.
UCLASS()
class UPhysicsConstraintComponent : public USceneComponent
{
	GENERATED_BODY()
public:
	UPhysicsConstraintComponent();

	UPROPERTY(EditAnywhere)
	FConstraintInstance ConstraintInstance;

	// 저장되는 연결 대상 (UE ConstraintActor1/ComponentName1). 같은 레벨의 액터 이름과, 그 안의 컴포넌트 이름(비우면
	// 루트 또는 첫 Primitive). 두 번째 액터를 비우면 첫 번째를 월드에 고정한다. 본 이름은 ConstraintInstance의
	// ConstraintBone1/2를 쓴다. SetConstrainedComponents로 정한 실행 중 대상이 있으면 그것이 우선한다.
	UPROPERTY(EditAnywhere)
	FString ConstraintActor1;
	UPROPERTY(EditAnywhere)
	FName ComponentName1;
	UPROPERTY(EditAnywhere)
	FString ConstraintActor2;
	UPROPERTY(EditAnywhere)
	FName ComponentName2;

	FConstraintBrokenSignature OnConstraintBroken;

	// UE SetConstrainedComponents. 바디가 준비되어 있으면 바로 연결하고, 아니면 바디가 생긴 뒤 연결한다.
	void SetConstrainedComponents(UPrimitiveComponent* Component1, FName BoneName1, UPrimitiveComponent* Component2, FName BoneName2);
	// 현재 Transform으로 프레임을 다시 잡아 연결한다. 두 바디가 없으면 false.
	bool InitComponentConstraint();
	void TermComponentConstraint();
	// 연결을 끊고 다시 잇지 않는다 (UE BreakConstraint)
	void BreakConstraint();
	bool IsBroken() const { return bBroken || ConstraintInstance.IsBroken(); }

	// 지워진 컴포넌트는 nullptr (UUID로 살아 있는지 확인한다)
	UPrimitiveComponent* GetConstrainedComponent1() const;
	UPrimitiveComponent* GetConstrainedComponent2() const;

	// UE 이름의 한계 설정
	void SetLinearXLimit(ELinearConstraintMotion Motion, float Limit) { ConstraintInstance.SetLinearXLimit(Motion, Limit); }
	void SetLinearYLimit(ELinearConstraintMotion Motion, float Limit) { ConstraintInstance.SetLinearYLimit(Motion, Limit); }
	void SetLinearZLimit(ELinearConstraintMotion Motion, float Limit) { ConstraintInstance.SetLinearZLimit(Motion, Limit); }
	void SetAngularSwing1Limit(EAngularConstraintMotion Motion, float Degrees) { ConstraintInstance.SetAngularSwing1Limit(Motion, Degrees); }
	void SetAngularSwing2Limit(EAngularConstraintMotion Motion, float Degrees) { ConstraintInstance.SetAngularSwing2Limit(Motion, Degrees); }
	void SetAngularTwistLimit(EAngularConstraintMotion Motion, float Degrees) { ConstraintInstance.SetAngularTwistLimit(Motion, Degrees); }
	void SetDisableCollision(bool bDisable) { ConstraintInstance.SetDisableCollision(bDisable); }

	virtual void TickComponent(float DeltaTime) override;
	virtual void PostEditChangeProperty(const FProperty& Property) override;

protected:
	virtual void OnUnregister() override;
	virtual bool ShouldCreatePhysicsState() const override;
	virtual bool OnCreatePhysicsState() override;
	virtual void OnDestroyPhysicsState() override;

private:
	FBodyInstance* GetBody(UPrimitiveComponent* Component, const FName& BoneName) const;
	UPrimitiveComponent* FindNamedComponent(const FString& ActorName, const FName& ComponentName) const;
	void ResolveNamedComponents();
	UPrimitiveComponent* Component1 = nullptr;
	UPrimitiveComponent* Component2 = nullptr;
	uint32 ComponentUUID1 = 0;
	uint32 ComponentUUID2 = 0;
	const UClass* ComponentClass1 = nullptr;
	const UClass* ComponentClass2 = nullptr;
	FName BoneName1;
	FName BoneName2;
	bool bBroken = false;
};
