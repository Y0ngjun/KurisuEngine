#pragma once

#include "Math/Vector.h"
#include <memory>
#include <unordered_set>
#include <vector>

class FPhysicsSystem;
class FPhysXVehicleManager;
class UPrimitiveComponent;
struct FBodyInstance;
struct FConstraintInstance;
class UPhysicalMaterial;
namespace physx
{
	class PxScene;
	class PxPhysics;
	class PxMaterial;
}

// UWorld마다 하나씩 소유한다. SDK가 살아 있는 동안에만 사용하며 직렬화/복제하지 않는다.
// Tick은 엔진 메인 스레드에서 호출하고, 매 서브스텝의 결과를 동기적으로 회수한다.
class FPhysScene
{
public:
	FPhysScene();
	~FPhysScene();
	FPhysScene(const FPhysScene&) = delete;
	FPhysScene& operator=(const FPhysScene&) = delete;

	bool Init(FPhysicsSystem& PhysicsSystem);
	void Shutdown();
	// SDK/Scene을 유지하면서 플레이의 바디만 정리한다.
	void ClearBodies();
	bool IsInitialized() const;
	physx::PxScene* GetPhysXScene() const;
	physx::PxPhysics* GetPhysics() const;
	// UPhysicalMaterial마다 하나인 공유 PxMaterial을 돌려준다. 처음이면 만들고, 이미 있으면 현재 값(마찰, 반발)으로 맞춘다.
	// nullptr이면 물리 재질을 지정하지 않은 기본 재질이다. 소유권은 씬이 가지므로 호출자는 release하지 않는다.
	// 차량 마찰 표(8장)가 재질로 바닥 종류를 구분하려면 같은 재질이 같은 PxMaterial이어야 해서 공유한다.
	physx::PxMaterial* AcquireMaterial(const UPhysicalMaterial* Material);
	// 어떤 도형도 쓰지 않는 공유 재질을 해제한다 (Lua 등이 바디마다 만든 재질이 플레이를 반복해도 쌓이지 않게). ClearBodies가 부른다.
	void ReleaseUnusedMaterials();

	// 이 씬의 차량 관리자 (UE4 FPhysXVehicleManager::GetManagerFromScene 대응). 초기화 전에는 nullptr.
	FPhysXVehicleManager* GetVehicleManager() const { return VehicleManager.get(); }

	// 월드 시간 배율이 적용된 초 단위 시간. 실행한 서브스텝 수를 반환한다.
	int Tick(float DeltaTime);
	void ResetTime();

	static constexpr double FixedTimeStep = 1.0 / 60.0;
	// 엔진의 최대 프레임 시간(0.1s) x 최대 시간 배율(20)을 수용한다.
	// 그 이상의 밀린 시간은 버려 무제한 catch-up을 막는다.
	static constexpr int MaxSubsteps = 120;

	// 등록된 바디 (씬에서 잠시 뺀 바디 포함). 순회 중 바디를 만들거나 지우지 않는다.
	const std::unordered_set<FBodyInstance*>& GetBodies() const { return Bodies; }
	bool IsRegisteredBody(const FBodyInstance* Body) const { return Body && Bodies.contains(const_cast<FBodyInstance*>(Body)); }
	// 실행 중인 관절. 순회 중 관절을 만들거나 지우지 않는다.
	const std::unordered_set<FConstraintInstance*>& GetConstraints() const { return Constraints; }
	// Body에 연결된 관절을 모두 해제한다. 바디를 해제하기 전에 FBodyInstance가 부른다.
	void TermConstraintsAttachedTo(const FBodyInstance& Body);
	// 결과를 회수한 Tick마다 1씩 는다. 다중 바디 컴포넌트가 동기화를 Tick당 한 번만 하도록 쓴다.
	uint64 GetSimulationCount() const { return SimulationCount; }

	// PhysX 콜백이 fetchResults 안에서 쌓은 이벤트. Tick이 모든 서브스텝과 Transform 동기화를 마친 뒤
	// 컴포넌트로 전달한다. 전달 전에 해제된 바디의 이벤트는 버린다.
	struct FPendingHit
	{
		FBodyInstance* Body = nullptr;
		FBodyInstance* OtherBody = nullptr;
		FVector ImpactPoint = FVector::ZeroVector;
		FVector ImpactNormal = FVector::ZeroVector; // 상대 표면의 법선. Body 쪽을 향한다
		FVector NormalImpulse = FVector::ZeroVector; // Body가 받은 충격량 (kg*cm/s)
	};
	struct FPendingSleep
	{
		FBodyInstance* Body = nullptr;
		bool bAwake = false;
	};

private:
	friend struct FBodyInstance;
	friend struct FConstraintInstance;
	friend class FSimulationEventCallback;
	friend class FSimulationFilterCallback;
	void RegisterConstraint(FConstraintInstance& Constraint);
	void UnregisterConstraint(FConstraintInstance& Constraint);
	std::unordered_set<FConstraintInstance*> Constraints;
	std::vector<FConstraintInstance*> PendingBreaks;
	uint64 SimulationCount = 0;
	void RegisterBody(FBodyInstance& Body);
	void UnregisterBody(FBodyInstance& Body);
	void DispatchSimulationEvents();
	void ClearPendingEvents();
	std::vector<FPendingHit> PendingHits;
	std::vector<FPendingSleep> PendingSleeps;
	bool bDispatchingEvents = false;
	// 씬에서 일시적으로 제거한 바디도 추적하여 Shutdown 때 먼저 해제한다.
	std::unordered_set<FBodyInstance*> Bodies;
	struct FImpl;
	std::unique_ptr<FImpl> Impl;
	// 씬을 해제하기 전에 먼저 해제한다 (차량은 씬 안의 바디를 쓴다).
	std::unique_ptr<FPhysXVehicleManager> VehicleManager;
	double Accumulator = 0.0;
};
