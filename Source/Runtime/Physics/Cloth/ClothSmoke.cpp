#include "ClothSmoke.h"

#include <d3d11.h>
#include <malloc.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include <foundation/PxErrorCallback.h>
#include <NvCloth/Callbacks.h>
#include <NvCloth/Cloth.h>
#include <NvCloth/DxContextManagerCallback.h>
#include <NvCloth/Factory.h>
#include <NvCloth/Solver.h>
#include <NvClothExt/ClothFabricCooker.h>

namespace
{
	namespace Cloth = nv::cloth;

	class FClothAllocator final : public physx::PxAllocatorCallback
	{
	public:
		void* allocate(std::size_t Size, const char*, const char*, int) override
		{
			void* Memory = _aligned_malloc(Size, 16);
			if (!Memory)
				std::abort();
			return Memory;
		}

		void deallocate(void* Memory) override
		{
			_aligned_free(Memory);
		}
	};

	class FClothErrors final : public physx::PxErrorCallback
	{
	public:
		void reportError(physx::PxErrorCode::Enum Code, const char* Message,
		                 const char* File, int Line) override
		{
			char Text[2048];
			std::snprintf(Text, sizeof(Text), "[NvCloth:%d] %s (%s:%d)\n",
			              static_cast<int>(Code), Message, File, Line);
			OutputDebugStringA(Text);
			std::fputs(Text, stderr);
		}
	};

	class FClothAssert final : public Cloth::PxAssertHandler
	{
	public:
		void operator()(const char* Expression, const char* File,
		                int Line, bool&) override
		{
			FClothErrors{}.reportError(
			    physx::PxErrorCode::eINTERNAL_ERROR, Expression, File, Line);
			std::abort();
		}
	};

	void InitializeClothCallbacks()
	{
		static FClothAllocator Allocator;
		static FClothErrors Errors;
		static FClothAssert AssertHandler;

		static const bool Initialized = []
		{
			Cloth::InitializeNvCloth(&Allocator, &Errors, &AssertHandler, nullptr);
			return true;
		}();
		(void)Initialized;
	}

	class FClothDxContext final : public Cloth::DxContextManagerCallback
	{
	public:
		FClothDxContext(ID3D11Device* InDevice, ID3D11DeviceContext* InContext)
		    : Device(InDevice), Context(InContext)
		{
		}

		// 이 테스트는 호출 스레드에서 동기적으로 끝납니다.
		void acquireContext() override {}
		void releaseContext() override {}
		ID3D11Device* getDevice() const override { return Device; }
		ID3D11DeviceContext* getContext() const override { return Context; }
		bool synchronizeResources() const override { return false; }

	private:
		ID3D11Device* Device;
		ID3D11DeviceContext* Context;
	};

	using FFactoryPtr = std::unique_ptr<Cloth::Factory, decltype(&NvClothDestroyFactory)>;

	FClothSmokeResult SimulateQuad(Cloth::Factory& Factory)
	{
		constexpr std::uint32_t VertexCount = 4;
		constexpr std::uint32_t PinnedCount = 2;
		const physx::PxVec3 Points[VertexCount] = {
		    {-50.0f, 0.0f, 0.0f},
		    {50.0f, 0.0f, 0.0f},
		    {-50.0f, 50.0f, 0.0f},
		    {50.0f, 50.0f, 0.0f},
		};
		const float InvMasses[VertexCount] = {0.0f, 0.0f, 1.0f, 1.0f};
		// NvCloth 1.1.6's DX11 fabric packs triangle indices in pairs.
		const std::uint32_t Triangles[][3] = {{0, 1, 2}, {2, 1, 3}};

		Cloth::ClothMeshDesc Desc;
		Desc.points.data = Points;
		Desc.points.count = VertexCount;
		Desc.points.stride = sizeof(Points[0]);
		Desc.invMasses.data = InvMasses;
		Desc.invMasses.count = VertexCount;
		Desc.invMasses.stride = sizeof(InvMasses[0]);
		Desc.triangles.data = Triangles;
		Desc.triangles.count = sizeof(Triangles) / sizeof(Triangles[0]);
		Desc.triangles.stride = sizeof(Triangles[0]);

		if (!Desc.isValid())
			return {"Mesh descriptor"};

		const auto ReleaseFabric = [](Cloth::Fabric* Fabric)
		{
			Fabric->decRefCount();
		};
		std::unique_ptr<Cloth::Fabric, decltype(ReleaseFabric)> Fabric(
		    NvClothCookFabricFromMesh(
		        &Factory, Desc, physx::PxVec3(0.0f, 0.0f, -1.0f), nullptr, false),
		    ReleaseFabric);
		if (!Fabric)
			return {"Fabric cooking"};

		physx::PxVec4 Particles[VertexCount];
		for (std::uint32_t Index = 0; Index < VertexCount; ++Index)
			Particles[Index] = physx::PxVec4(Points[Index], InvMasses[Index]);

		std::unique_ptr<Cloth::Cloth> Instance(Factory.createCloth(
		    Cloth::Range<const physx::PxVec4>(Particles, Particles + VertexCount), *Fabric));
		if (!Instance)
			return {"Cloth creation"};

		std::vector<Cloth::PhaseConfig> Phases;
		for (std::uint32_t Index = 0; Index < Fabric->getNumPhases(); ++Index)
			Phases.emplace_back(static_cast<std::uint16_t>(Index));

		if (!Phases.empty())
		{
			Instance->setPhaseConfig(Cloth::Range<const Cloth::PhaseConfig>(
			    Phases.data(), Phases.data() + Phases.size()));
		}

		Instance->setGravity(physx::PxVec3(0.0f, 0.0f, -980.0f));

		std::unique_ptr<Cloth::Solver> Solver(Factory.createSolver());
		if (!Solver || Solver->hasError())
			return {"Solver creation"};

		Solver->addCloth(Instance.get());

		bool Stepped = Solver->beginSimulation(1.0f / 60.0f);
		if (Stepped)
		{
			const int ChunkCount = Solver->getSimulationChunkCount();
			for (int Index = 0; Index < ChunkCount; ++Index)
				Solver->simulateChunk(Index);
			Solver->endSimulation();
			Stepped = ChunkCount > 0 && !Solver->hasError();
		}

		Solver->removeCloth(Instance.get());
		if (!Stepped)
			return {"Simulation step"};

		// const 오버로드로 읽기 전용 매핑을 얻습니다.
		const Cloth::Cloth& ReadOnlyCloth = *Instance;
		const auto Output = ReadOnlyCloth.getCurrentParticles();
		if (Output.size() != VertexCount)
			return {"Particle count"};

		for (std::uint32_t Index = 0; Index < VertexCount; ++Index)
		{
			const auto& P = Output[Index];
			if (!std::isfinite(P.x) || !std::isfinite(P.y) || !std::isfinite(P.z))
				return {"Non-finite particle"};
		}

		constexpr float PinTolerance = 0.001f;
		for (std::uint32_t Index = 0; Index < PinnedCount; ++Index)
		{
			const auto& P = Output[Index];
			const auto& Initial = Points[Index];
			if (std::fabs(P.x - Initial.x) > PinTolerance ||
			    std::fabs(P.y - Initial.y) > PinTolerance ||
			    std::fabs(P.z - Initial.z) > PinTolerance)
			{
				return {"Pinned particle moved", Output[PinnedCount].z};
			}
		}

		const float FreeZ = Output[PinnedCount].z;
		for (std::uint32_t Index = PinnedCount; Index < VertexCount; ++Index)
		{
			if (Output[Index].z >= -PinTolerance)
				return {"Free particle did not fall", Output[Index].z};
		}
		return {nullptr, FreeZ};
	}
} // namespace

FClothSmokeResult RunCpuClothSmoke()
{
	InitializeClothCallbacks();
	FFactoryPtr Factory(NvClothCreateFactoryCPU(), &NvClothDestroyFactory);
	if (!Factory || Factory->getPlatform() != Cloth::Platform::CPU)
		return {"CPU factory"};

	return SimulateQuad(*Factory);
}

FClothSmokeResult RunDx11ClothSmoke(
    ID3D11Device* Device, ID3D11DeviceContext* Context)
{
	InitializeClothCallbacks();
	if (!NvClothCompiledWithDxSupport())
		return {"NvCloth binary has DX11 disabled"};

	if (!Device || !Context ||
	    Device->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0 ||
	    Context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
	{
		return {"D3D11 device or immediate context"};
	}

	FClothDxContext DxContext(Device, Context);
	FFactoryPtr Factory(NvClothCreateFactoryDX11(&DxContext), &NvClothDestroyFactory);
	if (!Factory || Factory->getPlatform() != Cloth::Platform::DX11)
		return {"DX11 factory"};

	return SimulateQuad(*Factory);
}

void InitializeNvClothCallbacks()
{
	InitializeClothCallbacks();
}