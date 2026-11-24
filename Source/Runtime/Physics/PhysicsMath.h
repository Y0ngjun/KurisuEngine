#pragma once

#include "Math/Transform.h"
#include <foundation/PxTransform.h>

// 엔진과 PhysX 모두 cm, Z-up을 사용한다. 행렬 메모리 재해석 없이 성분을 전달한다.
namespace PhysicsMath
{
	inline physx::PxVec3 ToPx(const FVector& V)
	{
		return {V.X, V.Y, V.Z};
	}
	inline FVector FromPx(const physx::PxVec3& V)
	{
		return {V.x, V.y, V.z};
	}
	inline physx::PxQuat ToPx(const FQuat& Q)
	{
		return {Q.X, Q.Y, Q.Z, Q.W};
	}
	inline FQuat FromPx(const physx::PxQuat& Q)
	{
		return {Q.x, Q.y, Q.z, Q.w};
	}

	// 강체 pose에는 스케일이 없다. 스케일은 2단계의 충돌 형상 크기에 적용한다.
	// 입력 회전은 정규화된 쿼터니언이어야 한다.
	inline physx::PxTransform ToPxPose(const FTransform& Transform)
	{
		return {ToPx(Transform.Location), ToPx(Transform.Rotation)};
	}
	inline FTransform FromPxPose(const physx::PxTransform& Pose)
	{
		return FTransform(FromPx(Pose.q), FromPx(Pose.p));
	}
}
