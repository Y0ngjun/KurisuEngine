#pragma once

#include "ShapeComponent.h"
#include "CapsuleComponent.generated.h"

// 로컬 Z축이 캡슐 축이다. HalfHeight는 반구 끝까지 포함한 길이 (UE와 같음).
UCLASS()
class UCapsuleComponent : public UShapeComponent
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere)
	float CapsuleHalfHeight = 100.0f;
	UPROPERTY(EditAnywhere)
	float CapsuleRadius = 50.0f;

	float GetScaledCapsuleHalfHeight() const;
	float GetScaledCapsuleRadius() const;
	virtual FBox CalcLocalBounds() const override;
	virtual void DrawDebug(FLineBatcher*, const FVector4&) const override;

protected:
	virtual void UpdateBodySetup(UBodySetup& Setup) const override;
	virtual bool IsShapeSizeProperty(const FString& Name) const override { return Name == "CapsuleHalfHeight" || Name == "CapsuleRadius"; }
};
