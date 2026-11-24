#include "EnginePCH.h"
#include "Physics/ConstraintInstance.h"
#include "Physics/BodyInstance.h"
#include "Physics/PhysScene.h"
#include "Physics/PhysicsMath.h"

#include <PxPhysicsAPI.h>
#include <algorithm>
#include <cmath>

namespace
{
	constexpr float DegToRad = 3.14159265358979f / 180.0f;
	// PhysX 각도 한계는 (0, π) 열린 구간이어야 한다.
	constexpr float MinAngle = 0.01f * DegToRad;
	constexpr float MaxAngle = 179.0f * DegToRad;

	physx::PxQuat MakeFrameRotation(const FVector& InPri, const FVector& InSec)
	{
		physx::PxVec3 X = PhysicsMath::ToPx(InPri);
		physx::PxVec3 Y = PhysicsMath::ToPx(InSec);
		if (!X.isFinite() || X.magnitudeSquared() < 1e-8f)
			X = physx::PxVec3(1, 0, 0);
		X.normalize();
		Y -= X * X.dot(Y);
		if (!Y.isFinite() || Y.magnitudeSquared() < 1e-8f)
		{
			// 보조 축이 주 축과 평행하면 아무 수직 축을 고른다.
			Y = std::abs(X.x) < 0.9f ? physx::PxVec3(1, 0, 0) : physx::PxVec3(0, 1, 0);
			Y -= X * X.dot(Y);
		}
		Y.normalize();
		const physx::PxVec3 Z = X.cross(Y);
		physx::PxQuat Q(physx::PxMat33(X, Y, Z));
		Q.normalize();
		return Q;
	}

	physx::PxD6Motion::Enum ToPx(ELinearConstraintMotion Motion)
	{
		switch (Motion)
		{
		case LCM_Free: return physx::PxD6Motion::eFREE;
		case LCM_Limited: return physx::PxD6Motion::eLIMITED;
		default: return physx::PxD6Motion::eLOCKED;
		}
	}
	physx::PxD6Motion::Enum ToPx(EAngularConstraintMotion Motion, float LimitDegrees)
	{
		switch (Motion)
		{
		case ACM_Free: return physx::PxD6Motion::eFREE;
		// 0도 한계는 잠금과 같다. PhysX는 0 한계를 받지 않는다.
		case ACM_Limited: return LimitDegrees > 0.0f ? physx::PxD6Motion::eLIMITED : physx::PxD6Motion::eLOCKED;
		default: return physx::PxD6Motion::eLOCKED;
		}
	}
	float SwingLimitAngle(EAngularConstraintMotion Motion, float LimitDegrees)
	{
		// 원뿔 한계는 두 swing을 함께 쓴다. 자유 축은 최대로, 잠긴 축은 최소로 둔다 (잠금은 motion이 처리한다).
		if (Motion == ACM_Free)
			return MaxAngle;
		if (Motion == ACM_Locked)
			return MinAngle;
		return std::clamp(LimitDegrees * DegToRad, MinAngle, MaxAngle);
	}
	physx::PxD6JointDrive MakeDrive(const FConstraintDrive& Drive, bool bEnable)
	{
		if (!bEnable || !Drive.bEnablePositionDrive)
			return physx::PxD6JointDrive();
		const float MaxForce = Drive.MaxForce > 0.0f ? Drive.MaxForce : PX_MAX_F32;
		return physx::PxD6JointDrive(std::max(0.0f, Drive.Stiffness), std::max(0.0f, Drive.Damping), MaxForce, true);
	}

	void ApplySettings(physx::PxD6Joint& Joint, const FConstraintProfileProperties& Profile, float Scale)
	{
		const FLinearConstraint& Linear = Profile.LinearLimit;
		Joint.setMotion(physx::PxD6Axis::eX, ToPx(Linear.XMotion));
		Joint.setMotion(physx::PxD6Axis::eY, ToPx(Linear.YMotion));
		Joint.setMotion(physx::PxD6Axis::eZ, ToPx(Linear.ZMotion));
		if (Linear.XMotion == LCM_Limited || Linear.YMotion == LCM_Limited || Linear.ZMotion == LCM_Limited)
		{
			physx::PxPhysics& Physics = Joint.getScene() ? Joint.getScene()->getPhysics() : PxGetPhysics();
			Joint.setLinearLimit(physx::PxJointLinearLimit(Physics.getTolerancesScale(), std::max(Linear.Limit * Scale, 0.01f)));
		}

		const FTwistConstraint& Twist = Profile.TwistLimit;
		const FConeConstraint& Cone = Profile.ConeLimit;
		Joint.setMotion(physx::PxD6Axis::eTWIST, ToPx(Twist.TwistMotion, Twist.TwistLimitDegrees));
		// UE Swing1 = 프레임 Z축 회전 = PhysX eSWING2, UE Swing2 = 프레임 Y축 회전 = PhysX eSWING1
		Joint.setMotion(physx::PxD6Axis::eSWING2, ToPx(Cone.Swing1Motion, Cone.Swing1LimitDegrees));
		Joint.setMotion(physx::PxD6Axis::eSWING1, ToPx(Cone.Swing2Motion, Cone.Swing2LimitDegrees));
		if (Twist.TwistMotion == ACM_Limited)
		{
			const float Angle = std::clamp(Twist.TwistLimitDegrees * DegToRad, MinAngle, MaxAngle);
			Joint.setTwistLimit(physx::PxJointAngularLimitPair(-Angle, Angle));
		}
		if (Cone.Swing1Motion == ACM_Limited || Cone.Swing2Motion == ACM_Limited)
			Joint.setSwingLimit(physx::PxJointLimitCone(SwingLimitAngle(Cone.Swing2Motion, Cone.Swing2LimitDegrees),
			                                            SwingLimitAngle(Cone.Swing1Motion, Cone.Swing1LimitDegrees)));

		const FLinearDriveConstraint& LinearDrive = Profile.LinearDrive;
		Joint.setDrive(physx::PxD6Drive::eX, MakeDrive(LinearDrive.XDrive, true));
		Joint.setDrive(physx::PxD6Drive::eY, MakeDrive(LinearDrive.YDrive, true));
		Joint.setDrive(physx::PxD6Drive::eZ, MakeDrive(LinearDrive.ZDrive, true));
		// SLERP 드라이브는 세 회전 축이 모두 잠기지 않았을 때만 쓸 수 있다.
		const bool bAngularFree = Joint.getMotion(physx::PxD6Axis::eTWIST) != physx::PxD6Motion::eLOCKED &&
		                          Joint.getMotion(physx::PxD6Axis::eSWING1) != physx::PxD6Motion::eLOCKED &&
		                          Joint.getMotion(physx::PxD6Axis::eSWING2) != physx::PxD6Motion::eLOCKED;
		Joint.setDrive(physx::PxD6Drive::eSLERP, MakeDrive(Profile.AngularDrive.SlerpDrive, bAngularFree));
		const physx::PxQuat Orientation = PhysicsMath::ToPx(Profile.AngularDrive.OrientationTarget.Quaternion().Normalized());
		Joint.setDrivePosition(physx::PxTransform(PhysicsMath::ToPx(LinearDrive.PositionTarget * Scale), Orientation));
		Joint.setDriveVelocity(physx::PxVec3(0.0f), physx::PxVec3(0.0f));

		Joint.setBreakForce(Profile.bLinearBreakable ? std::max(Profile.LinearBreakThreshold, 0.0f) : PX_MAX_F32,
		                    Profile.bAngularBreakable ? std::max(Profile.AngularBreakThreshold, 0.0f) : PX_MAX_F32);
		// PhysX 기본값은 연결된 두 액터의 충돌을 끈다.
		Joint.setConstraintFlag(physx::PxConstraintFlag::eCOLLISION_ENABLED, !Profile.bDisableCollision);
		Joint.setConstraintFlag(physx::PxConstraintFlag::ePROJECTION, Profile.bEnableProjection);
		Joint.setProjectionLinearTolerance(std::max(Profile.ProjectionLinearTolerance * Scale, 0.0f));
		Joint.setProjectionAngularTolerance(std::clamp(Profile.ProjectionAngularTolerance * DegToRad, 0.0f, 3.14159265f));
	}
}

FConstraintInstance::~FConstraintInstance()
{
	TermConstraint();
}

void FConstraintInstance::CopyConstraintParamsFrom(const FConstraintInstance& Other)
{
	JointName = Other.JointName;
	ConstraintBone1 = Other.ConstraintBone1;
	ConstraintBone2 = Other.ConstraintBone2;
	Pos1 = Other.Pos1;
	PriAxis1 = Other.PriAxis1;
	SecAxis1 = Other.SecAxis1;
	Pos2 = Other.Pos2;
	PriAxis2 = Other.PriAxis2;
	SecAxis2 = Other.SecAxis2;
	ProfileInstance = Other.ProfileInstance;
}

void FConstraintInstance::SetRefFrame(EConstraintFrame Frame, const FTransform& RefFrame)
{
	const FQuat Rotation = RefFrame.Rotation.Normalized();
	const FVector Pri = Rotation.RotateVector(FVector(1, 0, 0));
	const FVector Sec = Rotation.RotateVector(FVector(0, 1, 0));
	if (Frame == EConstraintFrame::Frame1)
	{
		Pos1 = RefFrame.Location;
		PriAxis1 = Pri;
		SecAxis1 = Sec;
	}
	else
	{
		Pos2 = RefFrame.Location;
		PriAxis2 = Pri;
		SecAxis2 = Sec;
	}
}

FTransform FConstraintInstance::GetRefFrame(EConstraintFrame Frame) const
{
	const bool bFirst = Frame == EConstraintFrame::Frame1;
	const physx::PxQuat Q = MakeFrameRotation(bFirst ? PriAxis1 : PriAxis2, bFirst ? SecAxis1 : SecAxis2);
	return FTransform(PhysicsMath::FromPx(Q), bFirst ? Pos1 : Pos2);
}

void FConstraintInstance::SetRefFramesFromWorld(const FTransform& JointWorld, const FBodyInstance* InBody1, const FBodyInstance* InBody2)
{
	const physx::PxTransform Joint(PhysicsMath::ToPx(JointWorld.Location), PhysicsMath::ToPx(JointWorld.Rotation.Normalized()));
	auto ToBodySpace = [&](const FBodyInstance* Body)
	{
		if (!Body || !Body->GetActor())
			return Joint;
		return Body->GetActor()->getGlobalPose().transformInv(Joint);
	};
	SetRefFrame(EConstraintFrame::Frame1, PhysicsMath::FromPxPose(ToBodySpace(InBody1)));
	SetRefFrame(EConstraintFrame::Frame2, PhysicsMath::FromPxPose(ToBodySpace(InBody2)));
}

bool FConstraintInstance::InitConstraint(FBodyInstance* InBody1, FBodyInstance* InBody2, float Scale)
{
	TermConstraint();
	if (!InBody1 || !InBody1->IsValidBodyInstance() || InBody1 == InBody2 || !std::isfinite(Scale) || Scale <= 0.0f)
		return false;
	FPhysScene* Scene = InBody1->GetOwnerScene();
	if (!Scene || !Scene->IsInitialized())
		return false;
	if (InBody2 && (!InBody2->IsValidBodyInstance() || InBody2->GetOwnerScene() != Scene))
		return false;
	const FTransform Ref1 = GetRefFrame(EConstraintFrame::Frame1);
	const FTransform Ref2 = GetRefFrame(EConstraintFrame::Frame2);
	const physx::PxTransform Frame1(PhysicsMath::ToPx(Ref1.Location * Scale), PhysicsMath::ToPx(Ref1.Rotation));
	const physx::PxTransform Frame2(PhysicsMath::ToPx(Ref2.Location * Scale), PhysicsMath::ToPx(Ref2.Rotation));
	if (!Frame1.isValid() || !Frame2.isValid())
		return false;
	// UE4와 같은 순서: PhysX actor0 = Body2(부모 또는 월드), actor1 = Body1(자식)
	ConstraintHandle = physx::PxD6JointCreate(*Scene->GetPhysics(), InBody2 ? InBody2->GetActor() : nullptr, Frame2, InBody1->GetActor(), Frame1);
	if (!ConstraintHandle)
		return false;
	ConstraintHandle->userData = this;
	Body1 = InBody1;
	Body2 = InBody2;
	OwnerScene = Scene;
	LastScale = Scale;
	ApplySettings(*ConstraintHandle, ProfileInstance, Scale);
	Scene->RegisterConstraint(*this);
	return true;
}

void FConstraintInstance::TermConstraint()
{
	if (ConstraintHandle)
	{
		if (OwnerScene)
			OwnerScene->UnregisterConstraint(*this);
		ConstraintHandle->userData = nullptr;
		ConstraintHandle->release();
		ConstraintHandle = nullptr;
	}
	Body1 = nullptr;
	Body2 = nullptr;
	OwnerScene = nullptr;
}

bool FConstraintInstance::IsBroken() const
{
	return ConstraintHandle && ConstraintHandle->getConstraintFlags().isSet(physx::PxConstraintFlag::eBROKEN);
}

bool FConstraintInstance::UpdateConstraintSettings()
{
	if (!ConstraintHandle)
		return true;
	ApplySettings(*ConstraintHandle, ProfileInstance, LastScale);
	return true;
}

FQuat FConstraintInstance::GetCurrentRelativeRotation() const
{
	if (!ConstraintHandle)
		return FQuat::Identity();
	return PhysicsMath::FromPx(ConstraintHandle->getRelativeTransform().q);
}

void FConstraintInstance::SetLinearXLimit(ELinearConstraintMotion Motion, float Limit)
{
	ProfileInstance.LinearLimit.XMotion = Motion;
	ProfileInstance.LinearLimit.Limit = Limit;
	UpdateConstraintSettings();
}
void FConstraintInstance::SetLinearYLimit(ELinearConstraintMotion Motion, float Limit)
{
	ProfileInstance.LinearLimit.YMotion = Motion;
	ProfileInstance.LinearLimit.Limit = Limit;
	UpdateConstraintSettings();
}
void FConstraintInstance::SetLinearZLimit(ELinearConstraintMotion Motion, float Limit)
{
	ProfileInstance.LinearLimit.ZMotion = Motion;
	ProfileInstance.LinearLimit.Limit = Limit;
	UpdateConstraintSettings();
}
void FConstraintInstance::SetAngularSwing1Limit(EAngularConstraintMotion Motion, float LimitDegrees)
{
	ProfileInstance.ConeLimit.Swing1Motion = Motion;
	ProfileInstance.ConeLimit.Swing1LimitDegrees = LimitDegrees;
	UpdateConstraintSettings();
}
void FConstraintInstance::SetAngularSwing2Limit(EAngularConstraintMotion Motion, float LimitDegrees)
{
	ProfileInstance.ConeLimit.Swing2Motion = Motion;
	ProfileInstance.ConeLimit.Swing2LimitDegrees = LimitDegrees;
	UpdateConstraintSettings();
}
void FConstraintInstance::SetAngularTwistLimit(EAngularConstraintMotion Motion, float LimitDegrees)
{
	ProfileInstance.TwistLimit.TwistMotion = Motion;
	ProfileInstance.TwistLimit.TwistLimitDegrees = LimitDegrees;
	UpdateConstraintSettings();
}
void FConstraintInstance::SetDisableCollision(bool bDisable)
{
	ProfileInstance.bDisableCollision = bDisable;
	UpdateConstraintSettings();
}
void FConstraintInstance::SetAngularOrientationTarget(const FQuat& Target)
{
	ProfileInstance.AngularDrive.OrientationTarget = Target.Normalized().ToFRotator();
	UpdateConstraintSettings();
}
void FConstraintInstance::SetLinearPositionTarget(const FVector& Target)
{
	ProfileInstance.LinearDrive.PositionTarget = Target;
	UpdateConstraintSettings();
}
