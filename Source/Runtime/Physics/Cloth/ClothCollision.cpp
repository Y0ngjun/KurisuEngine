#include "EnginePCH.h"
#include "ClothCollision.h"

#include "Components/SkeletalMeshComponent.h"
#include "Physics/PhysicsAsset.h"

#include <geometry/PxConvexMesh.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <span>

namespace
{
	FVector XYZ(const FVector4& V)
	{
		return FVector(V.X, V.Y, V.Z);
	}

	bool IsFinite(const FVector& V)
	{
		return std::isfinite(V.X) && std::isfinite(V.Y) && std::isfinite(V.Z);
	}

	bool IsPositiveFinite(float Value)
	{
		return std::isfinite(Value) && Value > 0.0f;
	}

	bool IsClothCollider(const FKShapeElem& Shape)
	{
		return Shape.CollisionEnabled == ECollisionEnabled::PhysicsOnly ||
		       Shape.CollisionEnabled == ECollisionEnabled::QueryAndPhysics;
	}

	bool AppendRoundShape(FClothCollisionBinding& Out, int32 BoneIndex,
	                      const FVector& A, const FVector& B,
	                      float Radius, bool bCapsule)
	{
		const int32 NumSlots = bCapsule ? 2 : 1;
		const int32 First = Out.LocalSpheres.Num();
		if (First + NumSlots > MaxClothSpheres ||
		    !IsPositiveFinite(Radius) || !IsFinite(A) || !IsFinite(B))
		{
			return false;
		}

		Out.LocalSpheres.Add({BoneIndex, FVector4(A.X, A.Y, A.Z, Radius)});
		if (bCapsule)
		{
			Out.LocalSpheres.Add({BoneIndex, FVector4(B.X, B.Y, B.Z, Radius)});
			Out.CapsuleIndices.Add(static_cast<uint32>(First));
			Out.CapsuleIndices.Add(static_cast<uint32>(First + 1));
		}
		return true;
	}

	bool AppendHull(FClothCollisionBinding& Out, int32 BoneIndex,
	                std::span<const FVector4> Planes)
	{
		const int32 First = Out.LocalPlanes.Num();
		if (Planes.size() < 4 ||
		    Planes.size() > static_cast<size_t>(MaxClothPlanes - First))
		{
			return false;
		}

		for (const FVector4& Plane : Planes)
		{
			if (!IsPositiveFinite(XYZ(Plane).Size()) || !std::isfinite(Plane.W))
				return false;
		}

		uint32 Mask = 0;
		for (const FVector4& Plane : Planes)
		{
			Mask |= uint32{1} << Out.LocalPlanes.Num();
			Out.LocalPlanes.Add({BoneIndex, Plane});
		}
		Out.ConvexMasks.Add(Mask);
		return true;
	}

	bool AppendBox(FClothCollisionBinding& Out, int32 BoneIndex,
	               const FKBoxElem& Box)
	{
		if (!IsFinite(Box.Center) || !IsPositiveFinite(Box.X) ||
		    !IsPositiveFinite(Box.Y) || !IsPositiveFinite(Box.Z))
		{
			return false;
		}

		const FQuat Rotation = Box.Rotation.Quaternion();
		const FVector Axes[] = {
		    Rotation.RotateVector(FVector(1.0f, 0.0f, 0.0f)),
		    Rotation.RotateVector(FVector(0.0f, 1.0f, 0.0f)),
		    Rotation.RotateVector(FVector(0.0f, 0.0f, 1.0f))};
		const float HalfExtents[] = {Box.X * 0.5f, Box.Y * 0.5f, Box.Z * 0.5f};

		std::array<FVector4, 6> Planes;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const FVector& N = Axes[Axis];
			const float CenterProjection = N.Dot(Box.Center);
			Planes[2 * Axis] = FVector4(
			    N.X, N.Y, N.Z, -CenterProjection - HalfExtents[Axis]);
			Planes[2 * Axis + 1] = FVector4(
			    -N.X, -N.Y, -N.Z, CenterProjection - HalfExtents[Axis]);
		}
		return AppendHull(Out, BoneIndex, Planes);
	}

	bool AppendConvex(FClothCollisionBinding& Out, int32 BoneIndex,
	                  USkeletalBodySetup& Setup, const FKConvexElem& Convex,
	                  physx::PxPhysics& Physics)
	{
		FKConvexElem BoneLocal;
		BoneLocal.VertexData.Reserve(Convex.VertexData.Num());

		const FMatrix ShapeToBone = Convex.Transform.GetLocalMatrix();
		for (const FVector& Vertex : Convex.VertexData)
			BoneLocal.VertexData.Add(ShapeToBone.TransformPosition(Vertex));

		const auto Release = [](physx::PxConvexMesh* Mesh)
		{
			Mesh->release();
		};

		const std::unique_ptr<physx::PxConvexMesh, decltype(Release)> Hull(
		    Setup.CreateConvexMesh(BoneLocal, Physics), Release);

		if (!Hull || Hull->getNbPolygons() >
		                 static_cast<uint32>(MaxClothPlanes - Out.LocalPlanes.Num()))
		{
			return false;
		}

		std::array<FVector4, MaxClothPlanes> Planes;
		for (uint32 Index = 0; Index < Hull->getNbPolygons(); ++Index)
		{
			physx::PxHullPolygon Face;
			if (!Hull->getPolygonData(Index, Face))
				return false;

			Planes[Index] = FVector4(
			    Face.mPlane[0], Face.mPlane[1], Face.mPlane[2], Face.mPlane[3]);
		}

		return AppendHull(Out, BoneIndex,
		                  std::span(Planes.data(), Hull->getNbPolygons()));
	}
}

FClothCollisionBinding BuildClothCollisionBinding(
    const USkeletalMeshComponent& Component, const UPhysicsAsset& Asset,
    physx::PxPhysics* Physics)
{
	FClothCollisionBinding Out;

	for (USkeletalBodySetup* Setup : Asset.SkeletalBodySetups)
	{
		if (!Setup)
			continue;

		const int32 Bone = Component.FindBoneIndex(Setup->BoneName.ToString());
		if (Bone < 0)
		{
			KRS_LOG(Warning, "[Cloth] {}: missing bone {} in collision asset.",
			        Component.GetName(), Setup->BoneName.ToString());
			continue;
		}

		for (const auto& Sphere : Setup->AggGeom.SphereElems)
		{
			if (IsClothCollider(Sphere) &&
			    !AppendRoundShape(Out, Bone, Sphere.Center, Sphere.Center,
			                      Sphere.Radius, false))
			{
				++Out.NumSkippedShapes;
			}
		}

		for (const auto& Capsule : Setup->AggGeom.SphylElems)
		{
			if (!IsClothCollider(Capsule))
				continue;

			if (!std::isfinite(Capsule.Length) || Capsule.Length < 0.0f)
			{
				++Out.NumSkippedShapes;
				continue;
			}

			const FVector HalfAxis = Capsule.Rotation.Quaternion().RotateVector(
			    FVector(0.0f, 0.0f, Capsule.Length * 0.5f));

			if (!AppendRoundShape(Out, Bone, Capsule.Center - HalfAxis,
			                      Capsule.Center + HalfAxis, Capsule.Radius,
			                      Capsule.Length > 0.0f))
			{
				++Out.NumSkippedShapes;
			}
		}

		for (const auto& Box : Setup->AggGeom.BoxElems)
		{
			if (IsClothCollider(Box) && !AppendBox(Out, Bone, Box))
				++Out.NumSkippedShapes;
		}

		for (const auto& Convex : Setup->AggGeom.ConvexElems)
		{
			if (IsClothCollider(Convex) &&
			    (!Physics || !AppendConvex(Out, Bone, *Setup, Convex, *Physics)))
			{
				++Out.NumSkippedShapes;
			}
		}
	}

	return Out;
}

bool BuildClothCollisionFrame(
    const FClothCollisionBinding& Binding,
    const TArray<FMatrix>& BoneToComponent,
    FClothCollisionFrame& OutFrame)
{
	OutFrame.Spheres.Reset();
	OutFrame.Planes.Reset();

	for (const auto& Sphere : Binding.LocalSpheres)
	{
		if (!BoneToComponent.IsValidIndex(Sphere.BoneIndex))
			return false;

		const FMatrix& Matrix = BoneToComponent[Sphere.BoneIndex];
		const FVector Center = Matrix.TransformPosition(XYZ(Sphere.Value));
		const float Scale =
		    std::max({Matrix.TransformVector(FVector(1.0f, 0.0f, 0.0f)).Size(),
		              Matrix.TransformVector(FVector(0.0f, 1.0f, 0.0f)).Size(),
		              Matrix.TransformVector(FVector(0.0f, 0.0f, 1.0f)).Size()});
		const float Radius = Sphere.Value.W * Scale;
		if (!IsFinite(Center) || !IsPositiveFinite(Radius))
			return false;

		OutFrame.Spheres.Add(FVector4(Center.X, Center.Y, Center.Z, Radius));
	}

	int32 LastBone = INDEX_NONE;
	FMatrix NormalMatrix;
	FVector Translation;

	for (const auto& Plane : Binding.LocalPlanes)
	{
		if (!BoneToComponent.IsValidIndex(Plane.BoneIndex))
			return false;

		if (Plane.BoneIndex != LastBone)
		{
			const FMatrix& Matrix = BoneToComponent[Plane.BoneIndex];
			const float Determinant = Matrix.Determinant();
			if (!std::isfinite(Determinant) || std::abs(Determinant) <= SMALL_NUMBER)
				return false;

			NormalMatrix = Matrix.Inverse().GetTransposed();
			Translation = Matrix.TransformPosition(FVector::ZeroVector);
			LastBone = Plane.BoneIndex;
		}

		const FVector N = NormalMatrix.TransformVector(XYZ(Plane.Value));
		const float Length = N.Size();
		const float D = Plane.Value.W - N.Dot(Translation);
		if (!IsPositiveFinite(Length) || !std::isfinite(D))
			return false;

		const FVector UnitN = N / Length;
		OutFrame.Planes.Add(FVector4(UnitN.X, UnitN.Y, UnitN.Z, D / Length));
	}

	return true;
}
