#pragma once

#include "Core/Types.h"
#include "Core/Stats/LightweightStats.h"

// 물리 프레임 비용을 나눠 재는 스탯 ("stat physics", "stat profile"에서 본다).
// 시간은 FStatRegistry가 프레임 단위로 합친다. 서브스텝이 여러 번 돌면 그 합이 한 프레임 값이다.
namespace PhysicsStats
{
	// UWorld::Tick의 FPhysScene::Tick 전체 (아래 구간 중 Update Overlaps를 뺀 나머지를 포함)
	DECLARE_CYCLE_STAT("Physics Simulation", STAT_PhysicsSimulation);
	DECLARE_CYCLE_STAT("Phys Prepare Forces", STAT_PhysPrepareForces);
	DECLARE_CYCLE_STAT("Phys Simulate+Fetch", STAT_PhysSimulateFetch);
	DECLARE_CYCLE_STAT("Phys Contact Callback", STAT_PhysContactCallback);
	DECLARE_CYCLE_STAT("Phys Sync Transforms", STAT_PhysSyncTransforms);
	DECLARE_CYCLE_STAT("Phys Dispatch Events", STAT_PhysDispatchEvents);
	DECLARE_CYCLE_STAT("Phys Update Overlaps", STAT_PhysUpdateOverlaps);

	// 프레임마다 세는 수. Last는 직전 프레임, Max는 최근 120 프레임 중 최댓값.
	struct FCounter
	{
		static constexpr uint32 Duration = 120;
		uint64 Current = 0;
		uint64 Last = 0;
		uint64 History[Duration] = {};
		uint32 NextIndex = 0;

		void EndFrame()
		{
			Last = Current;
			History[NextIndex] = Current;
			NextIndex = (NextIndex + 1) % Duration;
			Current = 0;
		}
		uint64 GetMax() const
		{
			uint64 Max = 0;
			for (uint64 Value : History)
				Max = Value > Max ? Value : Max;
			return Max;
		}
	};

	struct FCounters
	{
		FCounter Substeps;          // 이번 프레임 simulate 횟수
		FCounter Bodies;            // 등록된 바디
		FCounter SimulatingBodies;  // 그중 시뮬레이션 중인 바디
		FCounter SyncedComponents;  // 결과를 컴포넌트로 되돌린 바디
		FCounter ContactPairs;      // onContact가 받은 접촉 쌍 (서브스텝 합)
		FCounter ContactPoints;     // extractContacts로 꺼낸 접촉점 (서브스텝 합)
		FCounter HitEvents;         // DispatchHit 호출 수
		FCounter OverlapBodies;     // Overlap 쿼리를 낸 바디
		FCounter OverlapQueries;    // PxScene::overlap 호출 수
		FCounter OverlapTouches;    // 쿼리가 돌려준 접촉 수 (필터 통과 전)
		FCounter OverlapPairs;      // 컴포넌트 쌍으로 합친 결과

		void EndFrame()
		{
			for (FCounter* Counter : {&Substeps, &Bodies, &SimulatingBodies, &SyncedComponents, &ContactPairs, &ContactPoints, &HitEvents,
			                          &OverlapBodies, &OverlapQueries, &OverlapTouches, &OverlapPairs})
				Counter->EndFrame();
		}
	};

	inline FCounters GCounters;
}
