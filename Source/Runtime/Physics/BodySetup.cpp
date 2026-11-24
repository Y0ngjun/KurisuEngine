#include "EnginePCH.h"
#include "Physics/BodySetup.h"
#include "Components/PrimitiveComponent.h"
#include "Render/StaticMeshData.h"
#include "UObject/UObjectIterator.h"
#include <cmath>
#include <PxPhysicsAPI.h>
#include "Physics/PhysicsMath.h"
#include "Core/Misc/Paths.h"
#include <fstream>
#include <cstring>
#include <sstream>
#include <iomanip>
#include "Render/LineBatcher.h"
#include "Physics/PhysicsSystem.h"
#include "Engine/Engine.h"
#include "Launch/LaunchEngineLoop.h"

void UBodySetupCore::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	Record << SA_VALUE("BoneName", BoneName);
}

FKBoxElem UBodySetup::MakeFitBox(const FStaticMeshData& Mesh)
{
	FKBoxElem Box;
	Box.Center = (Mesh.AABB.Min + Mesh.AABB.Max) * 0.5f;
	const FVector Size = Mesh.AABB.Max - Mesh.AABB.Min;
	Box.X = std::max(Size.X, 0.01f);
	Box.Y = std::max(Size.Y, 0.01f);
	Box.Z = std::max(Size.Z, 0.01f);
	return Box;
}

FKSphereElem UBodySetup::MakeFitSphere(const FStaticMeshData& Mesh)
{
	FKSphereElem Sphere;
	Sphere.Center = (Mesh.AABB.Min + Mesh.AABB.Max) * 0.5f;
	float RadiusSquared = 0;
	for (const FVertex& Vertex : Mesh.Vertices)
		RadiusSquared = std::max(RadiusSquared, (Vertex.Position - Sphere.Center).SizeSquared());
	Sphere.Radius = std::max(std::sqrt(RadiusSquared), 0.01f);
	return Sphere;
}

FKSphylElem UBodySetup::MakeFitCapsule(const FStaticMeshData& Mesh)
{
	FKSphylElem Capsule;
	Capsule.Center = (Mesh.AABB.Min + Mesh.AABB.Max) * 0.5f;
	const FVector Size = Mesh.AABB.Max - Mesh.AABB.Min;
	Capsule.Radius = std::max(std::max(Size.X, Size.Y) * 0.5f, 0.01f);
	Capsule.Length = std::max(Size.Z - 2.0f * Capsule.Radius, 0.0f);
	return Capsule;
}

FKConvexElem UBodySetup::MakeFitConvex(const FStaticMeshData& Mesh)
{
	FKConvexElem Convex;
	for (const FVertex& Vertex : Mesh.Vertices)
		Convex.VertexData.Add(Vertex.Position);
	return Convex;
}

void UBodySetup::FitBox(const FStaticMeshData& Mesh, bool bInvalidate)
{
	AggGeom.EmptyElements();
	AggGeom.BoxElems.Add(MakeFitBox(Mesh));
	if (bInvalidate)
		InvalidatePhysicsData();
}

void UBodySetup::FitSphere(const FStaticMeshData& Mesh)
{
	AggGeom.EmptyElements();
	AggGeom.SphereElems.Add(MakeFitSphere(Mesh));
	InvalidatePhysicsData();
}

void UBodySetup::InvalidatePhysicsData()
{
	bDirty = true;
	CookedConvexData.clear();
	for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
		// Include instances whose body was removed after clearing this asset's geometry.
		// Compare asset ownership rather than lazily initializing every other mesh.
		if (It->GetBodyInstance().GetBodySetup() == this ||
		    (It->GetRenderMesh() && It->GetRenderMesh() == GetOuter()))
			{ It->ApplyDefaultCollision(); It->RecreatePhysicsState(); }
}

void UBodySetup::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	InvalidatePhysicsData();
}


UBodySetup::UBodySetup()
{
    DefaultInstance.bOverrideMass = false;
}

void UBodySetup::Serialize(FStructuredArchive::FRecord Record)
{
    Super::Serialize(Record);
    if (Record.GetUnderlyingArchive().IsLoading()) CookedConvexData.clear();
}

void UBodySetup::FitCapsule(const FStaticMeshData& Mesh)
{
    AggGeom.EmptyElements();
    AggGeom.SphylElems.Add(MakeFitCapsule(Mesh));
    InvalidatePhysicsData();
}

void UBodySetup::FitConvex(const FStaticMeshData& Mesh)
{
    AggGeom.EmptyElements();
    AggGeom.ConvexElems.Add(MakeFitConvex(Mesh));
    InvalidatePhysicsData();
}

physx::PxConvexMesh* UBodySetup::CreateConvexMesh(const FKConvexElem& Element, physx::PxPhysics& Physics)
{
    if (Element.VertexData.Num() < 4 || Element.VertexData.Num() > 1000000) return nullptr;
    std::vector<physx::PxVec3> Points;
    for (const FVector& Vertex : Element.VertexData)
    {
        const auto Point = PhysicsMath::ToPx(Vertex);
        if (!Point.isFinite()) return nullptr;
        Points.push_back(Point);
    }
    // Full source bytes are the in-memory key and are also checked on disk, so hash collisions cannot reuse another hull.
    const uint32 Version = PX_PHYSICS_VERSION;
    FString Key(reinterpret_cast<const char*>(&Version), sizeof(Version));
    Key.append(reinterpret_cast<const char*>(Points.data()), Points.size() * sizeof(physx::PxVec3));
    auto Found = CookedConvexData.find(Key);
    auto LoadMesh = [&](TArray<uint8>& Data) -> physx::PxConvexMesh*
    {
        if (Data.IsEmpty()) return nullptr;
        physx::PxDefaultMemoryInputData Input(Data.GetData(), static_cast<physx::PxU32>(Data.Num()));
        return Physics.createConvexMesh(Input);
    };
    if (Found != CookedConvexData.end()) return LoadMesh(Found->second);
    uint64 Hash = 14695981039346656037ull;
    for (unsigned char Byte : Key) { Hash ^= Byte; Hash *= 1099511628211ull; }
    std::ostringstream Name; Name << std::hex << Hash << ".pxconvex";
    const auto CachePath = FPaths::ProjectBuildDir() / "Physics" / "Cooked" / Name.str();
    TArray<uint8> Data;
    if (FPaths::HasProject())
    {
        std::ifstream Cache(CachePath, std::ios::binary);
        uint32 Magic=0, KeySize=0, DataSize=0;
        Cache.read(reinterpret_cast<char*>(&Magic),4); Cache.read(reinterpret_cast<char*>(&KeySize),4);
        Cache.read(reinterpret_cast<char*>(&DataSize),4);
        if (Cache && Magic==0x32435850 && KeySize==Key.size() && DataSize>0 && DataSize<=16*1024*1024)
        {
            FString StoredKey(KeySize, '\0'); Cache.read(StoredKey.data(),KeySize);
            if (Cache && StoredKey==Key)
            {
                Data.SetNum(static_cast<int32>(DataSize)); Cache.read(reinterpret_cast<char*>(Data.GetData()),DataSize);
                if (Cache) if (auto* Mesh=LoadMesh(Data)) { CookedConvexData.emplace(Key,std::move(Data)); return Mesh; }
            }
        }
    }
    physx::PxCookingParams Parameters(Physics.getTolerancesScale());
    auto* Cooking = PxCreateCooking(PX_PHYSICS_VERSION, Physics.getFoundation(), Parameters);
    if (!Cooking) return nullptr;
    physx::PxConvexMeshDesc Description;
    Description.points.count = static_cast<physx::PxU32>(Points.size());
    Description.points.stride = sizeof(physx::PxVec3); Description.points.data = Points.data();
    Description.flags = physx::PxConvexFlag::eCOMPUTE_CONVEX;
    Description.vertexLimit = 255;
    physx::PxDefaultMemoryOutputStream Output;
    const bool Success = Cooking->cookConvexMesh(Description, Output);
    Cooking->release();
    if (!Success || Output.getSize()==0) return nullptr;
    Data.SetNum(static_cast<int32>(Output.getSize()));
    std::memcpy(Data.GetData(),Output.getData(),Output.getSize());
    auto* Mesh = LoadMesh(Data);
    if (!Mesh) return nullptr;
    if (FPaths::HasProject())
    {
        std::error_code Error;
        std::filesystem::create_directories(CachePath.parent_path(),Error);
        if (!Error)
        {
            auto Temporary=CachePath; Temporary += ".tmp";
            std::ofstream Cache(Temporary,std::ios::binary|std::ios::trunc);
            const uint32 Magic=0x32435850, KeySize=static_cast<uint32>(Key.size()), DataSize=static_cast<uint32>(Data.Num());
            Cache.write(reinterpret_cast<const char*>(&Magic),4); Cache.write(reinterpret_cast<const char*>(&KeySize),4);
            Cache.write(reinterpret_cast<const char*>(&DataSize),4); Cache.write(Key.data(),Key.size());
            Cache.write(reinterpret_cast<const char*>(Data.GetData()),Data.Num()); Cache.flush();
            const bool Written=static_cast<bool>(Cache); Cache.close();
            if (Written) MoveFileExW(Temporary.c_str(),CachePath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
        }
    }
    CookedConvexData.emplace(Key,std::move(Data));
    return Mesh;
}

namespace
{
	void AddArc(FLineBatcher& Lines, const FVector& Center, const FVector& AxisA, const FVector& AxisB,
	            float Radius, float StartRad, float EndRad, const FVector4& Color, int32 Segments = 24)
	{
		FVector Previous = Center + (AxisA * std::cos(StartRad) + AxisB * std::sin(StartRad)) * Radius;
		for (int32 Index = 1; Index <= Segments; ++Index)
		{
			const float Angle = StartRad + (EndRad - StartRad) * static_cast<float>(Index) / Segments;
			const FVector Point = Center + (AxisA * std::cos(Angle) + AxisB * std::sin(Angle)) * Radius;
			Lines.AddLine(Previous, Point, Color);
			Previous = Point;
		}
	}

	FVector MatrixAxis(const FMatrix& Matrix, int32 Row)
	{
		return FVector(Matrix.M[Row][0], Matrix.M[Row][1], Matrix.M[Row][2]);
	}
}

void DrawConvexMeshDebug(FLineBatcher& Lines, const physx::PxConvexMesh& Mesh, const FMatrix& ToWorld, const FVector4& Color)
{
	const physx::PxVec3* Vertices = Mesh.getVertices();
	const physx::PxU8* Indices = Mesh.getIndexBuffer();
	for (physx::PxU32 Polygon = 0; Polygon < Mesh.getNbPolygons(); ++Polygon)
	{
		physx::PxHullPolygon Data;
		if (!Mesh.getPolygonData(Polygon, Data) || Data.mNbVerts < 2)
			continue;
		for (physx::PxU16 Index = 0; Index < Data.mNbVerts; ++Index)
		{
			const physx::PxU8 A = Indices[Data.mIndexBase + Index];
			const physx::PxU8 B = Indices[Data.mIndexBase + (Index + 1) % Data.mNbVerts];
			Lines.AddLine(ToWorld.TransformPosition(PhysicsMath::FromPx(Vertices[A])),
			              ToWorld.TransformPosition(PhysicsMath::FromPx(Vertices[B])), Color);
		}
	}
}

void UBodySetup::DrawDebug(FLineBatcher& Lines, const FMatrix& World, const FVector& Scale, const FVector4& Color)
{
	if (!PhysicsMath::ToPx(Scale).isFinite() || Scale.X <= 0 || Scale.Y <= 0 || Scale.Z <= 0)
		return;
	const bool bUniform = Scale.X == Scale.Y && Scale.X == Scale.Z;
	for (const FKBoxElem& Box : AggGeom.BoxElems)
	{
		if (Box.X <= 0 || Box.Y <= 0 || Box.Z <= 0)
			continue;
		if ((Box.Rotation.Pitch != 0 || Box.Rotation.Yaw != 0 || Box.Rotation.Roll != 0) && !bUniform)
			continue;
		const FVector Extent(Box.X * 0.5f, Box.Y * 0.5f, Box.Z * 0.5f);
		Lines.AddOrientedBox({-Extent, Extent}, FTransform(Box.Rotation, Box.Center).GetLocalMatrix() * World, Color);
	}
	if (bUniform)
	{
		for (const FKSphereElem& Sphere : AggGeom.SphereElems)
			if (Sphere.Radius > 0)
				Lines.AddSphere(World.TransformPosition(Sphere.Center), Sphere.Radius * Scale.X, Color, 48);
		for (const FKSphylElem& Capsule : AggGeom.SphylElems)
		{
			if (Capsule.Radius <= 0 || Capsule.Length < 0)
				continue;
			// 스케일을 뺀 축. 캡슐 축은 로컬 Z.
			const FMatrix Local = FTransform(Capsule.Rotation, Capsule.Center).GetLocalMatrix() * World;
			const FVector Up = MatrixAxis(Local, 2).GetSafeNormal();
			const FVector X = MatrixAxis(Local, 0).GetSafeNormal();
			const FVector Y = Up.Cross(X).GetSafeNormal();
			const FVector Center = World.TransformPosition(Capsule.Center);
			const float Radius = Capsule.Radius * Scale.X;
			const float HalfLength = Capsule.Length * 0.5f * Scale.X;
			const FVector A = Center + Up * HalfLength;
			const FVector B = Center - Up * HalfLength;
			constexpr float Pi = 3.14159265358979f;
			AddArc(Lines, A, X, Y, Radius, 0, 2 * Pi, Color);
			AddArc(Lines, B, X, Y, Radius, 0, 2 * Pi, Color);
			AddArc(Lines, A, X, Up, Radius, 0, Pi, Color);
			AddArc(Lines, A, Y, Up, Radius, 0, Pi, Color);
			AddArc(Lines, B, X, Up, Radius, 0, -Pi, Color);
			AddArc(Lines, B, Y, Up, Radius, 0, -Pi, Color);
			for (const FVector& Side : {X, X * -1.0f, Y, Y * -1.0f})
				Lines.AddLine(A + Side * Radius, B + Side * Radius, Color);
		}
	}
	FPhysicsSystem* System = GEngine ? GEngine->GetEngineLoop().GetPhysicsSystem() : nullptr;
	physx::PxPhysics* Physics = System ? System->GetPhysics() : nullptr;
	if (!Physics)
		return;
	for (const FKConvexElem& Convex : AggGeom.ConvexElems)
	{
		// 같은 정점이면 메모리 캐시에서 바로 나오므로 매 프레임 다시 쿠킹하지 않는다.
		physx::PxConvexMesh* Mesh = CreateConvexMesh(Convex, *Physics);
		if (!Mesh)
			continue;
		DrawConvexMeshDebug(Lines, *Mesh, Convex.Transform.GetLocalMatrix() * World, Color);
		Mesh->release();
	}
}
