#include "EnginePCH.h"
#include "Physics/PhysicsAssetUtils.h"
#include "Physics/PhysicsAsset.h"
#include "Render/SkeletalMesh.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

namespace
{
	struct FBoneBox
	{
		FVector Min = FVector(FLT_MAX, FLT_MAX, FLT_MAX);
		FVector Max = FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX);
		int32 Count = 0;
		bool IsValid() const { return Count > 0 && Min.X <= Max.X; }
		void Add(const FVector& P)
		{
			for (int Axis = 0; Axis < 3; ++Axis)
			{
				Min[Axis] = std::min(Min[Axis], P[Axis]);
				Max[Axis] = std::max(Max[Axis], P[Axis]);
			}
			++Count;
		}
		float LongestSide() const { return IsValid() ? std::max({Max.X - Min.X, Max.Y - Min.Y, Max.Z - Min.Z}) : 0.0f; }
	};

	// 본 하나에 모인 정점들 (본 공간). 상자는 크기 판정(MinBoneSize)에, 점들은 캡슐 맞춤에 쓴다.
	struct FBonePoints
	{
		std::vector<FVector> Points;
		FBoneBox Box;
		void Add(const FVector& P)
		{
			Points.push_back(P);
			Box.Add(P);
		}
	};

	// 형상을 축 방향 캡슐로 근사한 것 (관절 축·겹침 판정용): 축 방향(단위 벡터), 중심, 반지름, 반구를 포함한 반길이
	struct FFitCapsule
	{
		FVector Axis = FVector(0, 0, 1);
		FVector Center = FVector::ZeroVector;
		float Radius = 1.0f;
		float HalfHeight = 1.0f;
	};

	FVector AxisVector(int32 Axis)
	{
		FVector V = FVector::ZeroVector;
		V[Axis] = 1.0f;
		return V;
	}

	// D에 수직인 아무 단위 벡터
	FVector AnyPerpendicular(const FVector& D)
	{
		const FVector Ref = std::abs(D.X) < 0.9f ? FVector(1, 0, 0) : FVector(0, 1, 0);
		return D.Cross(Ref).GetSafeNormal();
	}

	// 상자의 가장 긴 변 방향 (뼈 방향을 쓰지 않을 때, 또는 자식이 없는 끝 본)
	FVector LongestBoxAxis(const FBoneBox& Box)
	{
		const FVector Size = Box.Max - Box.Min;
		return AxisVector(Size.X >= Size.Y && Size.X >= Size.Z ? 0 : (Size.Y >= Size.Z ? 1 : 2));
	}

	// 뼈 방향 = 본 원점에서 자식 본 원점들의 평균 쪽 (본 공간). 자식이 없거나 방향이 없으면 0 벡터.
	FVector BoneDirection(const TArray<FBoneInfo>& Bones, const std::vector<std::vector<int32>>& Children, int32 Bone)
	{
		FVector Sum = FVector::ZeroVector;
		for (const int32 Child : Children[Bone])
			Sum += Bones[Child].LocalBindPose.Location;	// 자식 원점의 부모 본 공간 위치
		return Sum.Size() > 1e-4f ? Sum.GetSafeNormal() : FVector::ZeroVector;
	}

	// 로컬 Z(형상의 긴 축)를 Dir 방향으로 돌리는 회전
	FQuat RotationZTo(const FVector& Dir)
	{
		const FVector Z(0, 0, 1);
		const float Cos = std::clamp(Z.Dot(Dir), -1.0f, 1.0f);
		if (Cos > 0.9999f)
			return FQuat::Identity();
		if (Cos < -0.9999f)
			return FQuat::MakeFromAxisAngle(FVector(1, 0, 0), 3.14159265359f);
		return FQuat::MakeFromAxisAngle(Z.Cross(Dir), std::acos(Cos));
	}

	// ① 범위 재기 (형상과 무관). Rotation의 로컬 축 기준으로 잰 중심과 반 크기 (UE BoxCenter / BoxExtent).
	//    로컬 Z = 축 방향, 로컬 X·Y = 축에 수직인 두 방향.
	struct FBoneBounds
	{
		FQuat Rotation = FQuat::Identity();
		FVector Axis = FVector(0, 0, 1);
		FVector Center = FVector::ZeroVector;
		FVector HalfExtent = FVector::ZeroVector;
	};

	FBoneBounds MeasureBounds(const std::vector<FVector>& Points, const FVector& Dir)
	{
		FBoneBounds Bounds;
		Bounds.Rotation = RotationZTo(Dir);
		Bounds.Axis = Dir;
		const FVector LocalAxes[3] = {Bounds.Rotation.RotateVector(FVector(1, 0, 0)), Bounds.Rotation.RotateVector(FVector(0, 1, 0)), Dir};
		FVector Min(FLT_MAX, FLT_MAX, FLT_MAX), Max(-FLT_MAX, -FLT_MAX, -FLT_MAX);
		for (const FVector& P : Points)
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const float D = P.Dot(LocalAxes[Axis]);
				Min[Axis] = std::min(Min[Axis], D);
				Max[Axis] = std::max(Max[Axis], D);
			}
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			Bounds.Center += LocalAxes[Axis] * ((Min[Axis] + Max[Axis]) * 0.5f);
			Bounds.HalfExtent[Axis] = std::max((Max[Axis] - Min[Axis]) * 0.5f, 0.5f);
		}
		return Bounds;
	}

	// ② 형상 만들기. 각자 BodySetup에 형상을 넣고, 관절·겹침 판정용 캡슐 근사를 돌려준다.
	FFitCapsule AddCapsule(UBodySetup& Setup, const FBoneBounds& Bounds, const FName& Name)
	{
		FFitCapsule Fit;
		Fit.Axis = Bounds.Axis;
		Fit.Center = Bounds.Center;
		// 두 수직 폭의 평균: 최대값을 쓰면 몸통·허벅지처럼 납작한 부위가 너무 뚱뚱해져 서로 겹친다.
		Fit.Radius = (Bounds.HalfExtent.X + Bounds.HalfExtent.Y) * 0.5f;
		Fit.HalfHeight = std::max(Bounds.HalfExtent.Z, Fit.Radius);
		FKSphylElem Capsule;
		Capsule.Name = Name;
		Capsule.Center = Bounds.Center;
		Capsule.Rotation = Bounds.Rotation.ToFRotator();
		Capsule.Radius = Fit.Radius;
		Capsule.Length = std::max(0.0f, (Fit.HalfHeight - Fit.Radius) * 2.0f);	// 반구를 뺀 원기둥 길이
		Setup.AggGeom.SphylElems.Add(Capsule);
		return Fit;
	}

	FFitCapsule AddBox(UBodySetup& Setup, const FBoneBounds& Bounds, const FName& Name)
	{
		FKBoxElem Box;
		Box.Name = Name;
		Box.Center = Bounds.Center;
		Box.Rotation = Bounds.Rotation.ToFRotator();
		Box.X = Bounds.HalfExtent.X * 2.0f;	// 변 길이
		Box.Y = Bounds.HalfExtent.Y * 2.0f;
		Box.Z = Bounds.HalfExtent.Z * 2.0f;
		Setup.AggGeom.BoxElems.Add(Box);
		FFitCapsule Fit;
		Fit.Axis = Bounds.Axis;
		Fit.Center = Bounds.Center;
		Fit.Radius = std::max(Bounds.HalfExtent.X, Bounds.HalfExtent.Y);
		Fit.HalfHeight = std::max(Bounds.HalfExtent.Z, Fit.Radius);
		return Fit;
	}

	FFitCapsule AddSphere(UBodySetup& Setup, const FBoneBounds& Bounds, const FName& Name)
	{
		FKSphereElem Sphere;
		Sphere.Name = Name;
		Sphere.Center = Bounds.Center;
		Sphere.Radius = std::max({Bounds.HalfExtent.X, Bounds.HalfExtent.Y, Bounds.HalfExtent.Z});	// UE: 반 크기 최대값
		Setup.AggGeom.SphereElems.Add(Sphere);
		FFitCapsule Fit;
		Fit.Axis = Bounds.Axis;
		Fit.Center = Bounds.Center;
		Fit.Radius = Sphere.Radius;
		Fit.HalfHeight = Sphere.Radius;
		return Fit;
	}

	// 두 선분 사이 최단 거리 (캡슐 겹침 판정)
	float SegmentDistance(const FVector& P1, const FVector& Q1, const FVector& P2, const FVector& Q2)
	{
		const FVector D1 = Q1 - P1, D2 = Q2 - P2, R = P1 - P2;
		const float A = D1.Dot(D1), E = D2.Dot(D2), F = D2.Dot(R);
		float S = 0.0f, T = 0.0f;
		if (A <= 1e-6f && E <= 1e-6f)
			return (P1 - P2).Size();
		if (A <= 1e-6f)
			T = std::clamp(F / E, 0.0f, 1.0f);
		else
		{
			const float C = D1.Dot(R);
			if (E <= 1e-6f)
				S = std::clamp(-C / A, 0.0f, 1.0f);
			else
			{
				const float B = D1.Dot(D2), Denom = A * E - B * B;
				S = Denom > 1e-6f ? std::clamp((B * F - C * E) / Denom, 0.0f, 1.0f) : 0.0f;
				T = (B * S + F) / E;
				if (T < 0.0f)
				{
					T = 0.0f;
					S = std::clamp(-C / A, 0.0f, 1.0f);
				}
				else if (T > 1.0f)
				{
					T = 1.0f;
					S = std::clamp((B - C) / A, 0.0f, 1.0f);
				}
			}
		}
		return ((P1 + D1 * S) - (P2 + D2 * T)).Size();
	}
}

bool FPhysicsAssetUtils::CreateFromSkeletalMesh(UPhysicsAsset& PhysicsAsset, const USkeletalMesh& Mesh, const FPhysAssetCreateParams& Params)
{
	const TArray<FBoneInfo>& Bones = Mesh.GetRefSkeleton();
	const auto& LODs = Mesh.GetRenderData().LODRenderData;
	if (Bones.IsEmpty() || LODs.IsEmpty())
		return false;
	const FSkeletalMeshLODRenderData& LOD = LODs[0];
	const int32 NumBones = Bones.Num();

	// 1. 바인드 포즈의 본 → 컴포넌트 행렬 (행 벡터: Local * Parent)
	std::vector<FMatrix> BindCS(NumBones);
	for (int32 Index = 0; Index < NumBones; ++Index)
	{
		const FMatrix Local = Bones[Index].LocalBindPose.GetLocalMatrix();
		BindCS[Index] = Bones[Index].ParentIndex < 0 ? Local : Local * BindCS[Bones[Index].ParentIndex];
	}

	// 2. 정점마다 가장 큰 가중치의 본에, 그 본 공간 위치로 모은다 (UE EVW_DominantWeight).
	std::vector<FBonePoints> Boxes(NumBones);
	for (int32 V = 0; V < LOD.BindVertices.Num() && V < LOD.SkinWeights.Num(); ++V)
	{
		const FSkinWeight& Weight = LOD.SkinWeights[V];
		int32 Dominant = -1;
		float Best = 0.0f;
		for (int32 K = 0; K < MaxBoneInfluences; ++K)
			if (Weight.Weight[K] > Best)
			{
				Best = Weight.Weight[K];
				Dominant = Weight.BoneIndex[K];
			}
		if (Dominant < 0 || Dominant >= NumBones)
			continue;
		Boxes[Dominant].Add(Bones[Dominant].InverseBindPose.TransformPosition(LOD.BindVertices[V].Position));
	}

	// 3. 작은 본은 부모 본 공간으로 옮겨 합친다. 자식이 부모보다 뒤에 있으므로 뒤에서부터 처리한다.
	std::vector<bool> bHasBody(NumBones, false);
	for (int32 Index = NumBones - 1; Index >= 0; --Index)
	{
		FBonePoints& Bone = Boxes[Index];
		if (!Bone.Box.IsValid())
			continue;
		const int32 Parent = Bones[Index].ParentIndex;
		if (Bone.Box.LongestSide() >= Params.MinBoneSize || Parent < 0)
		{
			bHasBody[Index] = Bone.Box.LongestSide() > 0.0f;
			continue;
		}
		const FMatrix ToParent = Bones[Index].LocalBindPose.GetLocalMatrix();
		for (const FVector& P : Bone.Points)
			Boxes[Parent].Add(ToParent.TransformPosition(P));
		Bone.Points.clear();
	}

	// 뼈 방향을 구하려고 자식 목록을 만든다.
	std::vector<std::vector<int32>> Children(NumBones);
	for (int32 Index = 0; Index < NumBones; ++Index)
		if (Bones[Index].ParentIndex >= 0)
			Children[Bones[Index].ParentIndex].push_back(Index);

	// 4. 바디
	PhysicsAsset.ClearAll();
	PhysicsAsset.PreviewMeshPath = Mesh.GetPath();
	std::vector<int32> BodyOfBone(NumBones, -1);
	std::vector<FFitCapsule> Fits;
	std::vector<int32> BoneOfBody;
	for (int32 Index = 0; Index < NumBones; ++Index)
	{
		if (!bHasBody[Index])
			continue;
		USkeletalBodySetup* Setup = PhysicsAsset.AddBody(FName(Bones[Index].Name));
		if (!Setup)
			continue;
		// 형상 축: 뼈 방향(옵션) → 없으면 정점 상자의 가장 긴 변
		FVector Dir = Params.bAutoOrientToBone ? BoneDirection(Bones, Children, Index) : FVector::ZeroVector;
		if (Dir.SizeSquared() < 0.5f)
			Dir = LongestBoxAxis(Boxes[Index].Box);
		// ① 범위 재기 → ② 형상 만들기 (UE CreateCollisionFromBone과 같은 순서)
		const FBoneBounds Bounds = MeasureBounds(Boxes[Index].Points, Dir);
		const FName ShapeName(Bones[Index].Name);
		FFitCapsule Fit;
		switch (Params.GeomType)
		{
		case EPhysAssetFitGeomType::EFG_Box:
			Fit = AddBox(*Setup, Bounds, ShapeName);
			break;
		case EPhysAssetFitGeomType::EFG_Sphere:
			Fit = AddSphere(*Setup, Bounds, ShapeName);
			break;
		default:
			Fit = AddCapsule(*Setup, Bounds, ShapeName);
			break;
		}
		// 질량은 형상 부피 × 밀도. 래그돌이 가벼워 튀지 않게 약간의 감쇠를 둔다.
		Setup->DefaultInstance.bOverrideMass = false;
		Setup->DefaultInstance.LinearDamping = 0.05f;
		Setup->DefaultInstance.AngularDamping = 0.1f;
		// 관절 사슬은 반복을 늘려야 덜 늘어난다 (UE 래그돌 권장값 수준).
		Setup->DefaultInstance.PositionSolverIterationCount = 16;
		Setup->DefaultInstance.VelocitySolverIterationCount = 4;
		Setup->MarkSaved();
		BodyOfBone[Index] = static_cast<int32>(BoneOfBody.size());
		BoneOfBody.push_back(Index);
		Fits.push_back(Fit);
	}
	if (BoneOfBody.empty())
		return false;

	// 5. 관절: 각 바디를 가장 가까운 조상 바디에 잇는다. 관절은 자식 본 원점에 두고 X(Twist)축을 캡슐 축으로 한다.
	if (Params.bCreateConstraints)
	{
		for (size_t Body = 0; Body < BoneOfBody.size(); ++Body)
		{
			const int32 Bone = BoneOfBody[Body];
			int32 ParentBone = Bones[Bone].ParentIndex;
			while (ParentBone >= 0 && BodyOfBone[ParentBone] < 0)
				ParentBone = Bones[ParentBone].ParentIndex;
			if (ParentBone < 0)
				continue;
			UPhysicsConstraintTemplate* Template = PhysicsAsset.AddConstraint(FName(Bones[Bone].Name), FName(Bones[ParentBone].Name));
			if (!Template)
				continue;
			FConstraintInstance& Joint = Template->DefaultInstance;
			const FFitCapsule& Fit = Fits[Body];
			FVector Twist = Fit.Axis;
			if (Fit.Center.Dot(Twist) < 0.0f)
				Twist = Twist * -1.0f; // 관절에서 바디 쪽을 향하게
			const FVector Secondary = AnyPerpendicular(Twist);
			Joint.Pos1 = FVector::ZeroVector;
			Joint.PriAxis1 = Twist;
			Joint.SecAxis1 = Secondary;
			// 같은 관절을 부모 바디 공간으로: 자식 본 → 부모 본 = BindCS[child] * BindCS[parent]⁻¹
			const FMatrix ChildToParent = BindCS[Bone] * BindCS[ParentBone].Inverse();
			Joint.Pos2 = ChildToParent.TransformPosition(FVector::ZeroVector);
			Joint.PriAxis2 = ChildToParent.TransformVector(Twist).GetSafeNormal();
			Joint.SecAxis2 = ChildToParent.TransformVector(Secondary).GetSafeNormal();
			FConstraintProfileProperties& Profile = Joint.ProfileInstance;
			Profile.ConeLimit.Swing1Motion = Params.AngularConstraintMode;
			Profile.ConeLimit.Swing2Motion = Params.AngularConstraintMode;
			Profile.ConeLimit.Swing1LimitDegrees = Params.SwingLimitDegrees;
			Profile.ConeLimit.Swing2LimitDegrees = Params.SwingLimitDegrees;
			Profile.TwistLimit.TwistMotion = Params.AngularConstraintMode;
			Profile.TwistLimit.TwistLimitDegrees = Params.TwistLimitDegrees;
			Profile.bDisableCollision = true;
		}
	}

	// 6. 바인드 포즈에서 겹친 캡슐 쌍은 충돌을 끈다 (관절로 이은 쌍은 관절이 이미 끈다).
	if (Params.bDisableCollisionsOfOverlappingBodies)
	{
		struct FSegment
		{
			FVector A, B;
			float Radius;
		};
		std::vector<FSegment> Segments;
		for (size_t Body = 0; Body < BoneOfBody.size(); ++Body)
		{
			const FFitCapsule& Fit = Fits[Body];
			const FVector Axis = Fit.Axis * (Fit.HalfHeight - Fit.Radius);
			const FMatrix& ToComponent = BindCS[BoneOfBody[Body]];
			Segments.push_back({ToComponent.TransformPosition(Fit.Center - Axis), ToComponent.TransformPosition(Fit.Center + Axis), Fit.Radius});
		}
		for (size_t I = 0; I < Segments.size(); ++I)
			for (size_t J = I + 1; J < Segments.size(); ++J)
				if (SegmentDistance(Segments[I].A, Segments[I].B, Segments[J].A, Segments[J].B) < Segments[I].Radius + Segments[J].Radius)
					PhysicsAsset.DisableCollision(static_cast<int32>(I), static_cast<int32>(J));
	}
	PhysicsAsset.UpdateBodySetupIndexMap();
	return true;
}
