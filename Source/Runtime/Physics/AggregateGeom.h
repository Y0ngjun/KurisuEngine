#pragma once

#include "UObject/Class.h"
#include "Math/Transform.h"
#include "Physics/BodyInstance.h"
#include "AggregateGeom.generated.h"

USTRUCT()
struct FKShapeElem
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere)
	FName Name;
	UPROPERTY(EditAnywhere)
	bool bContributeToMass = true;
	UPROPERTY(EditAnywhere)
	ECollisionEnabled CollisionEnabled = ECollisionEnabled::QueryAndPhysics;
};

USTRUCT()
struct FKBoxElem : public FKShapeElem
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere)
	FVector Center = FVector::ZeroVector;
	UPROPERTY(EditAnywhere)
	FRotator Rotation;
	// Full lengths in local-space cm, as in Unreal's FKBoxElem.
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.01"))
	float X = 100.0f;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.01"))
	float Y = 100.0f;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.01"))
	float Z = 100.0f;
};

USTRUCT()
struct FKSphereElem : public FKShapeElem
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere)
	FVector Center = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.01"))
	float Radius = 50.0f;
};

USTRUCT()
struct FKSphylElem : public FKShapeElem
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere)
    FVector Center = FVector::ZeroVector;
    UPROPERTY(EditAnywhere)
    FRotator Rotation;
    UPROPERTY(EditAnywhere, meta=(ClampMin="0.01"))
    float Radius = 50.0f;
    // Cylinder length, excluding the hemispheres. The engine capsule axis is local Z.
    UPROPERTY(EditAnywhere, meta=(ClampMin="0"))
    float Length = 100.0f;
};

USTRUCT()
struct FKConvexElem : public FKShapeElem
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere)
    TArray<FVector> VertexData;
    UPROPERTY(EditAnywhere)
    FTransform Transform;
};

USTRUCT()
struct FKAggregateGeom
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere)
	TArray<FKBoxElem> BoxElems;
	UPROPERTY(EditAnywhere)
	TArray<FKSphereElem> SphereElems;
	UPROPERTY(EditAnywhere)
    TArray<FKSphylElem> SphylElems;
    UPROPERTY(EditAnywhere)
    TArray<FKConvexElem> ConvexElems;
    int32 GetElementCount() const { return BoxElems.Num() + SphereElems.Num() + SphylElems.Num() + ConvexElems.Num(); }
    void EmptyElements() { BoxElems.Empty(); SphereElems.Empty(); SphylElems.Empty(); ConvexElems.Empty(); }
};
