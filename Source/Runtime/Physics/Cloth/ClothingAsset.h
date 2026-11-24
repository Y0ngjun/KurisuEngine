#pragma once

#include "Render/SkeletalMeshData.h"
#include "UObject/Class.h"
#include "ClothingAsset.generated.h"

struct FClothingLODData
{
	int32 SourceRenderLOD = -1;
	int32 SourceSectionIndex = -1;
	FString SourceSectionSignature;

	// 컴포넌트 공간 기준 포즈. 세 배열은 시뮬레이션 정점 번호를 공유한다.
	TArray<FVector> RestPositions;
	TArray<FSkinWeight> SkinWeights;
	TArray<float> MaxDistances;

	// 3개씩 한 삼각형. 값은 시뮬레이션 정점 번호다.
	TArray<uint32> TriangleIndices;

	// LOD 전체 렌더 정점 번호로 조회한다. 섹션 밖 정점은 -1.
	TArray<int32> RenderToSimVertex;
};

USTRUCT()
struct FClothConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, meta = (Delta = "1"))
	float GravityZ = -980.0f;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "1", ClampMax = "1000", Delta = "1"))
	float SolverFrequency = 120.0f;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0", ClampMax = "1", Delta = "0.01"))
	FVector Damping = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0", ClampMax = "1", Delta = "0.01"))
	float Stiffness = 1.0f; // 강성
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0", ClampMax = "1", Delta = "0.01"))
	float Friction = 0.0f;
	UPROPERTY(EditAnywhere)
	bool bEnableContinuousCollision = false;
};

struct FClothingAsset
{
	FString Name;
	TArray<FClothingLODData> LODs;

	FClothConfig Config;
	bool bUsePhysicsAssetCollision = false;
	FString CollisionPhysicsAssetPath;
	FString Id;
};

struct FClothingSectionBinding
{
	int32 RenderLOD = -1;
	int32 SectionIndex = -1;
	int32 ClothingAssetIndex = -1;
	int32 ClothLODIndex = -1;
};
