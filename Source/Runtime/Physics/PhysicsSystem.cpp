#include "EnginePCH.h"
#include "Physics/PhysicsSystem.h"

#include <PxPhysicsAPI.h>

struct FPhysicsSystem::FImpl
{
	// 콜백은 Foundation을 해제할 때까지 살아 있어야 한다.
	physx::PxDefaultAllocator Allocator;
	physx::PxDefaultErrorCallback ErrorCallback;
	physx::PxFoundation* Foundation = nullptr;
	physx::PxPhysics* Physics = nullptr;
	bool bExtensionsInitialized = false;
	bool bVehicleSDKInitialized = false;
};

FPhysicsSystem::FPhysicsSystem() = default;

FPhysicsSystem::~FPhysicsSystem()
{
	Shutdown();
}

bool FPhysicsSystem::Init()
{
	if (IsInitialized())
		return true;
	Impl = std::make_unique<FImpl>();
	Impl->Foundation = PxCreateFoundation(PX_PHYSICS_VERSION, Impl->Allocator, Impl->ErrorCallback);
	if (!Impl->Foundation)
	{
		KRS_LOG(Error, "[Physics] PxCreateFoundation failed.");
		Shutdown();
		return false;
	}

	// cm/s/kg 월드에 맞춰 접촉/반발/수면 허용오차도 cm 기준으로 초기화한다.
	physx::PxTolerancesScale Scale;
	Scale.length = MetersToWorld;
	Scale.speed = 10.0f * MetersToWorld;
	Impl->Physics = PxCreatePhysics(PX_PHYSICS_VERSION, *Impl->Foundation, Scale);
	if (!Impl->Physics)
	{
		KRS_LOG(Error, "[Physics] PxCreatePhysics failed.");
		Shutdown();
		return false;
	}

	Impl->bExtensionsInitialized = PxInitExtensions(*Impl->Physics, nullptr);
	if (!Impl->bExtensionsInitialized)
	{
		KRS_LOG(Error, "[Physics] PxInitExtensions failed.");
		Shutdown();
		return false;
	}

	// 차량 SDK는 Foundation/Physics가 만들어진 뒤에 켠다 (UE4 FPhysXVehicleManager::InitVehicleSDK 대응).
	Impl->bVehicleSDKInitialized = physx::PxInitVehicleSDK(*Impl->Physics);
	if (!Impl->bVehicleSDKInitialized)
	{
		KRS_LOG(Error, "[Physics] PxInitVehicleSDK failed.");
		Shutdown();
		return false;
	}
	// 엔진 월드는 Z-up, +X가 앞이다 (UE와 같다). 차량 SDK 기본값은 Y-up이라 반드시 지정해야 한다.
	physx::PxVehicleSetBasisVectors(physx::PxVec3(0.0f, 0.0f, 1.0f), physx::PxVec3(1.0f, 0.0f, 0.0f));
	// 힘을 속도 변화로 적용한다 (UE4와 같은 방식). 가속도 방식보다 큰 타임스텝에서도 안정적이다.
	physx::PxVehicleSetUpdateMode(physx::PxVehicleUpdateMode::eVELOCITY_CHANGE);

	KRS_LOG(Info, "[Physics] PhysX {}.{}.{} initialized (CPU).",
	        PX_PHYSICS_VERSION_MAJOR, PX_PHYSICS_VERSION_MINOR, PX_PHYSICS_VERSION_BUGFIX);
	return true;
}

void FPhysicsSystem::Shutdown()
{
	if (!Impl)
		return;
	const bool bWasInitialized = IsInitialized();
	// 차량 SDK는 PxPhysics를 해제하기 전에 닫는다.
	if (Impl->bVehicleSDKInitialized)
	{
		physx::PxCloseVehicleSDK();
		Impl->bVehicleSDKInitialized = false;
	}
	if (Impl->bExtensionsInitialized)
	{
		PxCloseExtensions();
		Impl->bExtensionsInitialized = false;
	}
	if (Impl->Physics)
	{
		Impl->Physics->release();
		Impl->Physics = nullptr;
	}
	if (Impl->Foundation)
	{
		Impl->Foundation->release();
		Impl->Foundation = nullptr;
	}
	Impl.reset();
	if (bWasInitialized)
		KRS_LOG(Info, "[Physics] PhysX shut down.");
}

bool FPhysicsSystem::IsInitialized() const
{
	return Impl && Impl->Physics && Impl->bExtensionsInitialized && Impl->bVehicleSDKInitialized;
}

physx::PxPhysics* FPhysicsSystem::GetPhysics() const
{
	return IsInitialized() ? Impl->Physics : nullptr;
}
