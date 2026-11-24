#pragma once

#include "Core/Types.h"
#include "Physics/ConstraintInstance.h"

class UPhysicsAsset;
class USkeletalMesh;

// UE EPhysAssetFitGeomType. 본마다 만들 형상 (순서도 UE와 같다). Convex는 추후 추가.
enum class EPhysAssetFitGeomType : uint8
{
	EFG_Box,
	EFG_Sphyl,	// Capsule
	EFG_Sphere,
};

// UE FPhysAssetCreateParams의 축약판
struct FPhysAssetCreateParams
{
	// 본마다 만들 형상 (UE 기본 Capsule)
	EPhysAssetFitGeomType GeomType = EPhysAssetFitGeomType::EFG_Sphyl;
	// 본에 가중치가 실린 정점 범위의 가장 긴 변이 이보다 짧은 본은 바디를 만들지 않고 부모 바디에 합친다 (cm).
	// UE 기본값 20cm
	float MinBoneSize = 20.0f;
	// 캡슐 축을 뼈 방향(본 → 자식 본)에 맞춘다 (UE bAutoOrientToBone, 기본 켬).
	// 끄면 정점 범위 상자의 가장 긴 변을 축으로 쓴다. 자식이 없는 끝 본은 켜도 상자 축을 쓴다.
	bool bAutoOrientToBone = true;
	// 생성되는 관절의 기본 각도 한계 (도). UE처럼 생성 창에서는 받지 않고 FConstraintInstance 기본값(45)을 쓴다.
	float SwingLimitDegrees = 45.0f;
	float TwistLimitDegrees = 45.0f;
	bool bCreateConstraints = true;
	// 생성되는 관절의 Swing1·Swing2·Twist 모드 (UE AngularConstraintMode, 기본 Limited)
	EAngularConstraintMotion AngularConstraintMode = ACM_Limited;
	// 바인드 포즈에서 서로 겹치는 바디끼리 충돌을 끈다 (겹친 채 시작하면 래그돌이 튄다).
	bool bDisableCollisionsOfOverlappingBodies = true;
};

namespace FPhysicsAssetUtils
{
	// UE FPhysicsAssetUtils::CreateFromSkeletalMesh. 기존 내용을 지우고 본마다 캡슐 바디와 부모 바디로의 D6 관절을 만든다.
	// 바디 형상은 본 공간에서 그 본이 가장 크게 영향을 주는 정점들의 범위에 맞춘다. 실패하면 false.
	bool CreateFromSkeletalMesh(UPhysicsAsset& PhysicsAsset, const USkeletalMesh& Mesh, const FPhysAssetCreateParams& Params = FPhysAssetCreateParams());
}
