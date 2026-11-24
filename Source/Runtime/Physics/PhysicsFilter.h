#pragma once

#include "Collision/CollisionTypes.h"
#include <PxFiltering.h>

struct FBodyInstance;

// 충돌 채널·응답을 PhysX 필터 데이터로 옮기는 규칙 (UE4 PhysXSupport의 CreateShapeFilterData 대응).
// 형상의 Simulation/Query 필터 데이터는 같은 배치를 쓴다.
//   word0: 이 바디의 Object Type 비트 (1 << ObjectType)
//   word1: Block 응답인 채널 마스크
//   word2: Overlap 응답인 채널 마스크
//   word3: EFilterFlags (하위 8비트) | SelfCollisionGroup (상위 24비트)
namespace PhysicsFilter
{
	enum EFilterFlags : uint32
	{
		NotifyRigidBodyCollision = 1u << 0,
	};
	// word3의 상위 24비트: 같은 다중 바디 컴포넌트를 묶는 번호 (FBodyInstance::SelfCollisionGroup)
	constexpr uint32 SelfCollisionGroupShift = 8;
	constexpr uint32 SelfCollisionGroupMask = 0x00FFFFFFu;
	inline uint32 GetSelfCollisionGroup(const physx::PxFilterData& Data) { return Data.word3 >> SelfCollisionGroupShift; }

	physx::PxFilterData MakeFilterData(const FBodyInstance& Body);

	// 필터 데이터가 가진 응답 중 Channel에 대한 응답
	ECollisionResponse GetResponse(const physx::PxFilterData& Data, ECollisionChannel Channel);
	// 두 바디의 상호 응답: 서로에 대한 응답 중 약한 쪽 (UE와 같다)
	ECollisionResponse GetMutualResponse(const physx::PxFilterData& A, const physx::PxFilterData& B);

	// 상호 Block만 접촉을 풀고, 그 외는 쌍을 억제한다. Overlap 이벤트는 시뮬레이션이 아니라 Scene Query로 만든다 (UE와 같다).
	physx::PxFilterFlags SimulationFilterShader(physx::PxFilterObjectAttributes Attributes0, physx::PxFilterData FilterData0,
	                                            physx::PxFilterObjectAttributes Attributes1, physx::PxFilterData FilterData1,
	                                            physx::PxPairFlags& PairFlags, const void* ConstantBlock, physx::PxU32 ConstantBlockSize);
}
