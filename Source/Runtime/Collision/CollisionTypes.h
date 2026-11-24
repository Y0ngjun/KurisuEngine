#pragma once

#include "UObject/Class.h"
#include "Math/Vector.h"
#include "Containers/Array.h"
#include "CollisionTypes.generated.h"

class AActor;
class UPrimitiveComponent;

// UE EngineTypes.h의 ECollisionChannel. 바디의 Object Type이자 Trace 채널이다.
// 비트마스크(PxFilterData)에 담으므로 ECC_MAX는 32를 넘지 않는다.
UENUM()
enum class ECollisionChannel : uint8
{
	ECC_WorldStatic,
	ECC_WorldDynamic,
	ECC_Pawn,
	ECC_Visibility,
	ECC_Camera,
	ECC_PhysicsBody,
	ECC_Vehicle,
	ECC_Destructible,
	ECC_GameTraceChannel1,
	ECC_GameTraceChannel2,
	ECC_GameTraceChannel3,
	ECC_GameTraceChannel4,
	ECC_MAX UMETA(Hidden)
};
// UE 코드처럼 ECC_WorldStatic으로 바로 쓴다.
using enum ECollisionChannel;

UENUM()
enum class ECollisionResponse : uint8
{
	ECR_Ignore,
	ECR_Overlap,
	ECR_Block,
	ECR_MAX UMETA(Hidden)
};
using enum ECollisionResponse;

// 채널별 응답. UE처럼 채널마다 이름 있는 필드를 두어 리플렉션·저장·Details 편집을 그대로 쓴다.
USTRUCT()
struct FCollisionResponseContainer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	ECollisionResponse WorldStatic = ECR_Block;
	UPROPERTY(EditAnywhere)
	ECollisionResponse WorldDynamic = ECR_Block;
	UPROPERTY(EditAnywhere)
	ECollisionResponse Pawn = ECR_Block;
	UPROPERTY(EditAnywhere)
	ECollisionResponse Visibility = ECR_Block;
	UPROPERTY(EditAnywhere)
	ECollisionResponse Camera = ECR_Block;
	UPROPERTY(EditAnywhere)
	ECollisionResponse PhysicsBody = ECR_Block;
	UPROPERTY(EditAnywhere)
	ECollisionResponse Vehicle = ECR_Block;
	UPROPERTY(EditAnywhere)
	ECollisionResponse Destructible = ECR_Block;
	UPROPERTY(EditAnywhere)
	ECollisionResponse GameTraceChannel1 = ECR_Block;
	UPROPERTY(EditAnywhere)
	ECollisionResponse GameTraceChannel2 = ECR_Block;
	UPROPERTY(EditAnywhere)
	ECollisionResponse GameTraceChannel3 = ECR_Block;
	UPROPERTY(EditAnywhere)
	ECollisionResponse GameTraceChannel4 = ECR_Block;

	ECollisionResponse GetResponse(ECollisionChannel Channel) const
	{
		const ECollisionResponse* Response = const_cast<FCollisionResponseContainer*>(this)->Find(Channel);
		return Response ? *Response : ECR_Ignore;
	}
	// 값이 바뀌었으면 true
	bool SetResponse(ECollisionChannel Channel, ECollisionResponse NewResponse)
	{
		ECollisionResponse* Response = Find(Channel);
		if (!Response || *Response == NewResponse || NewResponse == ECR_MAX)
			return false;
		*Response = NewResponse;
		return true;
	}
	bool SetAllChannels(ECollisionResponse NewResponse)
	{
		bool bChanged = false;
		for (int32 Index = 0; Index < static_cast<int32>(ECC_MAX); ++Index)
			bChanged |= SetResponse(static_cast<ECollisionChannel>(Index), NewResponse);
		return bChanged;
	}
	bool operator==(const FCollisionResponseContainer& Other) const
	{
		for (int32 Index = 0; Index < static_cast<int32>(ECC_MAX); ++Index)
			if (GetResponse(static_cast<ECollisionChannel>(Index)) != Other.GetResponse(static_cast<ECollisionChannel>(Index)))
				return false;
		return true;
	}

private:
	ECollisionResponse* Find(ECollisionChannel Channel)
	{
		switch (Channel)
		{
		case ECC_WorldStatic: return &WorldStatic;
		case ECC_WorldDynamic: return &WorldDynamic;
		case ECC_Pawn: return &Pawn;
		case ECC_Visibility: return &Visibility;
		case ECC_Camera: return &Camera;
		case ECC_PhysicsBody: return &PhysicsBody;
		case ECC_Vehicle: return &Vehicle;
		case ECC_Destructible: return &Destructible;
		case ECC_GameTraceChannel1: return &GameTraceChannel1;
		case ECC_GameTraceChannel2: return &GameTraceChannel2;
		case ECC_GameTraceChannel3: return &GameTraceChannel3;
		case ECC_GameTraceChannel4: return &GameTraceChannel4;
		default: return nullptr;
		}
	}
};

// UE CollisionShape.h. Sweep/Overlap 쿼리에 쓰는 기본 도형. 크기는 월드 cm.
enum class ECollisionShape : uint8
{
	Line,
	Box,
	Sphere,
	Capsule
};

struct FCollisionShape
{
	ECollisionShape ShapeType = ECollisionShape::Line;
	FVector HalfExtent = FVector::ZeroVector; // Box
	float Radius = 0.0f;                      // Sphere, Capsule
	float HalfHeight = 0.0f;                  // Capsule: 반구를 포함한 절반 높이 (UE와 같다). 축은 로컬 Z

	static FCollisionShape MakeBox(const FVector& InHalfExtent)
	{
		FCollisionShape Shape;
		Shape.ShapeType = ECollisionShape::Box;
		Shape.HalfExtent = InHalfExtent;
		return Shape;
	}
	static FCollisionShape MakeSphere(float InRadius)
	{
		FCollisionShape Shape;
		Shape.ShapeType = ECollisionShape::Sphere;
		Shape.Radius = InRadius;
		return Shape;
	}
	static FCollisionShape MakeCapsule(float InRadius, float InHalfHeight)
	{
		FCollisionShape Shape;
		Shape.ShapeType = ECollisionShape::Capsule;
		Shape.Radius = InRadius;
		Shape.HalfHeight = InHalfHeight;
		return Shape;
	}
	bool IsLine() const { return ShapeType == ECollisionShape::Line; }
};

// UE CollisionQueryParams.h의 축약판. 무시할 액터·컴포넌트와 시작 겹침 보고 여부.
struct FCollisionQueryParams
{
	TArray<const AActor*> IgnoredActors;
	TArray<const UPrimitiveComponent*> IgnoredComponents;
	// Sweep이 시작 위치에서 이미 겹친 바디를 보고할지. 끄면 시작 겹침은 건너뛴다.
	bool bFindInitialOverlaps = true;

	FCollisionQueryParams() = default;
	explicit FCollisionQueryParams(const AActor* IgnoreActor)
	{
		AddIgnoredActor(IgnoreActor);
	}
	void AddIgnoredActor(const AActor* Actor)
	{
		if (Actor)
			IgnoredActors.Add(Actor);
	}
	void AddIgnoredComponent(const UPrimitiveComponent* Component)
	{
		if (Component)
			IgnoredComponents.Add(Component);
	}
};
