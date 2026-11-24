#pragma once
#include "UObject/Class.h"
#include "Math/Transform.h"
#include "LegacyPhysicsSettings.generated.h"

// Read-only compatibility with scenes predating BodySetup.
UENUM()
enum class ELegacyPhysicsBodyType : uint8
{
	Static,
	Dynamic
};
UENUM()
enum class ELegacyPhysicsShapeType : uint8
{
	Box,
	Sphere
};

// 모든 치수는 스케일 적용 전 cm. 형상의 축은 강체 축과 같고 Center는 로컬 중심이다.
USTRUCT()
struct FLegacyPhysicsShapeSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere)
	ELegacyPhysicsShapeType Type = ELegacyPhysicsShapeType::Box;
	UPROPERTY(EditAnywhere)
	bool bFitMeshBounds = true; // 메시 컴포넌트에서만 사용. 직접 생성한 바디는 치수를 그대로 사용한다.
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.01", Delta = "0.1"))
	FVector BoxHalfExtent = FVector(50, 50, 50);
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.01", Delta = "0.1"))
	float SphereRadius = 50.0f;
	UPROPERTY(EditAnywhere, meta = (Delta = "0.1"))
	FVector Center = FVector(0, 0, 0);
};

USTRUCT()
struct FLegacyPhysicsMaterialSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0", Delta = "0.01"))
	float StaticFriction = 0.5f;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0", Delta = "0.01"))
	float DynamicFriction = 0.5f;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0", ClampMax = "1", Delta = "0.01"))
	float Restitution = 0.1f; // 일반 강체용 [0, 1]. 접촉 쌍은 평균으로 결합한다.
};

USTRUCT()
struct FLegacyBodySettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere)
	ELegacyPhysicsBodyType Type = ELegacyPhysicsBodyType::Dynamic;
	UPROPERTY(EditAnywhere)
	FLegacyPhysicsShapeSettings Shape;
	UPROPERTY(EditAnywhere)
	FLegacyPhysicsMaterialSettings Material;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.001", Delta = "0.1"))
	float MassKg = 1.0f; // Dynamic에만 적용. 형상을 기준으로 관성·질량 중심을 계산한다.

	// 파일·UI에서 유효하지 않은 값을 받으면 기본값/허용 범위로 복구한다. 변경 여부 반환.
};
