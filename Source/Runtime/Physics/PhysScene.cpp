#include "EnginePCH.h"
#include "Physics/PhysScene.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/BodyInstance.h"
#include "Physics/BodySetup.h"
#include "Physics/ConstraintInstance.h"
#include "Physics/PhysicsFilter.h"
#include "Physics/PhysicsMath.h"
#include "Physics/PhysicsStats.h"
#include "Physics/PhysXVehicleManager.h"
#include "Physics/PhysicalMaterial.h"
#include "Components/PrimitiveComponent.h"

#include <PxPhysicsAPI.h>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

// fetchResults 안(엔진 메인 스레드)에서 호출된다. 여기서는 엔진 객체를 건드리지 않고 이벤트만 쌓는다.
class FSimulationEventCallback final : public physx::PxSimulationEventCallback
{
public:
	explicit FSimulationEventCallback(FPhysScene& InScene)
	    : Scene(InScene)
	{
	}

	void onContact(const physx::PxContactPairHeader& Header, const physx::PxContactPair* Pairs, physx::PxU32 Count) override
	{
		SCOPE_CYCLE_COUNTER(PhysicsStats::STAT_PhysContactCallback);
		PhysicsStats::GCounters.ContactPairs.Current += Count;
		if (Header.flags & (physx::PxContactPairHeaderFlag::eREMOVED_ACTOR_0 | physx::PxContactPairHeaderFlag::eREMOVED_ACTOR_1))
			return;
		auto* Body0 = static_cast<FBodyInstance*>(Header.actors[0]->userData);
		auto* Body1 = static_cast<FBodyInstance*>(Header.actors[1]->userData);
		if (Body0 == Body1 || !Scene.IsRegisteredBody(Body0) || !Scene.IsRegisteredBody(Body1))
			return;
		for (physx::PxU32 PairIndex = 0; PairIndex < Count; ++PairIndex)
		{
			const physx::PxContactPair& Pair = Pairs[PairIndex];
			if (Pair.flags & (physx::PxContactPairFlag::eREMOVED_SHAPE_0 | physx::PxContactPairFlag::eREMOVED_SHAPE_1))
				continue;
			if (!(Pair.events & (physx::PxPairFlag::eNOTIFY_TOUCH_FOUND | physx::PxPairFlag::eNOTIFY_TOUCH_PERSISTS)))
				continue;
			physx::PxContactPairPoint Points[16];
			const physx::PxU32 PointCount = Pair.extractContacts(Points, 16);
			PhysicsStats::GCounters.ContactPoints.Current += PointCount;
			physx::PxVec3 Position(0.0f), Impulse(0.0f), Normal(0.0f);
			for (physx::PxU32 Index = 0; Index < PointCount; ++Index)
			{
				Position += Points[Index].position;
				Impulse += Points[Index].impulse;
			}
			if (PointCount > 0)
			{
				Position /= static_cast<float>(PointCount);
				// PhysX 법선은 shape0이 shape1에서 빠져나가는 방향이다.
				Normal = Points[0].normal;
			}
			const FVector Point = PhysicsMath::FromPx(Position);
			Add(Body0, Body1, Point, PhysicsMath::FromPx(Normal), PhysicsMath::FromPx(Impulse));
			Add(Body1, Body0, Point, PhysicsMath::FromPx(-Normal), PhysicsMath::FromPx(-Impulse));
		}
	}
	void onWake(physx::PxActor** Actors, physx::PxU32 Count) override { QueueSleep(Actors, Count, true); }
	void onSleep(physx::PxActor** Actors, physx::PxU32 Count) override { QueueSleep(Actors, Count, false); }
	// 엔진은 트리거 형상을 쓰지 않는다. Overlap 이벤트는 UE처럼 Scene Query(UWorld::UpdatePhysicsOverlaps)로 만든다.
	void onTrigger(physx::PxTriggerPair*, physx::PxU32) override {}
	// 파괴된 관절은 PhysX가 eBROKEN으로 표시만 하고 남겨 둔다. 소유자에게 알리고 해제는 소유자가 한다.
	void onConstraintBreak(physx::PxConstraintInfo* Infos, physx::PxU32 Count) override
	{
		for (physx::PxU32 Index = 0; Index < Count; ++Index)
		{
			if (Infos[Index].type != physx::PxConstraintExtIDs::eJOINT || !Infos[Index].externalReference)
				continue;
			auto* Joint = static_cast<physx::PxJoint*>(Infos[Index].externalReference);
			auto* Constraint = static_cast<FConstraintInstance*>(Joint->userData);
			if (Constraint && Scene.Constraints.contains(Constraint))
				Scene.PendingBreaks.push_back(Constraint);
		}
	}
	void onAdvance(const physx::PxRigidBody* const*, const physx::PxTransform*, const physx::PxU32) override {}

private:
	void Add(FBodyInstance* Body, FBodyInstance* Other, const FVector& Point, const FVector& Normal, const FVector& Impulse)
	{
		if (!Body->bNotifyRigidBodyCollision)
			return;
		// 서브스텝마다 보고되는 같은 쌍은 프레임당 한 번으로 합치고 충격량은 더한다.
		for (FPhysScene::FPendingHit& Pending : Scene.PendingHits)
		{
			if (Pending.Body == Body && Pending.OtherBody == Other)
			{
				Pending.NormalImpulse += Impulse;
				return;
			}
		}
		Scene.PendingHits.push_back({Body, Other, Point, Normal, Impulse});
	}
	void QueueSleep(physx::PxActor** Actors, physx::PxU32 Count, bool bAwake)
	{
		for (physx::PxU32 Index = 0; Index < Count; ++Index)
		{
			auto* Body = static_cast<FBodyInstance*>(Actors[Index]->userData);
			if (Scene.IsRegisteredBody(Body))
				Scene.PendingSleeps.push_back({Body, bAwake});
		}
	}

	FPhysScene& Scene;
};

// 같은 다중 바디 컴포넌트의 두 바디가 처음 만날 때(필터 셰이더가 eCALLBACK) 불린다.
// 소유 컴포넌트의 CollisionDisableTable로 충돌 여부를 정한다 (UE PhysicsAsset의 충돌 끄기 표).
class FSimulationFilterCallback final : public physx::PxSimulationFilterCallback
{
public:
	physx::PxFilterFlags pairFound(physx::PxU32, physx::PxFilterObjectAttributes, physx::PxFilterData, const physx::PxActor* Actor0, const physx::PxShape*,
	                               physx::PxFilterObjectAttributes, physx::PxFilterData, const physx::PxActor* Actor1, const physx::PxShape*,
	                               physx::PxPairFlags&) override
	{
		const auto* Body0 = static_cast<const FBodyInstance*>(Actor0->userData);
		const auto* Body1 = static_cast<const FBodyInstance*>(Actor1->userData);
		if (!Body0 || !Body1 || Body0->GetOwnerComponent() != Body1->GetOwnerComponent() || !Body0->GetOwnerComponent())
			return physx::PxFilterFlag::eDEFAULT;
		return Body0->GetOwnerComponent()->IsBodyPairCollisionEnabled(Body0->InstanceBodyIndex, Body1->InstanceBodyIndex)
		           ? physx::PxFilterFlags(physx::PxFilterFlag::eDEFAULT)
		           : physx::PxFilterFlags(physx::PxFilterFlag::eSUPPRESS);
	}
	void pairLost(physx::PxU32, physx::PxFilterObjectAttributes, physx::PxFilterData, physx::PxFilterObjectAttributes, physx::PxFilterData, bool) override {}
	bool statusChange(physx::PxU32&, physx::PxPairFlags&, physx::PxFilterFlags&) override { return false; }
};

struct FPhysScene::FImpl
{
	physx::PxPhysics* Physics = nullptr; // 비소유. 씬보다 SDK가 오래 살아 있어야 한다.
	physx::PxDefaultCpuDispatcher* Dispatcher = nullptr;
	physx::PxScene* Scene = nullptr;
	std::unique_ptr<FSimulationEventCallback> EventCallback;
	FSimulationFilterCallback FilterCallback;

	// UPhysicalMaterial마다 하나씩 공유하는 PxMaterial (키가 nullptr이면 물리 재질을 지정하지 않은 기본 재질).
	// 씬이 참조 하나를 들고 있고, 도형은 자기 참조를 따로 가진다. 키는 값을 비교하는 용도로만 쓰고 역참조하지 않는다.
	struct FSharedMaterial
	{
		physx::PxMaterial* Native = nullptr;
		float TireFrictionScale = 1.0f;
	};
	std::unordered_map<const UPhysicalMaterial*, FSharedMaterial> Materials;
};

FPhysScene::FPhysScene() = default;

FPhysScene::~FPhysScene()
{
	Shutdown();
}

bool FPhysScene::Init(FPhysicsSystem& PhysicsSystem)
{
	if (IsInitialized())
		return true;
	physx::PxPhysics* Physics = PhysicsSystem.GetPhysics();
	if (!Physics)
	{
		KRS_LOG(Error, "[Physics] Cannot create a scene before the SDK is initialized.");
		return false;
	}

	Impl = std::make_unique<FImpl>();
	Impl->Physics = Physics;
	Impl->Dispatcher = physx::PxDefaultCpuDispatcherCreate(1);
	if (!Impl->Dispatcher)
	{
		KRS_LOG(Error, "[Physics] Failed to create a CPU dispatcher.");
		Shutdown();
		return false;
	}

	physx::PxSceneDesc Desc(Physics->getTolerancesScale());
	Desc.gravity = physx::PxVec3(0.0f, 0.0f, -WorldGravity);
	Desc.cpuDispatcher = Impl->Dispatcher;
	Impl->EventCallback = std::make_unique<FSimulationEventCallback>(*this);
	Desc.filterShader = PhysicsFilter::SimulationFilterShader;
	Desc.simulationEventCallback = Impl->EventCallback.get();
	Desc.filterCallback = &Impl->FilterCallback;
	Impl->Scene = Physics->createScene(Desc);
	if (!Impl->Scene)
	{
		KRS_LOG(Error, "[Physics] Failed to create a world physics scene.");
		Shutdown();
		return false;
	}
	VehicleManager = std::make_unique<FPhysXVehicleManager>(*this);
	// 기본 재질은 차량 마찰 표의 표면 타입 0이다. 물리 재질을 지정하지 않은 바닥은 모두 이것으로 취급한다.
	AcquireMaterial(nullptr);
	ResetTime();
	return true;
}

physx::PxMaterial* FPhysScene::AcquireMaterial(const UPhysicalMaterial* Material)
{
	if (!Impl || !Impl->Physics)
		return nullptr;
	const float StaticFriction = Material ? Material->StaticFriction : 0.5f;
	const float DynamicFriction = Material ? Material->Friction : 0.5f;
	const float Restitution = Material ? Material->Restitution : 0.1f;
	const float TireFrictionScale = Material ? Material->TireFrictionScale : 1.0f;

	bool bChanged = false;
	auto It = Impl->Materials.find(Material);
	if (It == Impl->Materials.end())
	{
		physx::PxMaterial* Native = Impl->Physics->createMaterial(StaticFriction, DynamicFriction, Restitution);
		if (!Native)
			return nullptr;
		It = Impl->Materials.emplace(Material, FImpl::FSharedMaterial{Native, TireFrictionScale}).first;
		bChanged = true;
	}
	else
	{
		// 같은 재질을 쓰는 모든 도형이 바로 새 값을 쓰도록 공유 재질의 값을 현재 값으로 맞춘다 (에디터에서 값을 바꾼 경우).
		physx::PxMaterial* Native = It->second.Native;
		if (Native->getStaticFriction() != StaticFriction)
			Native->setStaticFriction(StaticFriction);
		if (Native->getDynamicFriction() != DynamicFriction)
			Native->setDynamicFriction(DynamicFriction);
		if (Native->getRestitution() != Restitution)
			Native->setRestitution(Restitution);
		if (It->second.TireFrictionScale != TireFrictionScale)
		{
			It->second.TireFrictionScale = TireFrictionScale;
			bChanged = true;
		}
	}

	if (bChanged && VehicleManager)
	{
		if (Material)
			VehicleManager->SetSurfaceTireFriction(It->second.Native, TireFrictionScale);
		else
			VehicleManager->SetDefaultSurface(It->second.Native, TireFrictionScale);
	}
	return It->second.Native;
}

void FPhysScene::ReleaseUnusedMaterials()
{
	if (!Impl)
		return;
	for (auto It = Impl->Materials.begin(); It != Impl->Materials.end();)
	{
		physx::PxMaterial* Native = It->second.Native;
		// 참조가 씬 하나뿐이면 쓰는 도형이 없다.
		if (Native && Native->getReferenceCount() <= 1)
		{
			if (VehicleManager)
				VehicleManager->RemoveSurface(Native);
			Native->release();
			It = Impl->Materials.erase(It);
		}
		else
			++It;
	}
	// 기본 재질은 차량 마찰 표의 표면 0번이라 항상 있어야 한다.
	if (!Impl->Materials.contains(nullptr))
		AcquireMaterial(nullptr);
}

void FPhysScene::Shutdown()
{
	ResetTime();
	if (!Impl)
		return;
	ClearBodies();
	// 차량 관리자는 씬을 해제하기 전에 정리한다. 관리자의 마찰 표가 공유 재질을 가리키므로 재질보다 먼저 없앤다.
	VehicleManager.reset();
	for (auto& [Key, Shared] : Impl->Materials)
	{
		if (Shared.Native)
			Shared.Native->release();
	}
	Impl->Materials.clear();
	// Tick은 fetchResults까지 끝내므로 여기에는 진행 중인 시뮬레이션이 없다.
	if (Impl->Scene)
		Impl->Scene->release();
	if (Impl->Dispatcher)
		Impl->Dispatcher->release();
	Impl.reset();
	ClearPendingEvents();
}

void FPhysScene::ClearBodies()
{
	// 관절을 바디보다 먼저 해제한다. TermConstraint가 집합에서 자신을 제거한다.
	while (!Constraints.empty())
		(*Constraints.begin())->TermConstraint();
	// TermBody가 집합에서 자신을 제거한다. 외부의 FBodyInstance는 무효 상태로 남는다.
	while (!Bodies.empty())
	{
		FBodyInstance* Body = *Bodies.begin();
		if (UPrimitiveComponent* Component = Body->GetOwnerComponent())
			Component->DestroyPhysicsState();
		// Also handles unowned or incompletely constructed bodies.
		if (Bodies.contains(Body))
			Body->TermBody();
	}
	ClearPendingEvents();
	ResetTime();
	// 바디가 모두 없어졌으니 쓰는 도형이 없는 공유 재질을 정리한다.
	ReleaseUnusedMaterials();
}

bool FPhysScene::IsInitialized() const
{
	return Impl && Impl->Scene;
}

physx::PxScene* FPhysScene::GetPhysXScene() const
{
	return IsInitialized() ? Impl->Scene : nullptr;
}

physx::PxPhysics* FPhysScene::GetPhysics() const
{
	return IsInitialized() ? Impl->Physics : nullptr;
}

int FPhysScene::Tick(float DeltaTime)
{
	if (!IsInitialized() || !std::isfinite(DeltaTime) || DeltaTime <= 0.0f)
		return 0;
	Accumulator = std::min(Accumulator + static_cast<double>(DeltaTime), FixedTimeStep * MaxSubsteps);
	// 누적/차감의 부동소수점 오차 때문에 정확히 한 스텝이 모자라는 경우만 보정한다.
	const int Steps = std::min(static_cast<int>((Accumulator + 1e-9) / FixedTimeStep), MaxSubsteps);
	PhysicsStats::GCounters.Substeps.Current += static_cast<uint64>(Steps);
	PhysicsStats::GCounters.Bodies.Current += Bodies.size();
	{
		SCOPE_CYCLE_COUNTER(PhysicsStats::STAT_PhysPrepareForces);
		for (FBodyInstance* Body : Bodies)
		{
			Body->PrepareForces(static_cast<float>(std::min(static_cast<double>(DeltaTime), FixedTimeStep * MaxSubsteps)), Steps);
			if (Body->IsInstanceSimulatingPhysics())
				++PhysicsStats::GCounters.SimulatingBodies.Current;
		}
	}
	for (int Step = 0; Step < Steps; ++Step)
	{
		for (FBodyInstance* Body : Bodies)
			Body->ApplyPendingForces();
		// 차량은 서브스텝마다 갱신한다 (UE4와 같다). 서스펜션 레이캐스트는 시뮬레이션 중이 아닐 때만 쏠 수 있어서
		// simulate 직전에 부른다.
		if (VehicleManager)
			VehicleManager->Update(static_cast<float>(FixedTimeStep));
		// onContact는 fetchResults 안에서 불리므로 Contact Callback 시간은 이 값에 포함된다.
		SCOPE_CYCLE_COUNTER(PhysicsStats::STAT_PhysSimulateFetch);
		Impl->Scene->simulate(static_cast<float>(FixedTimeStep));
		if (!Impl->Scene->fetchResults(true))
		{
			KRS_LOG(Error, "[Physics] Failed to fetch world simulation results.");
			for (FBodyInstance* Body : Bodies)
				Body->ClearPendingForces();
			ClearPendingEvents();
			ResetTime();
			return Step;
		}
	}
	Accumulator = std::max(0.0, Accumulator - Steps * FixedTimeStep);
	if (Steps > 0)
	{
		for (FBodyInstance* Body : Bodies)
			Body->ClearPendingForces();
		// 다중 바디 컴포넌트는 SimulationCount를 보고 Tick당 한 번만 본 포즈를 갱신한다.
		++SimulationCount;
		{
			SCOPE_CYCLE_COUNTER(PhysicsStats::STAT_PhysSyncTransforms);
			// Transform 변경 중 등록 해제가 발생해도 순회자가 무효화되지 않게 스냅샷을 사용한다.
			const std::vector<FBodyInstance*> Snapshot(Bodies.begin(), Bodies.end());
			for (FBodyInstance* Body : Snapshot)
			{
				if (!Bodies.contains(Body) || !Body->IsInScene())
					continue;
				if (UPrimitiveComponent* Component = Body->GetOwnerComponent())
				{
					if (Body->IsInstanceSimulatingPhysics())
						++PhysicsStats::GCounters.SyncedComponents.Current;
					Component->SyncComponentToRBPhysics();
				}
			}
		}
		// 결과 회수와 Transform 동기화가 끝난 안전한 시점에 이벤트를 전달한다.
		SCOPE_CYCLE_COUNTER(PhysicsStats::STAT_PhysDispatchEvents);
		DispatchSimulationEvents();
	}
	return Steps;
}

void FPhysScene::DispatchSimulationEvents()
{
	if (bDispatchingEvents)
		return;
	bDispatchingEvents = true;
	// 콜백이 바디를 지우면 UnregisterBody가 그 바디의 남은 항목을 비운다. 그래서 인덱스로 순회한다.
	for (size_t Index = 0; Index < PendingHits.size(); ++Index)
	{
		const FPendingHit Pending = PendingHits[Index];
		if (!Pending.Body || !Pending.OtherBody)
			continue;
		UPrimitiveComponent* Component = Pending.Body->GetOwnerComponent();
		UPrimitiveComponent* OtherComponent = Pending.OtherBody->GetOwnerComponent();
		if (!Component || !OtherComponent || !Component->IsRegistered() || !OtherComponent->IsRegistered())
			continue;
		FHitResult Hit;
		Hit.bBlockingHit = true;
		Hit.Time = 0.0f;
		Hit.Distance = 0.0f;
		Hit.HitComponent = OtherComponent;
		if (const UBodySetup* OtherSetup = Pending.OtherBody->GetBodySetup())
			Hit.BoneName = OtherSetup->BoneName;
		if (const UBodySetup* MySetup = Pending.Body->GetBodySetup())
			Hit.MyBoneName = MySetup->BoneName;
		Hit.Location = Hit.ImpactPoint = Pending.ImpactPoint;
		Hit.Normal = Hit.ImpactNormal = Pending.ImpactNormal;
		++PhysicsStats::GCounters.HitEvents.Current;
		Component->DispatchHit(OtherComponent, Pending.NormalImpulse, Hit);
	}
	for (size_t Index = 0; Index < PendingSleeps.size(); ++Index)
	{
		const FPendingSleep Pending = PendingSleeps[Index];
		if (!Pending.Body)
			continue;
		if (UPrimitiveComponent* Component = Pending.Body->GetOwnerComponent(); Component && Component->IsRegistered())
		{
			const UBodySetup* Setup = Pending.Body->GetBodySetup();
			const FName BoneName = Setup ? Setup->BoneName : FName();
			if (Pending.bAwake)
				Component->OnComponentWake.Broadcast(Component, BoneName);
			else
				Component->OnComponentSleep.Broadcast(Component, BoneName);
		}
	}
	for (size_t Index = 0; Index < PendingBreaks.size(); ++Index)
	{
		FConstraintInstance* Constraint = PendingBreaks[Index];
		if (Constraint && Constraint->OnConstraintBroken)
			Constraint->OnConstraintBroken(*Constraint);
	}
	ClearPendingEvents();
	bDispatchingEvents = false;
}

void FPhysScene::RegisterConstraint(FConstraintInstance& Constraint)
{
	Constraints.insert(&Constraint);
}

void FPhysScene::UnregisterConstraint(FConstraintInstance& Constraint)
{
	Constraints.erase(&Constraint);
	for (FConstraintInstance*& Pending : PendingBreaks)
		if (Pending == &Constraint)
			Pending = nullptr;
}

void FPhysScene::TermConstraintsAttachedTo(const FBodyInstance& Body)
{
	std::vector<FConstraintInstance*> Attached;
	for (FConstraintInstance* Constraint : Constraints)
		if (Constraint->GetBody1() == &Body || Constraint->GetBody2() == &Body)
			Attached.push_back(Constraint);
	for (FConstraintInstance* Constraint : Attached)
		Constraint->TermConstraint();
}

void FPhysScene::ClearPendingEvents()
{
	PendingHits.clear();
	PendingSleeps.clear();
	PendingBreaks.clear();
}

void FPhysScene::ResetTime()
{
	Accumulator = 0.0;
}

void FPhysScene::RegisterBody(FBodyInstance& Body)
{
	Bodies.insert(&Body);
}

void FPhysScene::UnregisterBody(FBodyInstance& Body)
{
	Bodies.erase(&Body);
	// 전달 전(또는 전달 중) 해제된 바디의 이벤트가 끊어진 포인터를 쓰지 않게 한다.
	for (FPendingHit& Pending : PendingHits)
		if (Pending.Body == &Body || Pending.OtherBody == &Body)
			Pending.Body = Pending.OtherBody = nullptr;
	for (FPendingSleep& Pending : PendingSleeps)
		if (Pending.Body == &Body)
			Pending.Body = nullptr;
}
