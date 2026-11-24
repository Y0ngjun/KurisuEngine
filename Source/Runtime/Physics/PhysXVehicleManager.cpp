#include "EnginePCH.h"
#include "Physics/PhysXVehicleManager.h"
#include "Physics/PhysScene.h"
#include "Components/WheeledVehicleMovementComponent.h"

#include <PxPhysicsAPI.h>
#include <algorithm>

// 배치 쿼리(PxBatchQuery)는 PhysX 3.4부터 deprecated지만, 4.1 차량 SDK의 서스펜션 레이캐스트가 요구하는 방식이다 (UE4도 같다).
#pragma warning(push)
#pragma warning(disable : 4996)

namespace
{
	using namespace physx;

	constexpr uint32 NumWheelsPerVehicle = 4; // 지금은 4륜차뿐이다.
	// 타이어 종류는 하나뿐이다 (바퀴별 마찰 배율은 슬립 곡선 높이로 반영한다).
	constexpr uint32 NumTireTypes = 1;
	constexpr uint32 TireTypeIndex = 0;
	// 표면 타입(기본 표면 + 물리 재질) 최대 개수. PhysX 한계(eMAX_NB_SURFACE_TYPES)와 같다.
	constexpr uint32 MaxSurfaceTypes = PxVehicleDrivableSurfaceToTireFrictionPairs::eMAX_NB_SURFACE_TYPES;

	// 서스펜션 레이캐스트 사전 필터 (UE 트레이스 채널 규칙, 6.2).
	// 필터 데이터 배치는 Physics/PhysicsFilter.h를 따른다: 물체 word0 = 자기 종류 비트, word1 = 자기가 Block하는 채널 마스크.
	// 쿼리 word0 = 이 트레이스의 채널(Vehicle), word2 = 무시할 물체 종류 마스크 (WheeledVehicleMovementComponent가 정한다).
	PxQueryHitType::Enum SuspensionPreFilter(PxFilterData QueryFilterData, PxFilterData ObjectFilterData, const void* /*ConstantBlock*/, PxU32 /*ConstantBlockSize*/, PxHitFlags& /*HitFlags*/)
	{
		// 물체가 Vehicle 채널 트레이스를 막지 않으면 지나간다.
		if ((ObjectFilterData.word1 & QueryFilterData.word0) == 0)
			return PxQueryHitType::eNONE;
		// 다른 차량이나 폰 위로는 달리지 않는다.
		if ((ObjectFilterData.word0 & QueryFilterData.word2) != 0)
			return PxQueryHitType::eNONE;
		return PxQueryHitType::eBLOCK;
	}
}

struct FPhysXVehicleManager::FImpl
{
	// 표면 마찰 표. 표면 타입 0이 기본 표면이고, 그 뒤가 물리 재질 표면이다. 재질 소유는 씬이 가진다.
	struct FSurface
	{
		PxMaterial* Material = nullptr;
		float TireFriction = 1.0f;
	};
	FSurface DefaultSurface;
	std::vector<FSurface> Surfaces;
	PxVehicleDrivableSurfaceToTireFrictionPairs* FrictionPairs = nullptr;
	bool bFrictionTableDirty = true;

	PxBatchQuery* BatchQuery = nullptr;
	size_t BatchQueryCapacity = 0; // 한 번에 받을 수 있는 바퀴(= 레이캐스트) 수
	// 배치 쿼리가 결과를 쓰는 버퍼. 배치 쿼리가 살아 있는 동안 주소가 바뀌면 안 된다.
	std::vector<PxRaycastQueryResult> RaycastResults;
	std::vector<PxRaycastHit> RaycastTouchBuffer;

	// 매 갱신마다 다시 채우는 작업 버퍼
	std::vector<UWheeledVehicleMovementComponent*> ActiveComponents;
	std::vector<PxVehicleWheels*> ActiveVehicles;
	std::vector<PxWheelQueryResult> WheelQueryResults;
	std::vector<PxVehicleWheelQueryResult> VehicleWheelQueryResults;
};

FPhysXVehicleManager::FPhysXVehicleManager(FPhysScene& InScene)
    : Scene(InScene)
    , Impl(std::make_unique<FImpl>())
{
	PxPhysics* Physics = Scene.GetPhysics();
	if (!Physics)
	{
		KRS_LOG(Error, "[Vehicle] Vehicle manager: the physics SDK is not available.");
		return;
	}

	// 마찰 표는 한 번 할당해 두고, 표면이 등록되거나 값이 바뀔 때마다 Update에서 다시 채운다 (RebuildFrictionTable).
	Impl->FrictionPairs = PxVehicleDrivableSurfaceToTireFrictionPairs::allocate(NumTireTypes, MaxSurfaceTypes);
	if (!Impl->FrictionPairs)
		KRS_LOG(Error, "[Vehicle] Vehicle manager: failed to allocate the tire friction table.");
	KRS_LOG(Info, "[Vehicle] Vehicle manager created.");
}

FPhysXVehicleManager::~FPhysXVehicleManager()
{
	// 씬이 닫힐 때 차량이 남아 있으면 컴포넌트가 RemoveVehicle을 부르지 않은 것이다.
	if (!Vehicles.empty())
		KRS_LOG(Warning, "[Vehicle] Vehicle manager destroyed with {} vehicle(s) still registered.", Vehicles.size());
	if (Impl)
	{
		if (Impl->BatchQuery)
			Impl->BatchQuery->release();
		if (Impl->FrictionPairs)
			Impl->FrictionPairs->release();
	}
}

void FPhysXVehicleManager::SetDefaultSurface(PxMaterial* Material, float TireFriction)
{
	Impl->DefaultSurface.Material = Material;
	Impl->DefaultSurface.TireFriction = TireFriction;
	Impl->bFrictionTableDirty = true;
}

void FPhysXVehicleManager::SetSurfaceTireFriction(PxMaterial* Material, float TireFriction)
{
	if (!Material)
		return;
	for (FImpl::FSurface& Surface : Impl->Surfaces)
	{
		if (Surface.Material == Material)
		{
			Surface.TireFriction = TireFriction;
			Impl->bFrictionTableDirty = true;
			return;
		}
	}
	Impl->Surfaces.push_back({Material, TireFriction});
	Impl->bFrictionTableDirty = true;
}

void FPhysXVehicleManager::RemoveSurface(PxMaterial* Material)
{
	if (!Material)
		return;
	if (Impl->DefaultSurface.Material == Material)
		Impl->DefaultSurface.Material = nullptr; // 씬이 곧 다시 만들어 SetDefaultSurface로 알려 준다.
	std::erase_if(Impl->Surfaces, [Material](const FImpl::FSurface& Surface)
	              { return Surface.Material == Material; });
	Impl->bFrictionTableDirty = true;
}

void FPhysXVehicleManager::RebuildFrictionTable()
{
	Impl->bFrictionTableDirty = false;
	if (!Impl->FrictionPairs || !Impl->DefaultSurface.Material)
		return;

	// 표면 타입 번호 = 목록 순서. 0번이 기본 표면이다. PhysX는 등록되지 않은 재질을 0번 표면으로 취급한다 (헤더 문서).
	std::vector<const PxMaterial*> Materials;
	std::vector<PxVehicleDrivableSurfaceType> Types;
	std::vector<float> Frictions;
	Materials.push_back(Impl->DefaultSurface.Material);
	Frictions.push_back(Impl->DefaultSurface.TireFriction);
	for (const FImpl::FSurface& Surface : Impl->Surfaces)
	{
		if (Materials.size() >= MaxSurfaceTypes)
		{
			KRS_LOG(Warning, "[Vehicle] More than {} surface materials; the rest use the default surface friction.", MaxSurfaceTypes);
			break;
		}
		Materials.push_back(Surface.Material);
		Frictions.push_back(Surface.TireFriction);
	}
	Types.resize(Materials.size());
	for (size_t Index = 0; Index < Materials.size(); ++Index)
		Types[Index].mType = static_cast<PxU32>(Index);

	Impl->FrictionPairs->setup(NumTireTypes, static_cast<PxU32>(Materials.size()), Materials.data(), Types.data());
	for (size_t Index = 0; Index < Materials.size(); ++Index)
		Impl->FrictionPairs->setTypePairFriction(static_cast<PxU32>(Index), TireTypeIndex, Frictions[Index]);
	KRS_LOG(Info, "[Vehicle] Tire friction table rebuilt ({} surface types).", Materials.size());
}

void FPhysXVehicleManager::AddVehicle(UWheeledVehicleMovementComponent* Vehicle)
{
	if (!Vehicle || std::find(Vehicles.begin(), Vehicles.end(), Vehicle) != Vehicles.end())
		return;
	Vehicles.push_back(Vehicle);
	KRS_LOG(Info, "[Vehicle] Vehicle registered ({} total).", Vehicles.size());
}

void FPhysXVehicleManager::RemoveVehicle(UWheeledVehicleMovementComponent* Vehicle)
{
	const auto It = std::find(Vehicles.begin(), Vehicles.end(), Vehicle);
	if (It == Vehicles.end())
		return;
	Vehicles.erase(It);
	KRS_LOG(Info, "[Vehicle] Vehicle unregistered ({} total).", Vehicles.size());
}

bool FPhysXVehicleManager::EnsureBatchQueryCapacity(size_t TotalWheels)
{
	if (Impl->BatchQuery && Impl->BatchQueryCapacity >= TotalWheels)
		return true;
	PxScene* PhysXScene = Scene.GetPhysXScene();
	if (!PhysXScene)
		return false;

	// 용량은 넉넉히 두 배씩 늘려서 차량이 하나씩 늘 때마다 다시 만들지 않게 한다.
	size_t NewCapacity = (std::max)(static_cast<size_t>(16), Impl->BatchQueryCapacity);
	while (NewCapacity < TotalWheels)
		NewCapacity *= 2;

	if (Impl->BatchQuery)
	{
		Impl->BatchQuery->release();
		Impl->BatchQuery = nullptr;
	}
	Impl->RaycastResults.assign(NewCapacity, PxRaycastQueryResult());
	Impl->RaycastTouchBuffer.assign(NewCapacity, PxRaycastHit());

	// 바퀴마다 레이캐스트 하나, 막는 히트 하나만 받는다 (스윕/오버랩은 쓰지 않는다).
	PxBatchQueryDesc Desc(static_cast<PxU32>(NewCapacity), 0, 0);
	Desc.queryMemory.userRaycastResultBuffer = Impl->RaycastResults.data();
	Desc.queryMemory.userRaycastTouchBuffer = Impl->RaycastTouchBuffer.data();
	Desc.queryMemory.raycastTouchBufferSize = static_cast<PxU32>(NewCapacity);
	Desc.preFilterShader = SuspensionPreFilter;

	Impl->BatchQuery = PhysXScene->createBatchQuery(Desc);
	if (!Impl->BatchQuery)
	{
		Impl->BatchQueryCapacity = 0;
		KRS_LOG(Error, "[Vehicle] Failed to create the suspension raycast batch query.");
		return false;
	}
	Impl->BatchQueryCapacity = NewCapacity;
	KRS_LOG(Info, "[Vehicle] Suspension batch query created (capacity {} wheels).", NewCapacity);
	return true;
}

void FPhysXVehicleManager::Update(float DeltaTime)
{
	if (Vehicles.empty() || DeltaTime <= 0.0f || !Impl->FrictionPairs)
		return;
	PxScene* PhysXScene = Scene.GetPhysXScene();
	if (!PhysXScene)
		return;
	// 표면이 새로 등록되거나 마찰이 바뀌었으면 PxVehicleUpdates 전에 표를 다시 만든다.
	if (Impl->bFrictionTableDirty)
		RebuildFrictionTable();
	if (!Impl->DefaultSurface.Material)
		return;

	// 갱신할 수 있는 차량만 모은다. 섀시 바디가 이미 사라진 차량은 건드리면 안 된다.
	Impl->ActiveComponents.clear();
	Impl->ActiveVehicles.clear();
	for (UWheeledVehicleMovementComponent* Component : Vehicles)
	{
		if (Component && Component->CanUpdateVehicle())
		{
			Impl->ActiveComponents.push_back(Component);
			Impl->ActiveVehicles.push_back(Component->GetPhysXVehicle());
		}
	}
	const size_t NumVehicles = Impl->ActiveVehicles.size();
	if (NumVehicles == 0)
		return;
	const size_t TotalWheels = NumVehicles * NumWheelsPerVehicle;
	if (!EnsureBatchQueryCapacity(TotalWheels))
		return;

	// 바퀴 결과 버퍼: 차량마다 바퀴 4개씩 이어 붙인다.
	Impl->WheelQueryResults.assign(TotalWheels, PxWheelQueryResult());
	Impl->VehicleWheelQueryResults.resize(NumVehicles);
	for (size_t Index = 0; Index < NumVehicles; ++Index)
	{
		Impl->VehicleWheelQueryResults[Index].wheelQueryResults = &Impl->WheelQueryResults[Index * NumWheelsPerVehicle];
		Impl->VehicleWheelQueryResults[Index].nbWheelQueryResults = NumWheelsPerVehicle;
	}

	// 1) 서스펜션 레이캐스트: 모든 차의 모든 바퀴에서 아래로 한꺼번에 쏜다.
	PxVehicleSuspensionRaycasts(Impl->BatchQuery, static_cast<PxU32>(NumVehicles), Impl->ActiveVehicles.data(),
	                            static_cast<PxU32>(Impl->BatchQueryCapacity), Impl->RaycastResults.data());

	// 2) 모든 차량 갱신: 서스펜션, 타이어, 엔진 힘을 계산해 섀시의 속도에 반영한다.
	PxVehicleUpdates(DeltaTime, PhysXScene->getGravity(), *Impl->FrictionPairs, static_cast<PxU32>(NumVehicles),
	                 Impl->ActiveVehicles.data(), Impl->VehicleWheelQueryResults.data());

	// 3) 바퀴 결과를 각 차량 컴포넌트에 돌려준다.
	for (size_t Index = 0; Index < NumVehicles; ++Index)
		Impl->ActiveComponents[Index]->UpdateWheelStates(Impl->VehicleWheelQueryResults[Index].wheelQueryResults, NumWheelsPerVehicle);
}

#pragma warning(pop)
