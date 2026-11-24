#include "EnginePCH.h"
#include "PrimitiveComponent.h"
#include "../Render/Renderer.h"
#include "Asset/AssetManager.h"
#include "Render/RenderCommand.h"

#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/Scene.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysScene.h"
#include "Physics/PhysicsMath.h"
#include "Physics/LegacyPhysicsSettings.h"
#include <PxPhysicsAPI.h>
#include <cmath>

namespace
{
	FString PrimitiveTypeToString(EPrimitiveType Type)
	{
		switch (Type)
		{
		case EPrimitiveType::Sphere:
			return "Sphere";
			break;
		case EPrimitiveType::Cube:
			return "Cube";
			break;
		case EPrimitiveType::Cone:
			return "Cone";
			break;
		case EPrimitiveType::Plane:
			return "Plane";
			break;
		default:
			return "";
			break;
		}
	}
}

UPrimitiveComponent::UPrimitiveComponent()
{
	// SetMaterial(UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial"));
}

UPrimitiveComponent::~UPrimitiveComponent()
{
	DestroyPhysicsState();
}

void UPrimitiveComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UPrimitiveComponent::OnUnregister()
{
	Super::OnUnregister();
	// 삭제/등록 해제된 도형을 상대의 OverlapInfos에 남기지 않는다.
	while (!OverlapInfos.IsEmpty())
	{
		UPrimitiveComponent* Other = OverlapInfos[OverlapInfos.Num() - 1].OtherComp;
		EndComponentOverlap(Other);
		if (Other)
			Other->EndComponentOverlap(this);
	}
}

void UPrimitiveComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext)
{
}

bool UPrimitiveComponent::LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit)
{
	const FStaticMeshData* Mesh = GetMeshData();
	return Mesh && TraceMesh(WorldRay, *Mesh, GetWorldMatrix(), OutHit);
}

FPrimitiveSceneProxy* UPrimitiveComponent::CreateSceneProxy()
{
	return new FPrimitiveSceneProxy(this);
}

void UPrimitiveComponent::MarkRenderStateDirty()
{
	if (SceneProxy)
		SceneProxy->GetScene()->MarkRenderStateDirty(SceneProxy);
}

bool UPrimitiveComponent::LineTraceComponentLocal(const FRay& LocalRay, float& OutT)
{
	const FStaticMeshData* Mesh = GetMeshData();
	return Mesh && TraceMeshLocal(LocalRay, *Mesh, OutT);
}

void UPrimitiveComponent::OnTransformDirty()
{
	UpdatePhysicsTransform();
	if (SceneProxy)
		SceneProxy->GetScene()->MarkDirty(SceneProxy);
}

bool UPrimitiveComponent::MoveComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit)
{
	return Super::MoveComponent(Delta, NewRotation, bSweep, Hit);
}

void UPrimitiveComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	if (Property.Name == "bUseDefaultCollision")
		ApplyDefaultCollision();
	if (Property.Name == "BodyInstance" || Property.Name == "Mobility" || Property.Name == "bUseDefaultCollision")
		RefreshPhysicsSettings();
	if (Property.Name == "bCastShadow" ||
	    Property.Name == "bReceiveShadow" ||
	    Property.Name == "bReceivesDecals")
	{
		MarkRenderStateDirty();
	}
}

void UPrimitiveComponent::SetCastShadow(bool NewValue)
{
	if (bCastShadow != NewValue)
	{
		bCastShadow = NewValue;
	}
}

void UPrimitiveComponent::SetReceiveShadow(bool NewValue)
{
	if (bReceiveShadow != NewValue)
	{
		bReceiveShadow = NewValue;
	}
}

void UPrimitiveComponent::CreateRenderState()
{
	Super::CreateRenderState();
	GetWorld()->GetScene().AddPrimitive(this);
}

void UPrimitiveComponent::DestroyRenderState()
{
	GetWorld()->GetScene().RemovePrimitive(this);
	Super::DestroyRenderState();
}

bool UPrimitiveComponent::TraceMesh(const FRay& WorldRay, const FStaticMeshData& Mesh, const FMatrix& WorldMatrix, FHitResult& OutResult)
{
	FBox MeshBox = Mesh.AABB.GetWorldAABB(WorldMatrix);
	float BoxT;
	if (!RayIntersectsAABB(WorldRay, MeshBox.Min, MeshBox.Max, BoxT))
	{
		return false;
	}

	FRay LocalRay = ToLocalRay(WorldRay, WorldMatrix);
	float T = FLT_MAX;

	if (!RayIntersectsMesh(LocalRay, Mesh, T))
		return false;

	OutResult.HitComponent = this;
	OutResult.Distance = T;
	OutResult.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;

	return true;
}

bool UPrimitiveComponent::TraceMeshLocal(const FRay& LocalRay, const FStaticMeshData& Mesh, float& OutT)
{
	return RayIntersectsMesh(LocalRay, Mesh, OutT);
}

bool UPrimitiveComponent::IsOverlappingComponent(const UPrimitiveComponent* Other) const
{
	if (!Other)
		return false;

	for (const FOverlapInfo& Info : OverlapInfos)
	{
		if (Info.OtherComp == Other)
			return true;
	}

	return false;
}

void UPrimitiveComponent::BeginComponentOverlap(UPrimitiveComponent* OtherComp)
{
	if (!OtherComp || OtherComp == this || IsOverlappingComponent(OtherComp))
	{
		return;
	}

	OverlapInfos.Add({OtherComp});

	AActor* Owner = GetOwner();
	AActor* OtherActor = OtherComp->GetOwner();

	OnComponentBeginOverlap.Broadcast(this, OtherActor, OtherComp);
	if (Owner && OtherActor)
	{
		Owner->NotifyActorBeginOverlap(OtherActor);
	}
}

void UPrimitiveComponent::EndComponentOverlap(UPrimitiveComponent* OtherComp)
{
	if (!OtherComp)
		return;

	bool bRemoved = false;

	for (int32 Index = 0; Index < OverlapInfos.Num(); ++Index)
	{
		if (OverlapInfos[Index].OtherComp == OtherComp)
		{
			OverlapInfos.RemoveAt(Index, 1);
			bRemoved = true;
			break;
		}
	}

	if (!bRemoved)
		return;

	AActor* Owner = GetOwner();
	AActor* OtherActor = OtherComp->GetOwner();

	OnComponentEndOverlap.Broadcast(this, OtherActor, OtherComp);

	if (Owner && OtherActor)
	{
		Owner->NotifyActorEndOverlap(OtherActor);
	}
}

void UPrimitiveComponent::DispatchHit(UPrimitiveComponent* OtherComp)
{
	FHitResult Hit;
	Hit.bBlockingHit = true;
	Hit.HitComponent = OtherComp;
	DispatchHit(OtherComp, FVector::ZeroVector, Hit);
}

void UPrimitiveComponent::DispatchHit(UPrimitiveComponent* OtherComp, const FVector& NormalImpulse, const FHitResult& Hit)
{
	if (!OtherComp)
		return;

	AActor* Owner = GetOwner();
	AActor* OtherActor = OtherComp->GetOwner();

	OnComponentHit.Broadcast(this, OtherActor, OtherComp, NormalImpulse, Hit);

	if (Owner && OtherActor)
	{
		Owner->NotifyHit(OtherActor);
	}
}

void UPrimitiveComponent::SetPhysicsEnabled(bool bEnabled)
{
	SetCollisionEnabled(bEnabled ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
}

void UPrimitiveComponent::SetCollisionEnabled(ECollisionEnabled Value)
{
	// 직접 정한 값은 메시 에셋의 기본 충돌보다 우선한다. 채널·응답은 에셋 값을 이어받는다.
	ApplyDefaultCollision();
	bUseDefaultCollision = false;
	if (BodyInstance.CollisionEnabled == Value)
		return;
	BodyInstance.CollisionEnabled = Value;
	BodyInstance.InvalidateCollisionProfileName();
	RefreshPhysicsSettings();
}

void UPrimitiveComponent::SetCollisionProfileName(const FName& ProfileName)
{
	ApplyDefaultCollision();
	bUseDefaultCollision = false;
	if (BodyInstance.CollisionProfileName == ProfileName && !BodyInstance.IsCustomCollisionProfile())
		return;
	if (!BodyInstance.SetCollisionProfileName(ProfileName))
	{
		KRS_LOG(Warning, "{}: unknown collision profile {}", GetName(), ProfileName.ToString());
		return;
	}
	RefreshPhysicsSettings();
}

const FName& UPrimitiveComponent::GetCollisionProfileName() const
{
	if (bUseDefaultCollision)
		if (const UBodySetup* Setup = GetBodySetup())
			return Setup->DefaultInstance.CollisionProfileName;
	return BodyInstance.CollisionProfileName;
}

void UPrimitiveComponent::SetMobility(EComponentMobility Value)
{
	if (Mobility == Value)
		return;
	const auto Previous = Mobility;
	Mobility = Value;
	if (BodyInstance.IsValidBodyInstance() && !RecreatePhysicsState())
		Mobility = Previous;
}

void UPrimitiveComponent::RefreshPhysicsSettings()
{
	if (!ShouldCreatePhysicsState())
	{
		DestroyPhysicsState();
		return;
	}
	if (!BodyInstance.IsValidBodyInstance() || !BodyInstance.UpdateBodySettings())
		if (!RecreatePhysicsState() && BodyInstance.IsValidBodyInstance())
			Mobility = PhysicsMobility;
}

void UPrimitiveComponent::SetSimulatePhysics(bool bSimulate)
{
	if (BodyInstance.bSimulatePhysics == bSimulate)
		return;
	BodyInstance.bSimulatePhysics = bSimulate;
	RefreshPhysicsSettings();
}

bool UPrimitiveComponent::IsSupportedPhysicsRoot() const
{
	return GetOwner() && GetOwner()->GetRootComponent() == this && !GetAttachParent();
}

bool UPrimitiveComponent::CanHavePhysicsBody() const
{
	if (IsSupportedPhysicsRoot())
		return true;
	if (!GetOwner())
		return false;
	// 현재 바디가 아니라 지금 설정이 요구하는 종류로 판단한다 (시뮬레이션으로 바꾸는 중이면 거부).
	// 바디가 있으면 그 BodySetup을 그대로 쓴다. 도형의 GetBodySetup은 매번 형상을 다시 채운다.
	const UBodySetup* Setup = BodyInstance.IsValidBodyInstance() ? BodyInstance.GetBodySetup() : GetBodySetup();
	return BodyInstance.ResolveBodyType(Setup, this) != EPhysicsBodyType::Simulated;
}

FTransform UPrimitiveComponent::GetPhysicsBodyTransform() const
{
	return FTransform(GetWorldRotation().Quaternion(), GetWorldLocation(), GetWorldScale3D());
}

bool UPrimitiveComponent::ShouldCreatePhysicsState() const
{
	const UWorld* World = GetWorld();
	if (GetCollisionEnabled() == ECollisionEnabled::NoCollision || !World || !World->IsGameWorld() || !World->HasBegunPlay())
		return false;
	const UBodySetup* Setup = GetBodySetup();
	return Setup && Setup->AggGeom.GetElementCount() > 0 && CanHavePhysicsBody();
}

bool UPrimitiveComponent::OnCreatePhysicsState()
{
	ApplyDefaultCollision();
	FPhysScene* Scene = GetWorld()->GetPhysicsScene();
	if (!Scene || !BodyInstance.RecreateBody(GetBodySetup(), GetPhysicsBodyTransform(), this, *Scene))
	{
		KRS_LOG(Error, "[Physics] {}: body creation failed; check shared collision geometry, scale and material.", GetName());
		return false;
	}
	PhysicsScale = GetWorldScale3D();
	PhysicsMobility = Mobility;
	ComponentVelocity = FVector::ZeroVector;
	return true;
}

void UPrimitiveComponent::OnDestroyPhysicsState()
{
	// Overlap은 UWorld::UpdatePhysicsOverlaps가 바디 기준으로 관리한다.
	// 바디가 사라지면 다음 갱신에서 이 컴포넌트를 찾지 못하므로 여기서 끝낸다.
	if (BodyInstance.IsValidBodyInstance())
		while (!OverlapInfos.IsEmpty())
		{
			UPrimitiveComponent* Other = OverlapInfos[OverlapInfos.Num() - 1].OtherComp;
			EndComponentOverlap(Other);
			if (Other)
				Other->EndComponentOverlap(this);
		}
	BodyInstance.TermBody();
	ComponentVelocity = FVector::ZeroVector;
}

void UPrimitiveComponent::Serialize(FStructuredArchive::FRecord Record)
{
	FArchive& Ar = Record.GetUnderlyingArchive();
	bool bLegacy = false;
	bool bLegacySimulate = true;
	bool bOldSettings = false;
	TOptional<float> LegacyMassInKg;
	FLegacyBodySettings Legacy;
	TOptional<bool> LegacyPhysicsEnabled;
	bool bHasMobility = false;
	bool bHasCollisionEnabled = false;
	if (Ar.IsLoading() && !Ar.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate))
	{
		if (auto Slot = Record.TryEnterField("Properties", false))
		{
			auto Properties = Slot.GetValue().EnterRecord();
			if (auto CurrentMobility = Properties.TryEnterField("Mobility", false))
			{
				FString Value;
				CurrentMobility.GetValue() << Value;
				bHasMobility = true;
			}
			if (auto Enabled = Properties.TryEnterField("bPhysicsEnabled", false))
			{
				bool Value = false;
				Enabled.GetValue() << Value;
				LegacyPhysicsEnabled = Value;
			}
			if (auto Current = Properties.TryEnterField("BodyInstance", false))
			{
				auto Body = Current.GetValue().EnterRecord();
				if (auto CurrentCollision = Body.TryEnterField("CollisionEnabled", false))
				{
					FString Value;
					CurrentCollision.GetValue() << Value;
					bHasCollisionEnabled = true;
				}
				if (auto Mass = Body.TryEnterField("MassInKgOverride", false))
				{
					float Value = 1.0f;
					Mass.GetValue() << Value;
				}
				else if (auto Mass = Body.TryEnterField("MassInKg", false))
				{
					float Value = 1.0f;
					Mass.GetValue() << Value;
					LegacyMassInKg = Value;
				}
			}
			else
			{
				bLegacy = true;
				if (auto Old = Properties.TryEnterField("PhysicsSettings", false))
				{
					FLegacyBodySettings::StaticStruct()->SerializeTaggedProperties(Old.GetValue(), &Legacy);
					bOldSettings = true;
				}
				else if (auto Old = Properties.TryEnterField("bSimulatePhysics", false))
					Old.GetValue() << bLegacySimulate;
			}
		}
	}
	Super::Serialize(Record);
	if (Ar.IsLoading() && !Ar.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate))
	{
		if (!bHasCollisionEnabled && LegacyPhysicsEnabled.IsSet())
			BodyInstance.CollisionEnabled = LegacyPhysicsEnabled.GetValue() ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision;
		if (!bHasMobility && !BodyInstance.bSimulatePhysics)
			Mobility = EComponentMobility::Static;
	}
	if (LegacyMassInKg.IsSet())
		BodyInstance.MassInKgOverride = LegacyMassInKg.GetValue();
	if (Ar.IsLoading() && bLegacy)
	{
		BodyInstance.bSimulatePhysics = bOldSettings ? Legacy.Type == ELegacyPhysicsBodyType::Dynamic : bLegacySimulate;
		if (!bHasMobility && !BodyInstance.bSimulatePhysics)
			Mobility = EComponentMobility::Static;
		if (bOldSettings)
		{
			BodyInstance.MassInKgOverride = Legacy.MassKg;
			auto* Material = NewObject<UPhysicalMaterial>(this);
			Material->StaticFriction = Legacy.Material.StaticFriction;
			Material->Friction = Legacy.Material.DynamicFriction;
			Material->Restitution = Legacy.Material.Restitution;
			BodyInstance.PhysMaterialOverride = Material;
			if (UBodySetup* Setup = GetBodySetup(); Setup &&
			                                        (!Legacy.Shape.bFitMeshBounds || Legacy.Shape.Type == ELegacyPhysicsShapeType::Sphere))
			{
				Setup->AggGeom.EmptyElements();
				if (Legacy.Shape.Type == ELegacyPhysicsShapeType::Sphere)
				{
					if (Legacy.Shape.bFitMeshBounds && GetMeshData())
						Setup->FitSphere(*GetMeshData());
					else
					{
						FKSphereElem Elem;
						Elem.Center = Legacy.Shape.Center;
						Elem.Radius = Legacy.Shape.SphereRadius;
						Setup->AggGeom.SphereElems.Add(Elem);
					}
				}
				else
				{
					FKBoxElem Elem;
					Elem.Center = Legacy.Shape.Center;
					Elem.X = Legacy.Shape.BoxHalfExtent.X * 2;
					Elem.Y = Legacy.Shape.BoxHalfExtent.Y * 2;
					Elem.Z = Legacy.Shape.BoxHalfExtent.Z * 2;
					Setup->AggGeom.BoxElems.Add(Elem);
				}
				Setup->InvalidatePhysicsData();
				KRS_LOG(Warning, "[Physics] {}: legacy collision migrated to shared mesh BodySetup; instances of that mesh now share geometry.", GetName());
			}
		}
	}
	if (Ar.IsLoading() && (!std::isfinite(BodyInstance.MassInKgOverride) || BodyInstance.MassInKgOverride <= 0))
		BodyInstance.MassInKgOverride = 1;
	// Properties are serialized for the most-derived class, so the mesh (and its BodySetup) is
	// already set here. Sync once on load so the saved copy never shadows the asset's collision.
	if (Ar.IsLoading())
	{
		// 저장된 프리셋 이름이 있으면 그 정의로 값을 다시 채운다 (UE LoadProfileData).
		BodyInstance.LoadProfileData();
		ApplyDefaultCollision();
	}
}

void UPrimitiveComponent::SyncComponentToRBPhysics()
{
	if (!IsRegistered() || !BodyInstance.IsInScene())
		return;
	if (!CanHavePhysicsBody())
	{
		DestroyPhysicsState();
		return;
	}
	// 시뮬레이션 바디만 결과를 컴포넌트로 되돌린다. CanHavePhysicsBody가 루트임을 보장한다.
	if (!BodyInstance.IsInstanceSimulatingPhysics())
		return;
	auto* Dynamic = BodyInstance.GetActor()->is<physx::PxRigidDynamic>();
	if (!Dynamic)
		return;
	const auto Pose = Dynamic->getGlobalPose();
	ComponentVelocity = PhysicsMath::FromPx(Dynamic->getLinearVelocity());
	FTransform Updated = GetTransform();
	Updated.Location = PhysicsMath::FromPx(Pose.p);
	Updated.Rotation = PhysicsMath::FromPx(Pose.q);
	// 스케일은 생성 시 값 유지. 기존 dirty 경로가 자식·렌더 프록시·바운드도 갱신한다.
	Updated.Scale = PhysicsScale;
	bApplyingPhysicsTransform = true;
	SetTransform(Updated);
	bApplyingPhysicsTransform = false;
}

void UPrimitiveComponent::UpdatePhysicsTransform()
{
	if (bApplyingPhysicsTransform || !BodyInstance.IsValidBodyInstance())
		return;
	if (!CanHavePhysicsBody())
	{
		DestroyPhysicsState();
		return;
	}
	const bool bRoot = IsSupportedPhysicsRoot();
	const FVector WorldScale = GetWorldScale3D();
	if (WorldScale.X != PhysicsScale.X || WorldScale.Y != PhysicsScale.Y || WorldScale.Z != PhysicsScale.Z)
	{
		// 도형은 크기를 다시 구워 바디를 새로 만든다.
		if (BakesScaleIntoBodySetup())
		{
			RecreatePhysicsState();
			return;
		}
		// 메시 바디의 실행 중 크기 변경은 지원하지 않는다. 보이는 메시와 충돌 크기를 일치시킨다.
		if (bRoot)
			Transform.Scale = PhysicsScale;
		KRS_LOG(Warning, "[Physics] {}: changing scale while a physics body exists is unsupported.", GetName());
	}
	const FTransform BodyTransform = GetPhysicsBodyTransform();
	const auto Position = PhysicsMath::ToPx(BodyTransform.Location);
	const auto Rotation = PhysicsMath::ToPx(BodyTransform.Rotation);
	if (!Position.isFinite() || !Rotation.isFinite() || !Rotation.isUnit())
		return;
	if (BodyInstance.GetBodyType() == EPhysicsBodyType::Static)
	{
		// Static 바디는 움직이지 않는다. 루트는 컴포넌트를 바디 위치로 되돌린다.
		if (bRoot)
		{
			const auto Pose = BodyInstance.GetActor()->getGlobalPose();
			Transform.Location = PhysicsMath::FromPx(Pose.p);
			Transform.Rotation = PhysicsMath::FromPx(Pose.q);
		}
	}
	else
		BodyInstance.SetBodyTransform(BodyTransform);
}


ECollisionEnabled UPrimitiveComponent::GetCollisionEnabled() const
{
	if (bUseDefaultCollision)
		if (const UBodySetup* Setup = GetBodySetup())
			return Setup->DefaultInstance.CollisionEnabled;
	return BodyInstance.CollisionEnabled;
}

void UPrimitiveComponent::ApplyDefaultCollision()
{
	// UE의 bUseDefaultCollision처럼 충돌 설정(활성화·채널·응답)만 메시 에셋을 따른다.
	// 시뮬레이션·질량·머티리얼·Hit 알림 등은 컴포넌트 값.
	if (bUseDefaultCollision)
		if (const UBodySetup* Setup = GetBodySetup())
		{
			BodyInstance.CollisionEnabled = Setup->DefaultInstance.CollisionEnabled;
			BodyInstance.CollisionProfileName = Setup->DefaultInstance.CollisionProfileName;
			BodyInstance.ObjectType = Setup->DefaultInstance.ObjectType;
			BodyInstance.CollisionResponses = Setup->DefaultInstance.CollisionResponses;
		}
}

ECollisionChannel UPrimitiveComponent::GetCollisionObjectType() const
{
	if (bUseDefaultCollision)
		if (const UBodySetup* Setup = GetBodySetup())
			return Setup->DefaultInstance.ObjectType;
	return BodyInstance.ObjectType;
}

ECollisionResponse UPrimitiveComponent::GetCollisionResponseToChannel(ECollisionChannel Channel) const
{
	if (bUseDefaultCollision)
		if (const UBodySetup* Setup = GetBodySetup())
			return Setup->DefaultInstance.GetResponseToChannel(Channel);
	return BodyInstance.GetResponseToChannel(Channel);
}

void UPrimitiveComponent::SetCollisionObjectType(ECollisionChannel Channel)
{
	if (Channel == ECC_MAX)
		return;
	// 에셋 값을 따르던 중이면 현재 값을 복사해 둔 뒤 이 컴포넌트만 바꾼다.
	ApplyDefaultCollision();
	bUseDefaultCollision = false;
	if (BodyInstance.ObjectType == Channel)
		return;
	BodyInstance.SetObjectType(Channel);
	RefreshPhysicsSettings();
}

void UPrimitiveComponent::SetCollisionResponseToChannel(ECollisionChannel Channel, ECollisionResponse Response)
{
	ApplyDefaultCollision();
	bUseDefaultCollision = false;
	if (!BodyInstance.CollisionResponses.SetResponse(Channel, Response))
		return;
	BodyInstance.InvalidateCollisionProfileName();
	RefreshPhysicsSettings();
}

void UPrimitiveComponent::SetCollisionResponseToAllChannels(ECollisionResponse Response)
{
	ApplyDefaultCollision();
	bUseDefaultCollision = false;
	if (!BodyInstance.CollisionResponses.SetAllChannels(Response))
		return;
	BodyInstance.InvalidateCollisionProfileName();
	RefreshPhysicsSettings();
}

void UPrimitiveComponent::SetNotifyRigidBodyCollision(bool bNewNotify)
{
	if (BodyInstance.bNotifyRigidBodyCollision == bNewNotify)
		return;
	BodyInstance.bNotifyRigidBodyCollision = bNewNotify;
	RefreshPhysicsSettings();
}

void UPrimitiveComponent::SetUseDefaultCollision(bool bUseDefault)
{
	if (bUseDefaultCollision == bUseDefault)
		return;
	bUseDefaultCollision = bUseDefault;
	ApplyDefaultCollision();
	RefreshPhysicsSettings();
}

void UPrimitiveComponent::SetPhysMaterialOverride(UPhysicalMaterial* Material)
{
    if (Material && !Material->IsValidMaterial()) return;
    if (BodyInstance.PhysMaterialOverride == Material) return;
    BodyInstance.PhysMaterialOverride = Material;
    RefreshPhysicsSettings();
}

FBodyInstance* UPrimitiveComponent::FindBodyInstance(const FName&)
{
	return BodyInstance.IsValidBodyInstance() ? &BodyInstance : nullptr;
}
