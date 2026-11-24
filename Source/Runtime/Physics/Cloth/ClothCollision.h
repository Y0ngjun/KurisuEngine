#pragma once

#include "Containers/Array.h"
#include "Math/Matrix.h"
#include "Math/Vector4.h"

class UPhysicsAsset;
class USkeletalMeshComponent;
namespace physx
{
	class PxPhysics;
}

inline constexpr int32 MaxClothSpheres = 32;
inline constexpr int32 MaxClothPlanes = 32;

struct FClothBoneCollisionElement
{
	int32 BoneIndex;
	FVector4 Value;
};

// 충돌 등록 시 구성하는 데이터. 구와 평면은 각각 연결된 본의 로컬 공간에 저장한다.
struct FClothCollisionBinding
{
	// Sphere : (Center.X, Center.Y, Center.Z, Radius)
	TArray<FClothBoneCollisionElement> LocalSpheres;

	// Plane : (Normal.X, Normal.Y, Normal.Z, D), dot(Normal, P) + D = 0
	TArray<FClothBoneCollisionElement> LocalPlanes;

	// 두 원소가 캡슐 하나. 각 값은 LocalSpheres의 슬롯 번호.
	TArray<uint32> CapsuleIndices;
	// 한 원소가 볼록체 하나. i번 비트는 LocalPlanes[i]를 가리킨다.
	TArray<uint32> ConvexMasks;

	int32 NumSkippedShapes = 0;
};

// 최신 본 포즈로 천의 시뮬레이션 공간에 변환한 NvCloth 입력.
// 캡슐 인덱스와 볼록체 마스크를 재사용할 수 있도록 바인딩의 슬롯 순서를 유지한다.
struct FClothCollisionFrame
{
	TArray<FVector4> Spheres;
	TArray<FVector4> Planes;
};

FClothCollisionBinding BuildClothCollisionBinding(
    const USkeletalMeshComponent& Component,
    const UPhysicsAsset& Asset,
    physx::PxPhysics* Physics);

bool BuildClothCollisionFrame(
    const FClothCollisionBinding& Binding,
    const TArray<FMatrix>& BoneToComponent,
    FClothCollisionFrame& OutFrame);
