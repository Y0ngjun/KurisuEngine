#include "EnginePCH.h"
#include "ShapeComponent.h"
#include "Render/LineBatcher.h"
#include "UObject/UObjectIterator.h"
#include "Physics/BodySetup.h"

UShapeComponent::UShapeComponent()
{
	bCastShadow = false;
	bReceiveShadow = false;
	// UE 도형 기본 프리셋 OverlapAllDynamic: 쿼리만, 모든 채널 Overlap. 시뮬레이션하지 않는 키네마틱 바디.
	BodyInstance.SetCollisionProfileName(FName("OverlapAllDynamic"));
	BodyInstance.bSimulatePhysics = false;
	BodyInstance.bEnableGravity = false;
}

UBodySetup* UShapeComponent::GetBodySetup() const
{
	if (!ShapeBodySetup)
		ShapeBodySetup = NewObject<UBodySetup>(const_cast<UShapeComponent*>(this));
	if (!ShapeBodySetup)
		return nullptr;
	// 도형 하나뿐이라 매번 현재 크기로 다시 채운다. 실행 중인 바디는 생성 시점 형상을 유지한다.
	ShapeBodySetup->AggGeom.EmptyElements();
	UpdateBodySetup(*ShapeBodySetup);
	return ShapeBodySetup;
}

FTransform UShapeComponent::GetPhysicsBodyTransform() const
{
	// 스케일은 형상 크기에 이미 반영했다.
	return FTransform(GetWorldRotation().Quaternion(), GetWorldLocation());
}

void UShapeComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	if (IsShapeSizeProperty(Property.Name) && GetBodyInstance().IsValidBodyInstance())
		RecreatePhysicsState();
}

FBox UShapeComponent::GetWorldAABB() const
{
	// 자식이 CalcLocalBounds를 채우면 월드 변환은 SceneComponent가 처리한다
	return CalcBounds();
}

FVector4 UShapeComponent::GetDebugColor() const
{
	if (!GetOverlapInfos().IsEmpty())
		return FVector4(1.0f, 0.25f, 0.25f, 1.0f);
	return FVector4(ShapeColor.R / 255.0f, ShapeColor.G / 255.0f, ShapeColor.B / 255.0f, 1.0f);
}

void UShapeComponent::AddWireArc(FLineBatcher* LineBatcher, const FVector& Center, const FVector& AxisA, const FVector& AxisB,
                                 float Radius, float StartRad, float EndRad, const FVector4& Color, int32 Segments)
{
	auto PointAt = [&](float T)
	{
		return Center + AxisA * (std::cos(T) * Radius) + AxisB * (std::sin(T) * Radius);
	};
	FVector Prev = PointAt(StartRad);
	for (int32 i = 1; i <= Segments; ++i)
	{
		const FVector P = PointAt(StartRad + (EndRad - StartRad) * i / Segments);
		LineBatcher->AddLine(Prev, P, Color);
		Prev = P;
	}
}

void DrawCollisionShapes(UWorld* World, FLineBatcher* LineBatcher, const AActor* Selected)
{
	if (!World || !LineBatcher)
		return;
	for (TObjectIterator<UShapeComponent> It; It; ++It)
	{
		UShapeComponent* Shape = *It;
		if (!Shape || Shape->GetWorld() != World || !Shape->IsRegistered())
			continue;
		if (Shape->bDrawOnlyIfSelected && Shape->GetOwner() != Selected)
			continue;
		Shape->DrawDebug(LineBatcher, Shape->GetDebugColor());
	}
}