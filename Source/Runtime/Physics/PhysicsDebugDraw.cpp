#include "EnginePCH.h"
#include "Physics/PhysicsDebugDraw.h"
#include "Physics/PhysScene.h"
#include "Physics/BodyInstance.h"
#include "Physics/BodySetup.h"
#include "Physics/ConstraintInstance.h"
#include "Physics/PhysicsMath.h"
#include "Engine/World.h"
#include "Render/LineBatcher.h"

#include <PxPhysicsAPI.h>

namespace
{
	FVector4 BodyColor(const FBodyInstance& Body)
	{
		switch (Body.GetBodyType())
		{
		case EPhysicsBodyType::Simulated:
			return Body.IsInstanceAwake() ? FVector4(1.0f, 0.6f, 0.1f, 1.0f) : FVector4(0.55f, 0.55f, 0.55f, 1.0f);
		case EPhysicsBodyType::Kinematic:
			return FVector4(0.3f, 0.7f, 1.0f, 1.0f);
		default:
			return FVector4(0.3f, 0.9f, 0.4f, 1.0f);
		}
	}

	void DrawFrame(FLineBatcher& Lines, const physx::PxTransform& Frame, float Length)
	{
		const FVector Origin = PhysicsMath::FromPx(Frame.p);
		Lines.AddLine(Origin, Origin + PhysicsMath::FromPx(Frame.q.getBasisVector0()) * Length, FVector4(1, 0.15f, 0.15f, 1));
		Lines.AddLine(Origin, Origin + PhysicsMath::FromPx(Frame.q.getBasisVector1()) * Length, FVector4(0.2f, 1, 0.2f, 1));
		Lines.AddLine(Origin, Origin + PhysicsMath::FromPx(Frame.q.getBasisVector2()) * Length, FVector4(0.25f, 0.45f, 1, 1));
	}
}

void DrawPhysicsDebug(UWorld* World, FLineBatcher* Lines, bool bDrawBodies, bool bDrawConstraints)
{
	FPhysScene* Scene = World ? World->GetPhysicsScene() : nullptr;
	if (!Lines || !Scene || !Scene->IsInitialized())
		return;
	if (bDrawBodies)
	{
		for (FBodyInstance* Body : Scene->GetBodies())
		{
			UBodySetup* Setup = Body->GetBodySetup();
			if (!Setup || !Body->IsInScene())
				continue;
			const physx::PxTransform Pose = Body->GetActor()->getGlobalPose();
			const FTransform BodyTransform(PhysicsMath::FromPx(Pose.q), PhysicsMath::FromPx(Pose.p), Body->GetScale3D());
			Setup->DrawDebug(*Lines, BodyTransform.GetLocalMatrix(), Body->GetScale3D(), BodyColor(*Body));
		}
	}
	if (bDrawConstraints)
	{
		for (FConstraintInstance* Constraint : Scene->GetConstraints())
		{
			physx::PxD6Joint* Joint = Constraint->GetConstraintHandle();
			if (!Joint)
				continue;
			physx::PxRigidActor* Actor0 = nullptr;
			physx::PxRigidActor* Actor1 = nullptr;
			Joint->getActors(Actor0, Actor1);
			const physx::PxTransform Frame0 = (Actor0 ? Actor0->getGlobalPose() : physx::PxTransform(physx::PxIdentity)) * Joint->getLocalPose(physx::PxJointActorIndex::eACTOR0);
			const physx::PxTransform Frame1 = (Actor1 ? Actor1->getGlobalPose() : physx::PxTransform(physx::PxIdentity)) * Joint->getLocalPose(physx::PxJointActorIndex::eACTOR1);
			DrawFrame(*Lines, Frame0, 8.0f);
			DrawFrame(*Lines, Frame1, 5.0f);
			const FVector4 LinkColor = Constraint->IsBroken() ? FVector4(1, 0, 1, 1) : FVector4(1, 1, 0.2f, 1);
			Lines->AddLine(PhysicsMath::FromPx(Frame0.p), PhysicsMath::FromPx(Frame1.p), LinkColor);
			// 바디 중심까지 이어 어느 바디끼리 연결됐는지 보이게 한다.
			if (Actor0)
				Lines->AddLine(PhysicsMath::FromPx(Frame0.p), PhysicsMath::FromPx(Actor0->getGlobalPose().p), FVector4(0.7f, 0.7f, 0.2f, 1));
			if (Actor1)
				Lines->AddLine(PhysicsMath::FromPx(Frame1.p), PhysicsMath::FromPx(Actor1->getGlobalPose().p), FVector4(0.7f, 0.7f, 0.2f, 1));
		}
	}
}
