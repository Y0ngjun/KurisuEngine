#include "EnginePCH.h"
#include "Physics/BodyInstance.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsMath.h"
#include "Physics/PhysScene.h"
#include "Physics/PhysicsFilter.h"
#include "Collision/CollisionProfile.h"
#include "Components/PrimitiveComponent.h"
#include <PxPhysicsAPI.h>
#include <algorithm>
#include <cmath>
#include <vector>

FBodyInstance::~FBodyInstance()
{
	TermBody();
}

void FBodyInstance::CopyBodyInstancePropertiesFrom(const FBodyInstance& Other)
{
	bSimulatePhysics = Other.bSimulatePhysics;
	CollisionEnabled = Other.CollisionEnabled;
	bEnableGravity = Other.bEnableGravity;
	LinearDamping = Other.LinearDamping;
	AngularDamping = Other.AngularDamping;
	bOverrideMass = Other.bOverrideMass;
	MassInKgOverride = Other.MassInKgOverride;
	COMNudge = Other.COMNudge;
	InertiaTensorScale = Other.InertiaTensorScale;
	PhysMaterialOverride = Other.PhysMaterialOverride;
	CollisionProfileName = Other.CollisionProfileName;
	ObjectType = Other.ObjectType;
	CollisionResponses = Other.CollisionResponses;
	bNotifyRigidBodyCollision = Other.bNotifyRigidBodyCollision;
	PositionSolverIterationCount = Other.PositionSolverIterationCount;
	VelocitySolverIterationCount = Other.VelocitySolverIterationCount;
}

bool FBodyInstance::InitBody(UBodySetup* Setup, const FTransform& Transform, UPrimitiveComponent* Component, FPhysScene& Scene)
{
	if (!HasValidSettings() || CollisionEnabled == ECollisionEnabled::NoCollision || IsValidBodyInstance() || !Setup || !Scene.IsInitialized() || Setup->AggGeom.GetElementCount() == 0)
		return false;
	const auto Position = PhysicsMath::ToPx(Transform.Location);
	const auto Rotation = PhysicsMath::ToPx(Transform.Rotation);
	const FVector& Scale = Transform.Scale;
	if (!Position.isFinite() || !Rotation.isFinite() || !Rotation.isUnit() ||
	    !PhysicsMath::ToPx(Scale).isFinite() || Scale.X <= 0 || Scale.Y <= 0 || Scale.Z <= 0 ||
	    !std::isfinite(MassInKgOverride) || MassInKgOverride <= 0)
		return false;
	const UPhysicalMaterial* Material = PhysMaterialOverride ? PhysMaterialOverride : Setup->PhysMaterial;
	if (Material && !Material->IsValidMaterial())
		return false;
	struct FShape
	{
		physx::PxGeometryHolder Geometry;
		physx::PxTransform Pose;
		ECollisionEnabled Collision = ECollisionEnabled::QueryAndPhysics;
		bool ContributesToMass = true;
		std::shared_ptr<physx::PxConvexMesh> ConvexMesh;
	};
	std::vector<FShape> Shapes;
	auto ScaledCenter = [&](const FVector& Center)
	{
		return physx::PxVec3(Center.X * Scale.X, Center.Y * Scale.Y, Center.Z * Scale.Z);
	};
	for (const FKBoxElem& Box : Setup->AggGeom.BoxElems)
	{
		const auto Center = ScaledCenter(Box.Center);
		const auto LocalRotation = PhysicsMath::ToPx(Box.Rotation.Quaternion());
		// A rotated primitive under nonuniform scale would require a sheared shape.
		if ((Box.Rotation.Pitch != 0 || Box.Rotation.Yaw != 0 || Box.Rotation.Roll != 0) &&
		    (Scale.X != Scale.Y || Scale.X != Scale.Z))
			return false;
		const physx::PxBoxGeometry Geometry(Box.X * Scale.X * 0.5f, Box.Y * Scale.Y * 0.5f, Box.Z * Scale.Z * 0.5f);
		if (!Geometry.isValid() || !Center.isFinite() || !LocalRotation.isFinite() || !LocalRotation.isUnit())
			return false;
		FShape Shape;
		Shape.Geometry.storeAny(Geometry);
		Shape.Pose = physx::PxTransform(Center, LocalRotation);
		Shape.Collision = Box.CollisionEnabled; Shape.ContributesToMass = Box.bContributeToMass;
		Shapes.push_back(Shape);
	}
	for (const FKSphereElem& Sphere : Setup->AggGeom.SphereElems)
	{
		if (Scale.X != Scale.Y || Scale.X != Scale.Z)
			return false;
		const auto Center = ScaledCenter(Sphere.Center);
		const physx::PxSphereGeometry Geometry(Sphere.Radius * Scale.X);
		if (!Geometry.isValid() || !Center.isFinite())
			return false;
		FShape Shape;
		Shape.Geometry.storeAny(Geometry);
		Shape.Pose = physx::PxTransform(Center);
		Shape.Collision = Sphere.CollisionEnabled; Shape.ContributesToMass = Sphere.bContributeToMass;
		Shapes.push_back(Shape);
	}

    for (const FKSphylElem& Capsule : Setup->AggGeom.SphylElems)
    {
        // A nonuniformly scaled capsule is not a capsule; reject rather than silently distort collision.
        if (Scale.X != Scale.Y || Scale.X != Scale.Z || !std::isfinite(Capsule.Length) || Capsule.Length < 0) return false;
        const auto Center = ScaledCenter(Capsule.Center);
        const auto Rotation = PhysicsMath::ToPx(Capsule.Rotation.Quaternion());
        const physx::PxCapsuleGeometry Geometry(Capsule.Radius * Scale.X, Capsule.Length * Scale.Z * 0.5f);
        if (!Geometry.isValid() || !Center.isFinite() || !Rotation.isUnit()) return false;
        FShape Shape; Shape.Geometry.storeAny(Geometry);
        // PhysX capsules extend along X, whereas FKSphylElem extends along local Z.
        Shape.Pose = physx::PxTransform(Center, Rotation * physx::PxQuat(-physx::PxHalfPi, physx::PxVec3(0,1,0)));
        Shape.Collision = Capsule.CollisionEnabled; Shape.ContributesToMass = Capsule.bContributeToMass;
        Shapes.push_back(std::move(Shape));
    }
    for (const FKConvexElem& Convex : Setup->AggGeom.ConvexElems)
    {
        const auto LocalPose = PhysicsMath::ToPxPose(Convex.Transform);
        const auto LocalScale = PhysicsMath::ToPx(Convex.Transform.Scale);
        if (!LocalPose.isValid() || !LocalScale.isFinite() || LocalScale.minElement() <= 0) return false;
        // Bake local TRS and component scale into vertices, including rotated nonuniform scaling.
        FKConvexElem Scaled;
        for (const FVector& Vertex : Convex.VertexData)
            Scaled.VertexData.Add(PhysicsMath::FromPx(LocalPose.transform(PhysicsMath::ToPx(Vertex).multiply(LocalScale)).multiply(PhysicsMath::ToPx(Scale))));
        std::shared_ptr<physx::PxConvexMesh> Mesh(Setup->CreateConvexMesh(Scaled, *Scene.GetPhysics()),
            [](physx::PxConvexMesh* Value){ if (Value) Value->release(); });
        if (!Mesh) return false;
        FShape Shape; Shape.ConvexMesh = Mesh;
        Shape.Geometry.storeAny(physx::PxConvexMeshGeometry(Mesh.get()));
        Shape.Pose = physx::PxTransform(physx::PxIdentity);
        Shape.Collision = Convex.CollisionEnabled; Shape.ContributesToMass = Convex.bContributeToMass;
        Shapes.push_back(std::move(Shape));
    }
	physx::PxPhysics& Physics = *Scene.GetPhysics();
	const auto Pose = PhysicsMath::ToPxPose(Transform);
	const auto Type = ResolveBodyType(Setup, Component);
	if (Type != EPhysicsBodyType::Static)
		Actor = Physics.createRigidDynamic(Pose);
	else
		Actor = Physics.createRigidStatic(Pose);
	if (!Actor)
		return false;
	OwnerScene = &Scene;
	OwnerComponent = Component;
	BodySetup = Setup;
	Scale3D = Scale;
	Actor->userData = this;
	if (auto* Dynamic = Actor->is<physx::PxRigidDynamic>())
	{
		Dynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, Type == EPhysicsBodyType::Kinematic);
		// onWake/onSleep 콜백을 받아 OnComponentWake/Sleep으로 전달한다.
		Dynamic->setActorFlag(physx::PxActorFlag::eSEND_SLEEP_NOTIFIES, true);
	}
	OwnerScene->RegisterBody(*this);
	// 같은 물리 재질을 쓰는 바디는 PxMaterial 하나를 공유한다 (소유는 씬). 차량이 바닥 종류를 재질로 구분하려면 필요하다.
	physx::PxMaterial* NativeMaterial = Scene.AcquireMaterial(Material);
	if (!NativeMaterial)
	{
		TermBody();
		return false;
	}
	bool bSuccess = true;
	for (const FShape& Definition : Shapes)
	{
		auto* Shape = Physics.createShape(Definition.Geometry.any(), *NativeMaterial, true);
		if (!Shape)
		{
			bSuccess = false;
			break;
		}
		Shape->setLocalPose(Definition.Pose);
		Shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, CollisionEnabled != ECollisionEnabled::QueryOnly);
		Shape->setFlag(physx::PxShapeFlag::eSCENE_QUERY_SHAPE, CollisionEnabled != ECollisionEnabled::PhysicsOnly);
		bSuccess = AttachShape(*Shape);
		if (bSuccess) { ShapeCollisionModes.back() = Definition.Collision; ShapeMassContribution.back() = Definition.ContributesToMass; }
		Shape->release(); // Actor retains its own reference on successful attachment.
		if (!bSuccess)
			break;
	}
	if (bSuccess)
		bSuccess = UpdateBodySettings();
	// Register only a completely configured native body into the simulation scene.
	if (!bSuccess || !AddToScene())
	{
		TermBody();
		return false;
	}
	SaveAppliedSettings();
	return true;
}

bool FBodyInstance::RecreateBody(UBodySetup* Setup, const FTransform& Transform, UPrimitiveComponent* Component, FPhysScene& Scene)
{
	if (!IsValidBodyInstance())
		return InitBody(Setup, Transform, Component, Scene);
	if (OwnerScene != &Scene)
		return false;
	FBodyInstance Replacement;
	Replacement.InstanceBodyIndex = InstanceBodyIndex;
	Replacement.InstanceBoneIndex = InstanceBoneIndex;
	Replacement.SelfCollisionGroup = SelfCollisionGroup;
	Replacement.CopyBodyInstancePropertiesFrom(*this);
	if (!Replacement.InitBody(Setup, Transform, Component, Scene))
	{
		RestoreAppliedSettings();
		return false;
	}

    if (GetBodyType() == EPhysicsBodyType::Simulated && Replacement.GetBodyType() == EPhysicsBodyType::Simulated && IsInstanceSimulatingPhysics() && Replacement.IsInstanceSimulatingPhysics())
    {
        Replacement.SetLinearVelocity(GetLinearVelocity());
        Replacement.SetAngularVelocityInRadians(GetAngularVelocityInRadians());
        if (!IsInstanceAwake()) Replacement.PutInstanceToSleep();
    }
	// 이전 액터에 붙은 관절은 액터와 함께 해제될 때 월드에 붙어 버리므로 먼저 끊는다. 소유자가 다시 연결한다.
	Scene.TermConstraintsAttachedTo(*this);
	std::swap(Actor, Replacement.Actor);
	std::swap(BodySetup, Replacement.BodySetup);
	std::swap(Scale3D, Replacement.Scale3D);
	std::swap(ShapeCollisionModes, Replacement.ShapeCollisionModes);
	std::swap(ShapeMassContribution, Replacement.ShapeMassContribution);
	Actor->userData = this;
	Replacement.Actor->userData = &Replacement;
	OwnerComponent = Component;
	SaveAppliedSettings();
	return true;
}

void FBodyInstance::TermBody()
{
	ClearPendingForces();
	// 관절을 바디보다 먼저 해제한다 (UE와 같은 순서).
	if (OwnerScene)
		OwnerScene->TermConstraintsAttachedTo(*this);
	if (Actor)
	{
		RemoveFromScene();
		Actor->userData = nullptr;
		// 부착된 Shape의 참조도 반환한다. Shape를 따로 중복 해제하지 않는다.
		Actor->release();
		Actor = nullptr;
	}
	if (OwnerScene)
		OwnerScene->UnregisterBody(*this);
	OwnerScene = nullptr;
	OwnerComponent = nullptr;
	BodySetup = nullptr;
	Scale3D = FVector(1, 1, 1);
	ShapeCollisionModes.clear(); ShapeMassContribution.clear();
}

bool FBodyInstance::IsInScene() const
{
	return Actor && OwnerScene && Actor->getScene() == OwnerScene->GetPhysXScene();
}

bool FBodyInstance::AddToScene()
{
	if (!Actor || !OwnerScene || !OwnerScene->IsInitialized())
		return false;
	if (IsInScene())
		return true;
	if (Actor->getScene())
		return false;
	OwnerScene->GetPhysXScene()->addActor(*Actor);
	return IsInScene();
}

void FBodyInstance::RemoveFromScene()
{
	if (Actor && Actor->getScene())
		Actor->getScene()->removeActor(*Actor);
}

bool FBodyInstance::AttachShape(physx::PxShape& Shape)
{
	if (!Actor || !Actor->attachShape(Shape)) return false;
    ShapeCollisionModes.push_back(ECollisionEnabled::QueryAndPhysics); ShapeMassContribution.push_back(true);
    return true;
}

bool FBodyInstance::DetachShape(physx::PxShape& Shape)
{
	if (!Actor)
		return false;
	const physx::PxU32 Count = Actor->getNbShapes();
	for (physx::PxU32 Index = 0; Index < Count; ++Index)
	{
		physx::PxShape* Attached = nullptr;
		Actor->getShapes(&Attached, 1, Index);
		if (Attached == &Shape)
		{
			Actor->detachShape(Shape);
			if (Index < ShapeCollisionModes.size()) ShapeCollisionModes.erase(ShapeCollisionModes.begin()+Index);
			if (Index < ShapeMassContribution.size()) ShapeMassContribution.erase(ShapeMassContribution.begin()+Index);
			return true;
		}
	}
	return false;
}

bool FBodyInstance::HasValidSettings() const
{
	return std::isfinite(MassInKgOverride) && MassInKgOverride > 0 &&
	       std::isfinite(LinearDamping) && LinearDamping >= 0 &&
	       std::isfinite(AngularDamping) && AngularDamping >= 0;
}

EPhysicsBodyType FBodyInstance::ResolveBodyType(const UBodySetup* Setup, const UPrimitiveComponent* Component) const
{
	// Mobility wins over asset/instance simulation settings.
	if (Component && Component->GetMobility() != EComponentMobility::Movable)
		return EPhysicsBodyType::Static;
	if (Setup && Setup->PhysicsType == EPhysicsType::Kinematic)
		return EPhysicsBodyType::Kinematic;
	if (Setup && Setup->PhysicsType == EPhysicsType::Simulated)
		return EPhysicsBodyType::Simulated;
	return bSimulatePhysics ? EPhysicsBodyType::Simulated : EPhysicsBodyType::Kinematic;
}

EPhysicsBodyType FBodyInstance::GetBodyType() const
{
	if (Actor)
	{
		if (const auto* Dynamic = Actor->is<physx::PxRigidDynamic>())
			return Dynamic->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC)
			           ? EPhysicsBodyType::Kinematic
			           : EPhysicsBodyType::Simulated;
		return EPhysicsBodyType::Static;
	}
	return ResolveBodyType(BodySetup, OwnerComponent);
}

bool FBodyInstance::IsCustomCollisionProfile() const
{
	return CollisionProfileName == UCollisionProfile::CustomCollisionProfileName() || !UCollisionProfile::FindProfile(CollisionProfileName);
}

bool FBodyInstance::SetCollisionProfileName(const FName& ProfileName)
{
	if (ProfileName == UCollisionProfile::CustomCollisionProfileName())
	{
		CollisionProfileName = ProfileName;
		return true;
	}
	const FCollisionResponseTemplate* Template = UCollisionProfile::FindProfile(ProfileName);
	if (!Template)
		return false;
	CollisionProfileName = ProfileName;
	CollisionEnabled = Template->CollisionEnabled;
	ObjectType = Template->ObjectType;
	CollisionResponses = Template->ResponseToChannels;
	return true;
}

void FBodyInstance::LoadProfileData()
{
	if (!IsCustomCollisionProfile())
		SetCollisionProfileName(CollisionProfileName);
}

void FBodyInstance::InvalidateCollisionProfileName()
{
	CollisionProfileName = UCollisionProfile::CustomCollisionProfileName();
}

bool FBodyInstance::SetInstanceSimulatePhysics(bool bSimulate)
{
	auto* Dynamic = Actor ? Actor->is<physx::PxRigidDynamic>() : nullptr;
	if (!Actor)
	{
		bSimulatePhysics = bSimulate;
		AppliedSimulate = bSimulate;
		return true;
	}
	if (!Dynamic)
		return false;
	bSimulatePhysics = bSimulate;
	AppliedSimulate = bSimulate;
	const bool bKinematic = ResolveBodyType(BodySetup, OwnerComponent) != EPhysicsBodyType::Simulated;
	if (Dynamic->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC) == bKinematic)
		return true;
	Dynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, bKinematic);
	if (!bKinematic && IsInScene())
		Dynamic->wakeUp();
	if (bKinematic)
		ClearPendingForces();
	return true;
}

bool FBodyInstance::IsInstanceSimulatingPhysics() const
{
	return IsInScene() && GetBodyType() == EPhysicsBodyType::Simulated &&
	       !Actor->getActorFlags().isSet(physx::PxActorFlag::eDISABLE_SIMULATION);
}

bool FBodyInstance::UpdateBodySettings()
{
	if (!HasValidSettings())
		return false;
	if (!Actor)
		return true;
	if (IsInScene() && AppliedMaterial != PhysMaterialOverride)
		return false;
	if (ResolveBodyType(BodySetup, OwnerComponent) != GetBodyType())
		return false;
	Actor->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, !bEnableGravity);
	const bool bQuery = CollisionEnabled == ECollisionEnabled::QueryOnly || CollisionEnabled == ECollisionEnabled::QueryAndPhysics;
	const bool bPhysics = CollisionEnabled == ECollisionEnabled::PhysicsOnly || CollisionEnabled == ECollisionEnabled::QueryAndPhysics;
	// Query-only actors stay in the query scene without integrating or receiving forces.
	Actor->setActorFlag(physx::PxActorFlag::eDISABLE_SIMULATION, !bPhysics);
	for (physx::PxU32 Index = 0; Index < Actor->getNbShapes(); ++Index)
	{
		physx::PxShape* Shape = nullptr;
		Actor->getShapes(&Shape, 1, Index);
		const auto Mode = Index < ShapeCollisionModes.size() ? ShapeCollisionModes[Index] : ECollisionEnabled::QueryAndPhysics;
        Shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, bPhysics && (Mode == ECollisionEnabled::PhysicsOnly || Mode == ECollisionEnabled::QueryAndPhysics));
        Shape->setFlag(physx::PxShapeFlag::eSCENE_QUERY_SHAPE, bQuery && (Mode == ECollisionEnabled::QueryOnly || Mode == ECollisionEnabled::QueryAndPhysics));
	}
	if (auto* Dynamic = Actor->is<physx::PxRigidDynamic>())
	{
		Dynamic->setLinearDamping(LinearDamping);
		Dynamic->setAngularDamping(AngularDamping);
		Dynamic->setSolverIterationCounts(static_cast<physx::PxU32>(std::clamp(PositionSolverIterationCount, 1, 255)),
		                                  static_cast<physx::PxU32>(std::clamp(VelocitySolverIterationCount, 1, 255)));
		if (!UpdateMassProperties()) return false;
	}
	if (!bPhysics)
		ClearPendingForces();
	UpdateFilterData();
	SaveAppliedSettings();
	return true;
}

void FBodyInstance::UpdateFilterData()
{
	if (!Actor)
		return;
	const physx::PxFilterData Data = PhysicsFilter::MakeFilterData(*this);
	bool bChanged = false;
	for (physx::PxU32 Index = 0; Index < Actor->getNbShapes(); ++Index)
	{
		physx::PxShape* Shape = nullptr;
		Actor->getShapes(&Shape, 1, Index);
		const physx::PxFilterData Current = Shape->getSimulationFilterData();
		if (Current.word0 != Data.word0 || Current.word1 != Data.word1 || Current.word2 != Data.word2 || Current.word3 != Data.word3)
			bChanged = true;
		Shape->setSimulationFilterData(Data);
		Shape->setQueryFilterData(Data);
	}
	// 이미 판정된 쌍(접촉/억제)은 필터 데이터만 바꿔서는 다시 판정되지 않는다.
	if (bChanged && IsInScene())
		OwnerScene->GetPhysXScene()->resetFiltering(*Actor);
}

void FBodyInstance::SaveAppliedSettings()
{
	AppliedSimulate = bSimulatePhysics;
	AppliedMass = MassInKgOverride;
	AppliedMaterial = PhysMaterialOverride;
	AppliedCollision = CollisionEnabled;
	AppliedGravity = bEnableGravity;
	AppliedOverrideMass = bOverrideMass;
	AppliedLinearDamping = LinearDamping;
	AppliedAngularDamping = AngularDamping;
	AppliedProfileName = CollisionProfileName;
	AppliedObjectType = ObjectType;
	AppliedResponses = CollisionResponses;
	AppliedNotifyRigidBodyCollision = bNotifyRigidBodyCollision;
}

void FBodyInstance::RestoreAppliedSettings()
{
	bSimulatePhysics = AppliedSimulate;
	MassInKgOverride = AppliedMass;
	PhysMaterialOverride = AppliedMaterial;
	CollisionEnabled = AppliedCollision;
	bEnableGravity = AppliedGravity;
	bOverrideMass = AppliedOverrideMass;
	LinearDamping = AppliedLinearDamping;
	AngularDamping = AppliedAngularDamping;
	CollisionProfileName = AppliedProfileName;
	ObjectType = AppliedObjectType;
	CollisionResponses = AppliedResponses;
	bNotifyRigidBodyCollision = AppliedNotifyRigidBodyCollision;
}

bool FBodyInstance::SetEnableGravity(bool bEnabled)
{
	bEnableGravity = bEnabled;
	AppliedGravity = bEnabled;
	if (Actor)
		Actor->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, !bEnabled);
	WakeInstance();
	return true;
}

bool FBodyInstance::SetLinearDamping(float Value)
{
	if (!std::isfinite(Value) || Value < 0)
		return false;
	LinearDamping = Value;
	AppliedLinearDamping = Value;
	if (Actor)
		if (auto* Dynamic = Actor->is<physx::PxRigidDynamic>())
			Dynamic->setLinearDamping(Value);
	return true;
}

bool FBodyInstance::SetAngularDamping(float Value)
{
	if (!std::isfinite(Value) || Value < 0)
		return false;
	AngularDamping = Value;
	AppliedAngularDamping = Value;
	if (Actor)
		if (auto* Dynamic = Actor->is<physx::PxRigidDynamic>())
			Dynamic->setAngularDamping(Value);
	return true;
}

bool FBodyInstance::SetMassOverride(float MassInKg, bool bOverride)
{
	if (!std::isfinite(MassInKg) || MassInKg <= 0)
		return false;

    const float PreviousMass = MassInKgOverride; const bool PreviousOverride = bOverrideMass;
    MassInKgOverride = MassInKg; bOverrideMass = bOverride;
    if (!UpdateMassProperties()) { MassInKgOverride = PreviousMass; bOverrideMass = PreviousOverride; return false; }
    AppliedMass = MassInKg; AppliedOverrideMass = bOverride;

	return true;
}

void FBodyInstance::AddForce(const FVector& Force, bool bAccelChange)
{
	if (!IsInstanceSimulatingPhysics() || !PhysicsMath::ToPx(Force).isFinite())
		return;
	(bAccelChange ? PendingAcceleration : PendingForce) += Force;
}

void FBodyInstance::AddTorqueInRadians(const FVector& Torque, bool bAccelChange)
{
	if (!IsInstanceSimulatingPhysics() || !PhysicsMath::ToPx(Torque).isFinite())
		return;
	(bAccelChange ? PendingAngularAcceleration : PendingTorque) += Torque;
}

void FBodyInstance::ApplyPendingForces()
{
	if (!IsInstanceSimulatingPhysics())
		return;
	auto* Dynamic = Actor->is<physx::PxRigidDynamic>();
	auto Apply = [&](const FVector& Value, physx::PxForceMode::Enum Mode, bool bTorque)
	{
		const auto Native = PhysicsMath::ToPx(Value);
		if (!Native.isFinite() || Native.isZero())
			return;
		if (bTorque)
			Dynamic->addTorque(Native, Mode);
		else
			Dynamic->addForce(Native, Mode);
	};
	Apply(PendingForce, physx::PxForceMode::eFORCE, false);
	Apply(PendingAcceleration, physx::PxForceMode::eACCELERATION, false);
	Apply(PendingTorque, physx::PxForceMode::eFORCE, true);
	Apply(PendingAngularAcceleration, physx::PxForceMode::eACCELERATION, true);
}

void FBodyInstance::PrepareForces(float DeltaTime, int Steps)
{
	// Integrate frame commands before distributing them over the fixed steps.
	// A frame without a physics step retains its impulse, not an extra full force.
	ForceImpulse += PendingForce * DeltaTime;
	AccelerationDelta += PendingAcceleration * DeltaTime;
	TorqueImpulse += PendingTorque * DeltaTime;
	AngularAccelerationDelta += PendingAngularAcceleration * DeltaTime;
	PendingForce = PendingAcceleration = PendingTorque = PendingAngularAcceleration = FVector::ZeroVector;
	if (Steps <= 0)
		return;
	const float Duration = static_cast<float>(Steps * FPhysScene::FixedTimeStep);
	PendingForce = ForceImpulse / Duration;
	PendingAcceleration = AccelerationDelta / Duration;
	PendingTorque = TorqueImpulse / Duration;
	PendingAngularAcceleration = AngularAccelerationDelta / Duration;
	ForceImpulse = AccelerationDelta = TorqueImpulse = AngularAccelerationDelta = FVector::ZeroVector;
}

void FBodyInstance::ClearPendingForces()
{
	PendingForce = PendingAcceleration = PendingTorque = PendingAngularAcceleration = FVector::ZeroVector;
	ForceImpulse = AccelerationDelta = TorqueImpulse = AngularAccelerationDelta = FVector::ZeroVector;
}

void FBodyInstance::AddImpulse(const FVector& Impulse, bool bVelChange)
{
	if (IsInstanceSimulatingPhysics() && PhysicsMath::ToPx(Impulse).isFinite())
		Actor->is<physx::PxRigidDynamic>()->addForce(PhysicsMath::ToPx(Impulse),
		                                             bVelChange ? physx::PxForceMode::eVELOCITY_CHANGE : physx::PxForceMode::eIMPULSE);
}

void FBodyInstance::AddAngularImpulseInRadians(const FVector& Impulse, bool bVelChange)
{
	if (IsInstanceSimulatingPhysics() && PhysicsMath::ToPx(Impulse).isFinite())
		Actor->is<physx::PxRigidDynamic>()->addTorque(PhysicsMath::ToPx(Impulse),
		                                              bVelChange ? physx::PxForceMode::eVELOCITY_CHANGE : physx::PxForceMode::eIMPULSE);
}

void FBodyInstance::SetLinearVelocity(const FVector& Velocity, bool bAddToCurrent)
{
	const FVector Value = bAddToCurrent ? GetLinearVelocity() + Velocity : Velocity;
	if (IsInstanceSimulatingPhysics() && PhysicsMath::ToPx(Value).isFinite())
		Actor->is<physx::PxRigidDynamic>()->setLinearVelocity(PhysicsMath::ToPx(Value));
}

void FBodyInstance::SetAngularVelocityInRadians(const FVector& Velocity, bool bAddToCurrent)
{
	const FVector Value = bAddToCurrent ? GetAngularVelocityInRadians() + Velocity : Velocity;
	if (IsInstanceSimulatingPhysics() && PhysicsMath::ToPx(Value).isFinite())
		Actor->is<physx::PxRigidDynamic>()->setAngularVelocity(PhysicsMath::ToPx(Value));
}

FVector FBodyInstance::GetLinearVelocity() const
{
	if (Actor)
		if (const auto* Dynamic = Actor->is<physx::PxRigidDynamic>())
			return PhysicsMath::FromPx(Dynamic->getLinearVelocity());
	return FVector::ZeroVector;
}

FVector FBodyInstance::GetAngularVelocityInRadians() const
{
	if (Actor)
		if (const auto* Dynamic = Actor->is<physx::PxRigidDynamic>())
			return PhysicsMath::FromPx(Dynamic->getAngularVelocity());
	return FVector::ZeroVector;
}

float FBodyInstance::GetBodyMass() const
{
	if (Actor)
		if (const auto* Dynamic = Actor->is<physx::PxRigidDynamic>())
			return Dynamic->getMass();
	return 0.0f;
}

void FBodyInstance::WakeInstance()
{
	if (IsInstanceSimulatingPhysics())
		Actor->is<physx::PxRigidDynamic>()->wakeUp();
}

void FBodyInstance::PutInstanceToSleep()
{
	ClearPendingForces();
	if (IsInstanceSimulatingPhysics())
		Actor->is<physx::PxRigidDynamic>()->putToSleep();
}

bool FBodyInstance::IsInstanceAwake() const
{
	return IsInstanceSimulatingPhysics() && !Actor->is<physx::PxRigidDynamic>()->isSleeping();
}

void FBodyInstance::SetBodyTransform(const FTransform& Transform, bool bTeleport)
{
	if (!Actor)
		return;
	const auto Pose = PhysicsMath::ToPxPose(Transform);
	if (!Pose.isValid())
		return;
	if (GetBodyType() == EPhysicsBodyType::Kinematic && !bTeleport &&
	    !Actor->getActorFlags().isSet(physx::PxActorFlag::eDISABLE_SIMULATION))
		Actor->is<physx::PxRigidDynamic>()->setKinematicTarget(Pose);
	else if (GetBodyType() != EPhysicsBodyType::Static || bTeleport)
		Actor->setGlobalPose(Pose);
}


bool FBodyInstance::UpdateMassProperties()
{
    if (!Actor) return true;
    auto* Dynamic = Actor->is<physx::PxRigidDynamic>();
    if (!Dynamic) return true;
    const auto* Material = PhysMaterialOverride ? PhysMaterialOverride : (BodySetup ? BodySetup->PhysMaterial : nullptr);
    if (Material && !Material->IsValidMaterial()) return false;
    const float DensityKgPerCm3 = (Material ? Material->Density : 1.0f) * 0.001f;
    std::vector<physx::PxMassProperties> Properties;
    std::vector<physx::PxTransform> Transforms;
    // bOnlyContributing=false collects every attached shape; used only to shape the inertia
    // when no shape contributes mass.
    auto Collect = [&](bool bOnlyContributing) -> bool
    {
        Properties.clear(); Transforms.clear();
        for (physx::PxU32 Index=0; Index<Actor->getNbShapes(); ++Index)
        {
            if (bOnlyContributing)
            {
                if (Index < ShapeMassContribution.size() && !ShapeMassContribution[Index]) continue;
                const auto Mode = Index < ShapeCollisionModes.size() ? ShapeCollisionModes[Index] : ECollisionEnabled::QueryAndPhysics;
                if (Mode == ECollisionEnabled::NoCollision || Mode == ECollisionEnabled::QueryOnly) continue;
            }
            physx::PxShape* Shape=nullptr; Actor->getShapes(&Shape,1,Index);
            const physx::PxMassProperties Property(Shape->getGeometry().any());
            if (!std::isfinite(Property.mass) || Property.mass <= 0) return false;
            Properties.push_back(Property); Transforms.push_back(Shape->getLocalPose());
        }
        return true;
    };
    if (!Collect(true)) return false;
    const bool bHasContributingShape = !Properties.empty();
    if (!bHasContributingShape && !Collect(false)) return false;

    // No contributing shape: use the override mass, or 1 kg like Unreal's fallback.
    const float DefaultMass = 1.0f;
    if (Properties.empty())
    {
        // No shapes at all: unit-sphere-like inertia about the actor origin.
        const float Mass = bOverrideMass ? MassInKgOverride : DefaultMass;
        if (!std::isfinite(Mass) || Mass <= 0) return false;
        Dynamic->setMass(Mass);
        Dynamic->setMassSpaceInertiaTensor(physx::PxVec3(Mass));
        Dynamic->setCMassLocalPose(physx::PxTransform(physx::PxIdentity));
        return true;
    }
    const auto Combined = physx::PxMassProperties::sum(Properties.data(),Transforms.data(),static_cast<physx::PxU32>(Properties.size()));
    const float TargetMass = bOverrideMass ? MassInKgOverride
        : (bHasContributingShape ? Combined.mass * DensityKgPerCm3 : DefaultMass);
    if (!std::isfinite(TargetMass) || TargetMass <= 0 || !std::isfinite(Combined.mass) || Combined.mass <= 0) return false;
    const auto Scaled = Combined * (TargetMass / Combined.mass);
    physx::PxQuat Frame;
    auto Inertia = physx::PxMassProperties::getMassSpaceInertia(Scaled.inertiaTensor,Frame);
    if (!Inertia.isFinite() || Inertia.minElement() <= 0 || !Frame.isUnit() || !Scaled.centerOfMass.isFinite()) return false;
    // UE처럼 주축별 관성 배율과 질량 중심 이동량을 마지막에 적용한다.
    const physx::PxVec3 InertiaScale = PhysicsMath::ToPx(InertiaTensorScale);
    const physx::PxVec3 CenterNudge = PhysicsMath::ToPx(COMNudge);
    if (!InertiaScale.isFinite() || InertiaScale.minElement() <= 0 || !CenterNudge.isFinite()) return false;
    Inertia = physx::PxVec3(Inertia.x * InertiaScale.x, Inertia.y * InertiaScale.y, Inertia.z * InertiaScale.z);
    Dynamic->setMass(TargetMass);
    Dynamic->setMassSpaceInertiaTensor(Inertia);
    Dynamic->setCMassLocalPose(physx::PxTransform(Scaled.centerOfMass + CenterNudge,Frame));
    return true;
}
