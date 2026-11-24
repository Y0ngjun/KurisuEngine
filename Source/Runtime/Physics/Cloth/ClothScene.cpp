#include "EnginePCH.h"
#include "ClothScene.h"
#include "ClothSmoke.h"
#include "ClothingAsset.h"
#include "ClothCollision.h"

#include <NvCloth/Cloth.h>
#include <NvCloth/Fabric.h>
#include <NvCloth/Factory.h>
#include <NvCloth/PhaseConfig.h>
#include <NvCloth/Solver.h>
#include <NvClothExt/ClothFabricCooker.h>
#include <NvCloth/DxContextManagerCallback.h>
#include <foundation/PxQuat.h>
#include <d3d11.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <array>

namespace
{
	namespace Nv = nv::cloth;

	class FClothDxContext final : public Nv::DxContextManagerCallback
	{
	public:
		FClothDxContext(ID3D11Device* InDevice, ID3D11DeviceContext* InContext)
		    : Device(InDevice), Context(InContext)
		{
		}

		// 월드 Cloth 틱과 렌더링이 같은 스레드에서 순차 실행된다.
		void acquireContext() override {}
		void releaseContext() override {}

		ID3D11Device* getDevice() const override { return Device; }
		ID3D11DeviceContext* getContext() const override { return Context; }

		bool synchronizeResources() const override { return false; }

	private:
		ID3D11Device* Device;
		ID3D11DeviceContext* Context;
	};

	constexpr float MaxWorldStep = 1.0f / 30.0f;

	using FFactoryPtr = std::unique_ptr<Nv::Factory, decltype(&NvClothDestroyFactory)>;

	struct FFabricDeleter
	{
		void operator()(Nv::Fabric* Fabric) const
		{
			Fabric->decRefCount();
		}
	};

	using FFabricPtr = std::unique_ptr<Nv::Fabric, FFabricDeleter>;

	void ApplyClothCollision(Nv::Cloth& Cloth,
	                         const FClothCollisionBinding& Binding,
	                         const FClothCollisionFrame& Frame,
	                         bool bReset)
	{
		std::array<physx::PxVec4, MaxClothSpheres> Spheres;
		std::array<physx::PxVec4, MaxClothPlanes> Planes;

		for (int32 I = 0; I < Frame.Spheres.Num(); ++I)
		{
			const auto& V = Frame.Spheres[I];
			Spheres[I] = physx::PxVec4(V.X, V.Y, V.Z, V.W);
		}
		for (int32 I = 0; I < Frame.Planes.Num(); ++I)
		{
			const auto& V = Frame.Planes[I];
			Planes[I] = physx::PxVec4(V.X, V.Y, V.Z, V.W);
		}

		const Nv::Range<const physx::PxVec4> SphereRange(
		    Spheres.data(), Spheres.data() + Frame.Spheres.Num());
		const Nv::Range<const physx::PxVec4> PlaneRange(
		    Planes.data(), Planes.data() + Frame.Planes.Num());
		
		if (bReset)
		{
			if (Cloth.getNumCapsules() > 0)
				Cloth.setCapsules({}, 0, Cloth.getNumCapsules());
			if (Cloth.getNumConvexes() > 0)
				Cloth.setConvexes({}, 0, Cloth.getNumConvexes());
			if (Cloth.getNumSpheres() > 0)
				Cloth.setSpheres({}, 0, Cloth.getNumSpheres());
			if (Cloth.getNumPlanes() > 0)
				Cloth.setPlanes({}, 0, Cloth.getNumPlanes());
		}

		if (!Frame.Spheres.IsEmpty())
			Cloth.setSpheres(SphereRange, 0, Cloth.getNumSpheres());
		if (!Frame.Planes.IsEmpty())
			Cloth.setPlanes(PlaneRange, 0, Cloth.getNumPlanes());

		if (!bReset)
			return;

		if (!Binding.CapsuleIndices.IsEmpty())
		{
			const uint32* Begin = Binding.CapsuleIndices.GetData();
			Cloth.setCapsules(
			    Nv::Range<const uint32>(Begin, Begin + Binding.CapsuleIndices.Num()), 0, 0);
		}
		if (!Binding.ConvexMasks.IsEmpty())
		{
			const uint32* Begin = Binding.ConvexMasks.GetData();
			Cloth.setConvexes(
			    Nv::Range<const uint32>(Begin, Begin + Binding.ConvexMasks.Num()), 0, 0);
		}

		Cloth.clearInterpolation();
	}
}

struct FClothSystem::FImpl
{
	TUniquePtr<FClothDxContext> DxContext;
	FFactoryPtr Factory{nullptr, &NvClothDestroyFactory};
};

FClothSystem::FClothSystem() = default;
FClothSystem::~FClothSystem() = default;

bool FClothSystem::Init(EClothBackend Backend,
                        ID3D11Device* Device,
                        ID3D11DeviceContext* Context)
{
	const bool bUseDx11 = Backend == EClothBackend::DX11;
	const auto Platform = bUseDx11 ? Nv::Platform::DX11 : Nv::Platform::CPU;
	const char* BackendName = bUseDx11 ? "DX11" : "CPU";

	if (IsInitialized())
	{
		if (Impl->Factory->getPlatform() != Platform)
			return false;

		return !bUseDx11 ||
		       (Impl->DxContext->getDevice() == Device &&
		        Impl->DxContext->getContext() == Context);
	}

	InitializeNvClothCallbacks();

	auto NewImpl = MakeUnique<FImpl>();

	if (bUseDx11)
	{
		if (!NvClothCompiledWithDxSupport())
		{
			KRS_LOG(Error, "[Cloth] NvCloth binary has DX11 support disabled.");
			return false;
		}

		if (!Device || !Context ||
		    Device->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0 ||
		    Context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
		{
			KRS_LOG(Error, "[Cloth] Dx11 requires FL 11.0+ and an immediate context.");
			return false;
		}

		NewImpl->DxContext = MakeUnique<FClothDxContext>(Device, Context);
		NewImpl->Factory.reset(NvClothCreateFactoryDX11(NewImpl->DxContext.get()));
	}
	else
	{
		NewImpl->Factory.reset(NvClothCreateFactoryCPU());
	}

	if (!NewImpl->Factory || NewImpl->Factory->getPlatform() != Platform)
	{
		KRS_LOG(Error, "[Cloth] Failed to create {} factory.", BackendName);
		return false;
	}

	Impl = std::move(NewImpl);
	KRS_LOG(Info, "[Cloth] NvCloth factory initialized. Backend={}", BackendName);
	return true;
}

bool FClothSystem::IsInitialized() const
{
	return Impl && Impl->Factory;
}

struct FClothScene::FImpl
{
	struct FEntry
	{
		FFabricPtr OwnedFabric;
		TUniquePtr<Nv::Cloth> Cloth;
		bool bInSolver = false;
	};

	explicit FImpl(FClothSystem& InSystem) : System(InSystem) {}
	~FImpl()
	{
		for (auto& Entry : Cloths)
		{
			if (Entry.second.bInSolver)
				Solver->removeCloth(Entry.second.Cloth.get());
		}

		Cloths.clear();
		// 이후 Solver가 해제된다. Factory와 Fabric은 엔진이 계속 보유한다.
	}

	bool RegisterCloth(uint64 InstanceId, Nv::Fabric& Fabric,
	                   Nv::Range<const physx::PxVec4> Particles,
	                   const FClothConfig& Config, FFabricPtr OwnedFabric)
	{
		TUniquePtr<Nv::Cloth> Instance(
		    System.Impl->Factory->createCloth(Particles, Fabric));
		if (!Instance)
			return false;

		std::vector<Nv::PhaseConfig> Phases;
		Phases.reserve(Fabric.getNumPhases());
		for (uint32 Index = 0; Index < Fabric.getNumPhases(); ++Index)
		{
			auto& Phase = Phases.emplace_back(static_cast<uint16>(Index));
			Phase.mStiffness = Config.Stiffness;
		}

		if (!Phases.empty())
		{
			Instance->setPhaseConfig(Nv::Range<const Nv::PhaseConfig>(
			    Phases.data(), Phases.data() + Phases.size()));
		}

		Instance->setGravity(physx::PxVec3(0, 0, Config.GravityZ));
		Instance->setSolverFrequency(Config.SolverFrequency);
		Instance->setDamping(
		    physx::PxVec3(Config.Damping.X, Config.Damping.Y, Config.Damping.Z));
		Instance->setFriction(Config.Friction);
		Instance->enableContinuousCollision(Config.bEnableContinuousCollision);

		auto [It, bInserted] = Cloths.try_emplace(
		    InstanceId, FEntry{std::move(OwnedFabric), std::move(Instance)});
		if (!bInserted)
			return false;

		Solver->addCloth(It->second.Cloth.get());
		It->second.bInSolver = true;
		return true;
	}

	FClothSystem& System;
	TUniquePtr<Nv::Solver> Solver;
	std::unordered_map<uint64, FEntry> Cloths;
};

FClothScene::FClothScene() = default;
FClothScene::~FClothScene() = default;

bool FClothScene::Init(FClothSystem& System)
{
	if (Impl)
		return &Impl->System == &System;

	if (!System.IsInitialized())
		return false;

	auto NewImpl = MakeUnique<FImpl>(System);
	NewImpl->Solver.reset(System.Impl->Factory->createSolver());
	if (!NewImpl->Solver || NewImpl->Solver->hasError())
		return false;

	Impl = std::move(NewImpl);
	return true;
}

bool FClothScene::RemoveCloth(uint64 InstanceId)
{
	if (!Impl)
		return false;

	const auto It = Impl->Cloths.find(InstanceId);
	if (It == Impl->Cloths.end())
		return false;

	if (It->second.bInSolver)
		Impl->Solver->removeCloth(It->second.Cloth.get());
	Impl->Cloths.erase(It);
	return true;
}

int32 FClothScene::GetClothCount() const
{
	return Impl ? Impl->Solver->getNumCloths() : 0;
}

bool FClothScene::Tick(float DeltaTime)
{
	if (!Impl || !std::isfinite(DeltaTime) || DeltaTime <= 0.0f)
		return false;

	auto& Solver = *Impl->Solver;
	if (Solver.hasError() || Solver.getNumCloths() == 0)
		return false;

	const float Step = std::min(DeltaTime, MaxWorldStep);
	if (!Solver.beginSimulation(Step))
		return false;

	const int ChunkCount = Solver.getSimulationChunkCount();
	for (int Chunk = 0; Chunk < ChunkCount; ++Chunk)
		Solver.simulateChunk(Chunk);

	Solver.endSimulation();
	return ChunkCount > 0 && !Solver.hasError();
}

bool FClothScene::AddClothingMesh(
    uint64 InstanceId, const FClothingLODData& Data, const FClothConfig& Config)
{
	if (!Impl || Impl->Solver->hasError() || Impl->Cloths.contains(InstanceId))
		return false;

	const int32 Count = Data.RestPositions.Num();
	if (Count < 3 || Data.MaxDistances.Num() != Count ||
	    Data.TriangleIndices.IsEmpty() || Data.TriangleIndices.Num() % 3 != 0)
	{
		return false;
	}

	for (uint32 Index : Data.TriangleIndices)
	{
		if (Index >= static_cast<uint32>(Count))
			return false;
	}

	std::vector<physx::PxVec3> Points;
	std::vector<float> InvMasses;
	std::vector<physx::PxVec4> Particles;
	Points.reserve(Count);
	InvMasses.reserve(Count);
	Particles.reserve(Count);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector& P = Data.RestPositions[Index];
		const float InvMass = Data.MaxDistances[Index] > 0.0f ? 1.0f : 0.0f;
		Points.emplace_back(P.X, P.Y, P.Z);
		InvMasses.push_back(InvMass);
		Particles.emplace_back(Points.back(), InvMass);
	}

	Nv::ClothMeshDesc Desc;
	Desc.points.data = Points.data();
	Desc.points.count = static_cast<uint32>(Count);
	Desc.points.stride = sizeof(physx::PxVec3);
	Desc.invMasses.data = InvMasses.data();
	Desc.invMasses.count = static_cast<uint32>(Count);
	Desc.invMasses.stride = sizeof(float);
	Desc.triangles.data = Data.TriangleIndices.GetData();
	Desc.triangles.count = static_cast<uint32>(Data.TriangleIndices.Num() / 3);
	Desc.triangles.stride = sizeof(uint32) * 3;

	if (!Desc.isValid())
		return false;

	auto* Factory = Impl->System.Impl->Factory.get();
	FFabricPtr Fabric(NvClothCookFabricFromMesh(
	    Factory, Desc, physx::PxVec3(0, 0, -1), nullptr, false));
	if (!Fabric)
		return false;

	Nv::Fabric& FabricRef = *Fabric;
	return Impl->RegisterCloth(
	    InstanceId, FabricRef,
	    Nv::Range<const physx::PxVec4>(
	        Particles.data(), Particles.data() + Particles.size()),
	    Config, std::move(Fabric));
}

bool FClothScene::ReadClothPositions(
    uint64 InstanceId, TArray<FVector>& OutPositions) const
{
	if (!Impl)
		return false;

	const auto It = Impl->Cloths.find(InstanceId);
	if (It == Impl->Cloths.end())
		return false;

	const Nv::Cloth& Cloth = *It->second.Cloth;
	const auto Particles = Cloth.getCurrentParticles();

	TArray<FVector> Positions;
	Positions.SetNum(Particles.size());
	for (uint32 Index = 0; Index < Particles.size(); ++Index)
	{
		const auto& P = Particles[Index];
		if (!std::isfinite(P.x) || !std::isfinite(P.y) ||
		    !std::isfinite(P.z))
		{
			return false;
		}
		Positions[Index] = FVector(P.x, P.y, P.z);
	}
	OutPositions = std::move(Positions);
	return true;
}

bool FClothScene::UpdateClothInputs(
    uint64 InstanceId,
    const FClothingLODData& Data,
    const TArray<FVector>& SkinnedPositions,
    const FVector& WorldLocation,
    const FQuat& WorldRotation,
    float GravityZ,
    const FClothCollisionBinding& Binding,
    const FClothCollisionFrame& Frame,
    bool bReset)
{
	if (!Impl)
		return false;

	const auto It = Impl->Cloths.find(InstanceId);
	if (It == Impl->Cloths.end())
		return false;

	auto& Cloth = *It->second.Cloth;
	const int32 Count = SkinnedPositions.Num();
	if (Count != Data.MaxDistances.Num() ||
	    static_cast<uint32>(Count) != Cloth.getNumParticles())
	{
		return false;
	}

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector& P = SkinnedPositions[Index];
		const float Radius = Data.MaxDistances[Index];

		if (!std::isfinite(P.X) || !std::isfinite(P.Y) ||
		    !std::isfinite(P.Z) || !std::isfinite(Radius) || Radius < 0.0f)
		{
			return false;
		}
	}

	const FQuat Rotation = WorldRotation.Normalized();
	const physx::PxQuat PxRotation(
	    Rotation.X, Rotation.Y, Rotation.Z, Rotation.W);
	const physx::PxVec3 PxLocation(
	    WorldLocation.X, WorldLocation.Y, WorldLocation.Z);

	//const FVector LocalGravity =
	//    Rotation.UnitInverse().RotateVector(FVector(0, 0, GravityZ));

	//Cloth.setGravity(
	//    physx::PxVec3(LocalGravity.X, LocalGravity.Y, LocalGravity.Z));
	Cloth.setGravity(physx::PxVec3(0.0f, 0.0f, GravityZ));

	if (bReset)
	{
		Cloth.teleportToLocation(PxLocation, PxRotation);
		Cloth.clearInertia();
		Cloth.clearMotionConstraints();
	}
	else
	{
		Cloth.setTranslation(PxLocation);
		Cloth.setRotation(PxRotation);
	}

	ApplyClothCollision(Cloth, Binding, Frame, bReset);

	const auto UpdateParticles = [&](auto& Particles)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			// MaxDistance가 0이면 고정된 입자이므로 항상 스킨 위치를 유지한다.
			// 그 외에는 bReset이 true일 때만 스킨 위치를 강제로 적용한다.
			if (!bReset && Data.MaxDistances[Index] > 0.0f)
				continue;

			const FVector& P = SkinnedPositions[Index];
			auto& Particle = Particles[Index];
			Particle.x = P.X;
			Particle.y = P.Y;
			Particle.z = P.Z;
			// w는 생성할 때 지정한 역질량을 유지한다.
		}
	};

	{
		auto Current = Cloth.getCurrentParticles();
		UpdateParticles(Current);
	}

	{
		auto Previous = Cloth.getPreviousParticles();
		UpdateParticles(Previous);
	}

	auto Constraints = Cloth.getMotionConstraints();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector& P = SkinnedPositions[Index];
		Constraints[Index] =
		    physx::PxVec4(P.X, P.Y, P.Z, Data.MaxDistances[Index]);
	}

	return true;
}

bool FClothScene::SetClothActive(uint64 InstanceId, bool bActive)
{
	if (!Impl)
		return false;

	const auto It = Impl->Cloths.find(InstanceId);
	if (It == Impl->Cloths.end())
		return false;

	auto& Entry = It->second;
	if (Entry.bInSolver == bActive)
		return true;

	if (bActive)
		Impl->Solver->addCloth(Entry.Cloth.get());
	else
		Impl->Solver->removeCloth(Entry.Cloth.get());

	Entry.bInSolver = bActive;
	return true;
}
