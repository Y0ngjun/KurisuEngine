#include "EnginePCH.h"
#include "Physics/PhysicsFilter.h"
#include "Physics/BodyInstance.h"

#include <PxPhysicsAPI.h>
#include <algorithm>

static_assert(static_cast<int32>(ECC_MAX) <= 32, "Collision channels are stored in 32-bit filter masks.");

namespace PhysicsFilter
{
	physx::PxFilterData MakeFilterData(const FBodyInstance& Body)
	{
		physx::PxFilterData Data;
		Data.word0 = 1u << static_cast<uint32>(Body.ObjectType);
		for (int32 Index = 0; Index < static_cast<int32>(ECC_MAX); ++Index)
		{
			const ECollisionResponse Response = Body.CollisionResponses.GetResponse(static_cast<ECollisionChannel>(Index));
			if (Response == ECR_Block)
				Data.word1 |= 1u << Index;
			else if (Response == ECR_Overlap)
				Data.word2 |= 1u << Index;
		}
		if (Body.bNotifyRigidBodyCollision)
			Data.word3 |= NotifyRigidBodyCollision;
		Data.word3 |= (Body.SelfCollisionGroup & SelfCollisionGroupMask) << SelfCollisionGroupShift;
		return Data;
	}

	ECollisionResponse GetResponse(const physx::PxFilterData& Data, ECollisionChannel Channel)
	{
		if (Channel >= ECC_MAX)
			return ECR_Ignore;
		const uint32 Bit = 1u << static_cast<uint32>(Channel);
		if (Data.word1 & Bit)
			return ECR_Block;
		if (Data.word2 & Bit)
			return ECR_Overlap;
		return ECR_Ignore;
	}

	static ECollisionResponse ResponseToObject(const physx::PxFilterData& Self, const physx::PxFilterData& Other)
	{
		if (Self.word1 & Other.word0)
			return ECR_Block;
		if (Self.word2 & Other.word0)
			return ECR_Overlap;
		return ECR_Ignore;
	}

	ECollisionResponse GetMutualResponse(const physx::PxFilterData& A, const physx::PxFilterData& B)
	{
		return std::min(ResponseToObject(A, B), ResponseToObject(B, A));
	}

	physx::PxFilterFlags SimulationFilterShader(physx::PxFilterObjectAttributes Attributes0, physx::PxFilterData FilterData0,
	                                            physx::PxFilterObjectAttributes Attributes1, physx::PxFilterData FilterData1,
	                                            physx::PxPairFlags& PairFlags, const void*, physx::PxU32)
	{
		// 엔진은 트리거 형상을 만들지 않는다. 외부에서 만든 트리거도 기본 동작으로 보고만 한다.
		if (physx::PxFilterObjectIsTrigger(Attributes0) || physx::PxFilterObjectIsTrigger(Attributes1))
		{
			PairFlags = physx::PxPairFlag::eTRIGGER_DEFAULT;
			return physx::PxFilterFlag::eDEFAULT;
		}
		// eSUPPRESS: 필터 데이터가 바뀌어 resetFiltering을 하면 다시 판정한다.
		if (GetMutualResponse(FilterData0, FilterData1) != ECR_Block)
			return physx::PxFilterFlag::eSUPPRESS;
		const uint32 Group0 = GetSelfCollisionGroup(FilterData0);
		PairFlags = physx::PxPairFlag::eCONTACT_DEFAULT;
		// UE처럼 접촉이 시작될 때와 유지되는 동안 Hit를 보고한다. 같은 쌍은 프레임당 한 번으로 합친다.
		if ((FilterData0.word3 | FilterData1.word3) & NotifyRigidBodyCollision)
			PairFlags |= physx::PxPairFlag::eNOTIFY_TOUCH_FOUND | physx::PxPairFlag::eNOTIFY_TOUCH_PERSISTS |
			             physx::PxPairFlag::eNOTIFY_CONTACT_POINTS;
		// 같은 스켈레탈 컴포넌트의 바디끼리는 PhysicsAsset의 CollisionDisableTable을 콜백에서 확인한다.
		if (Group0 != 0 && Group0 == GetSelfCollisionGroup(FilterData1))
			return physx::PxFilterFlag::eCALLBACK;
		return physx::PxFilterFlag::eDEFAULT;
	}
}
