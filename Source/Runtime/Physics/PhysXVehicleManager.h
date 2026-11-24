#pragma once

#include <cstddef>
#include <memory>
#include <vector>

class FPhysScene;
class UWheeledVehicleMovementComponent;
namespace physx
{
	class PxMaterial;
}

// UE4 FPhysXVehicleManager 대응. FPhysScene마다 하나씩 소유하며 씬 안의 모든 차량을 모아서 한꺼번에 갱신한다.
// 차량 컴포넌트는 물리 상태를 만들 때 Add, 없앨 때 Remove한다 (UE4 UWheeledVehicleMovementComponent::OnCreatePhysicsState/OnDestroyPhysicsState).
// 엔진 메인 스레드에서만 사용한다.
//
// 맡는 일:
//  - 서스펜션 레이캐스트를 모든 차의 모든 바퀴에 대해 배치 쿼리로 한 번에 실행 (6.2)
//  - PxVehicleUpdates로 모든 차를 한 번에 갱신하고 결과(바퀴 상태)를 각 차량 컴포넌트에 돌려줌 (6.3)
//  - 표면별 타이어 마찰 표(PxVehicleDrivableSurfaceToTireFrictionPairs) 보관 (6.4)
class FPhysXVehicleManager
{
public:
	explicit FPhysXVehicleManager(FPhysScene& InScene);
	~FPhysXVehicleManager();
	FPhysXVehicleManager(const FPhysXVehicleManager&) = delete;
	FPhysXVehicleManager& operator=(const FPhysXVehicleManager&) = delete;

	// nullptr이거나 이미 등록된 차량은 무시한다.
	void AddVehicle(UWheeledVehicleMovementComponent* Vehicle);
	// 등록되지 않은 차량은 무시한다. 차량 컴포넌트가 파괴되기 전에 반드시 불러야 한다.
	void RemoveVehicle(UWheeledVehicleMovementComponent* Vehicle);

	// ---- 표면 마찰 표 (8장) ----
	// 물리 재질을 지정하지 않은 기본 표면(표면 타입 0)과 그 타이어 마찰. 등록되지 않은 모든 재질은 이 마찰을 쓴다.
	void SetDefaultSurface(physx::PxMaterial* Material, float TireFriction);
	// 이 재질(물리 재질당 하나인 공유 PxMaterial)을 밟은 타이어의 마찰. 같은 재질을 다시 부르면 값을 바꾼다.
	// 표는 다음 Update 때 다시 만든다.
	void SetSurfaceTireFriction(physx::PxMaterial* Material, float TireFriction);
	// 해제될 재질을 표에서 뺀다. 씬이 재질을 해제하기 전에 부른다.
	void RemoveSurface(physx::PxMaterial* Material);

	// 물리 서브스텝 하나만큼 모든 차량을 갱신한다. PxScene::simulate를 부르기 직전에, 시뮬레이션 중이 아닐 때 부른다.
	// 서스펜션 레이캐스트로 바닥을 찾고 → 타이어/서스펜션/엔진 힘을 계산해 섀시 바디의 속도에 반영한다.
	void Update(float DeltaTime);

	size_t GetNumVehicles() const { return Vehicles.size(); }
	const std::vector<UWheeledVehicleMovementComponent*>& GetVehicles() const { return Vehicles; }
	FPhysScene& GetScene() const { return Scene; }

private:
	// 서스펜션 배치 쿼리가 한 번에 받을 수 있는 바퀴 수를 TotalWheels 이상으로 맞춘다. 모자라면 다시 만든다.
	bool EnsureBatchQueryCapacity(size_t TotalWheels);
	// 등록된 표면(기본 표면 + 물리 재질들)으로 타이어 마찰 표를 다시 만든다.
	void RebuildFrictionTable();

	FPhysScene& Scene; // 비소유. 씬이 이 관리자를 소유하므로 항상 씬보다 먼저 사라진다.
	std::vector<UWheeledVehicleMovementComponent*> Vehicles;

	struct FImpl; // PhysX 객체(배치 쿼리, 결과 버퍼, 마찰 표). 헤더에 PhysX를 노출하지 않는다.
	std::unique_ptr<FImpl> Impl;
};
