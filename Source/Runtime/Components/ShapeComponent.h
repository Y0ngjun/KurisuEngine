#pragma once

#include "PrimitiveComponent.h"
#include "Render/LineBatcher.h"
#include "ShapeComponent.generated.h"

class FLineBatcher;

// World의 모든 도형을 그린다. Selected가 있으면 bDrawOnlyIfSelected인 도형은 그 액터 것만
void DrawCollisionShapes(UWorld* World, FLineBatcher* LineBatcher, const AActor* Selected);

// 충돌 판정용 도형의 공통 부모. 메시가 없고 LineBatcher로 와이어프레임만 그린다. (담당: D)
UCLASS()
class UShapeComponent : public UPrimitiveComponent
{
	GENERATED_BODY()
public:
	UShapeComponent();

	// 넓은 범위 판정용. 월드 스케일과 회전이 반영된 AABB
	virtual FBox GetWorldAABB() const;

	// 월드 공간 와이어프레임. 색은 0~1 범위 (캐스케이드 디버그 선과 같은 규약)
	virtual void DrawDebug(FLineBatcher* LineBatcher, const FVector4& Color) const {}

	// 평소 ShapeColor, 겹쳐 있으면 빨강
	FVector4 GetDebugColor() const;

	UPROPERTY(EditAnywhere)
	FColor ShapeColor = FColor(110, 200, 120, 255);
	UPROPERTY(EditAnywhere)
	bool bDrawOnlyIfSelected = false;

	// UE UShapeComponent::ShapeBodySetup. 컴포넌트마다 하나인 일시 BodySetup으로, 저장하지 않는다.
	// 도형 크기는 월드 스케일을 반영해 굽고(GetScaled*), 바디 Transform에는 스케일을 넣지 않는다.
	virtual UBodySetup* GetBodySetup() const override;
	virtual void PostEditChangeProperty(const FProperty& Property) override;

protected:
	// 자식이 이 도형 하나를 Setup.AggGeom에 채운다. 크기는 월드 스케일이 반영된 값.
	virtual void UpdateBodySetup(UBodySetup& Setup) const {}
	// 도형 크기를 바꾸는 속성인지. 바뀌면 바디를 다시 만든다.
	virtual bool IsShapeSizeProperty(const FString& Name) const { return false; }
	virtual FTransform GetPhysicsBodyTransform() const override;
	virtual bool BakesScaleIntoBodySetup() const override { return true; }
	virtual bool ShouldCreateRenderState() const override { return false; }

	// 중심, 원이 놓일 평면의 두 축, 반지름, 시작~끝 각도(라디안)
	static void AddWireArc(FLineBatcher* LineBatcher, const FVector& Center, const FVector& AxisA, const FVector& AxisB,
	                       float Radius, float StartRad, float EndRad, const FVector4& Color, int32 Segments = 24);

private:
	mutable UBodySetup* ShapeBodySetup = nullptr;
};
