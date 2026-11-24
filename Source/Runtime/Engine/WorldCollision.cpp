#include "EnginePCH.h"
#include "Engine/World.h"
#include "Physics/PhysScene.h"
#include "Physics/BodyInstance.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsFilter.h"
#include "Physics/PhysicsMath.h"
#include "Physics/PhysicsStats.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"

#include <PxPhysicsAPI.h>
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <vector>

// UE WorldCollision.cpp / PhysXCollision.cpp 대응. 채널 기반 Scene Query와 물리 바디 Overlap 이벤트.

namespace
{
	constexpr physx::PxU32 MaxQueryHits = 256;
	constexpr float MinQueryLength = 1.e-4f;

	FBodyInstance* GetBody(const physx::PxRigidActor* Actor)
	{
		return Actor ? static_cast<FBodyInstance*>(Actor->userData) : nullptr;
	}

	bool IsIgnored(const FCollisionQueryParams& Params, const UPrimitiveComponent* Component)
	{
		if (!Component || !Component->IsRegistered())
			return true;
		if (Params.IgnoredComponents.Contains(Component))
			return true;
		const AActor* Owner = Component->GetOwner();
		return Owner && Params.IgnoredActors.Contains(Owner);
	}

	// 채널 쿼리. 각 형상의 쿼리 필터 데이터에서 Trace 채널에 대한 응답을 읽는다.
	class FChannelQueryFilter final : public physx::PxQueryFilterCallback
	{
	public:
		FChannelQueryFilter(const FPhysScene& InScene, ECollisionChannel InChannel, const FCollisionQueryParams& InParams, bool bInMulti, bool bInOverlap)
		    : Scene(InScene), Channel(InChannel), Params(InParams), bMulti(bInMulti), bOverlap(bInOverlap)
		{
		}

		physx::PxQueryHitType::Enum preFilter(const physx::PxFilterData&, const physx::PxShape* Shape, const physx::PxRigidActor* Actor, physx::PxHitFlags&) override
		{
			const FBodyInstance* Body = GetBody(Actor);
			if (!Scene.IsRegisteredBody(Body) || IsIgnored(Params, Body->GetOwnerComponent()))
				return physx::PxQueryHitType::eNONE;
			return ToHitType(PhysicsFilter::GetResponse(Shape->getQueryFilterData(), Channel));
		}

		// bFindInitialOverlaps가 꺼져 있을 때만 호출된다(ePOSTFILTER). 시작 위치에서 겹친 바디를 건너뛴다.
		physx::PxQueryHitType::Enum postFilter(const physx::PxFilterData&, const physx::PxQueryHit& Hit) override
		{
			const auto& LocationHit = static_cast<const physx::PxLocationHit&>(Hit);
			if (LocationHit.hadInitialOverlap())
				return physx::PxQueryHitType::eNONE;
			return ToHitType(PhysicsFilter::GetResponse(Hit.shape->getQueryFilterData(), Channel));
		}

	private:
		physx::PxQueryHitType::Enum ToHitType(ECollisionResponse Response) const
		{
			if (Response == ECR_Block)
				return bOverlap ? physx::PxQueryHitType::eTOUCH : physx::PxQueryHitType::eBLOCK;
			if (Response == ECR_Overlap && (bMulti || bOverlap))
				return physx::PxQueryHitType::eTOUCH;
			return physx::PxQueryHitType::eNONE;
		}

		const FPhysScene& Scene;
		ECollisionChannel Channel;
		const FCollisionQueryParams& Params;
		bool bMulti;
		bool bOverlap;
	};

	physx::PxQueryFilterData MakeQueryFilterData(const FCollisionQueryParams& Params, bool bOverlap)
	{
		// 필터 데이터의 단어가 모두 0이면 PhysX 내장 필터는 건너뛰고 preFilter만 판정한다.
		physx::PxQueryFilterData Data;
		Data.flags = physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC | physx::PxQueryFlag::ePREFILTER;
		if (bOverlap)
			Data.flags |= physx::PxQueryFlag::eNO_BLOCK;
		else if (!Params.bFindInitialOverlaps)
			Data.flags |= physx::PxQueryFlag::ePOSTFILTER;
		return Data;
	}

	FHitResult MakeHitResult(const physx::PxLocationHit& Hit, const FVector& Start, const FVector& End, bool bBlocking, bool bSweep)
	{
		FHitResult Out;
		Out.Init(Start, End);
		const FVector Delta = End - Start;
		const float Length = Delta.Size();
		const FVector Direction = Length > MinQueryLength ? Delta / Length : FVector::ZeroVector;
		const FBodyInstance* Body = GetBody(Hit.actor);
		Out.HitComponent = Body ? Body->GetOwnerComponent() : nullptr;
		if (Body && Body->GetBodySetup())
			Out.BoneName = Body->GetBodySetup()->BoneName;
		Out.FaceIndex = Hit.faceIndex == 0xFFFFFFFFu ? -1 : static_cast<int32>(Hit.faceIndex);
		Out.bBlockingHit = bBlocking;
		Out.bStartPenetrating = Hit.hadInitialOverlap();
		Out.ImpactPoint = PhysicsMath::FromPx(Hit.position);
		Out.ImpactNormal = PhysicsMath::FromPx(Hit.normal);
		Out.Normal = Out.ImpactNormal;
		if (Out.bStartPenetrating)
		{
			// eMTD: distance는 -(관통 깊이), normal은 빠져나갈 방향이다.
			Out.Distance = 0.0f;
			Out.Time = 0.0f;
			Out.PenetrationDepth = std::max(0.0f, -Hit.distance);
			Out.Location = Start;
		}
		else
		{
			Out.Distance = Hit.distance;
			Out.Time = Length > MinQueryLength ? Hit.distance / Length : 0.0f;
			Out.PenetrationDepth = 0.0f;
			// Sweep의 Location은 도형 중심이 멈춘 위치, Raycast는 맞은 점이다 (UE와 같다).
			Out.Location = bSweep ? Start + Direction * Hit.distance : Out.ImpactPoint;
		}
		return Out;
	}

	void SortTouchesThenBlock(TArray<FHitResult>& Hits, bool bHasBlock)
	{
		const int32 TouchCount = bHasBlock ? Hits.Num() - 1 : Hits.Num();
		if (TouchCount > 1)
			std::sort(Hits.begin(), Hits.begin() + TouchCount, [](const FHitResult& A, const FHitResult& B) { return A.Distance < B.Distance; });
	}

	// 쿼리 도형. Capsule은 UE처럼 로컬 Z축이고 HalfHeight는 반구를 포함한다. PhysX Capsule은 X축이다.
	bool MakeGeometry(const FCollisionShape& Shape, const FQuat& Rot, physx::PxGeometryHolder& OutGeometry, physx::PxQuat& OutRotation)
	{
		physx::PxQuat Rotation = PhysicsMath::ToPx(Rot);
		if (!Rotation.isFinite() || Rotation.magnitudeSquared() < 1.e-6f)
			return false;
		Rotation.normalize();
		switch (Shape.ShapeType)
		{
		case ECollisionShape::Box:
		{
			const physx::PxBoxGeometry Box(Shape.HalfExtent.X, Shape.HalfExtent.Y, Shape.HalfExtent.Z);
			if (!Box.isValid())
				return false;
			OutGeometry.storeAny(Box);
			break;
		}
		case ECollisionShape::Sphere:
		{
			const physx::PxSphereGeometry Sphere(Shape.Radius);
			if (!Sphere.isValid())
				return false;
			OutGeometry.storeAny(Sphere);
			break;
		}
		case ECollisionShape::Capsule:
		{
			const physx::PxCapsuleGeometry Capsule(Shape.Radius, std::max(0.0f, Shape.HalfHeight - Shape.Radius));
			if (!Capsule.isValid())
				return false;
			OutGeometry.storeAny(Capsule);
			Rotation = Rotation * physx::PxQuat(-physx::PxHalfPi, physx::PxVec3(0, 1, 0));
			break;
		}
		default:
			return false;
		}
		OutRotation = Rotation;
		return true;
	}

	bool IsUsableSweepShape(const FCollisionShape& Shape)
	{
		// 반지름 0 구는 선으로 본다 (UE와 같다).
		return !Shape.IsLine() && !(Shape.ShapeType == ECollisionShape::Sphere && Shape.Radius <= 0.0f);
	}

	bool CanGeneratePhysicsOverlap(const UPrimitiveComponent* Component)
	{
		const AActor* Owner = Component ? Component->GetOwner() : nullptr;
		return Owner && Component->IsRegistered() && Component->bGenerateOverlapEvents && Component->IsQueryCollisionEnabled() &&
		       Owner->GetActorEnableCollision();
	}

	// 컴포넌트 쌍을 UUID 순서로 정규화한 키
	struct FComponentPair
	{
		UPrimitiveComponent* A = nullptr;
		UPrimitiveComponent* B = nullptr;
		bool operator==(const FComponentPair& Other) const { return A == Other.A && B == Other.B; }
	};
	FComponentPair MakePair(UPrimitiveComponent* X, UPrimitiveComponent* Y)
	{
		return X->GetUUID() < Y->GetUUID() ? FComponentPair{X, Y} : FComponentPair{Y, X};
	}
	struct FComponentPairHash
	{
		size_t operator()(const FComponentPair& Pair) const
		{
			return std::hash<const void*>()(Pair.A) ^ (std::hash<const void*>()(Pair.B) << 1);
		}
	};

	// 물리 Overlap 이벤트용: 대상 바디의 형상과 상호 Overlap 응답인 다른 바디만 받는다.
	class FBodyOverlapFilter final : public physx::PxQueryFilterCallback
	{
	public:
		FBodyOverlapFilter(const FPhysScene& InScene, const FBodyInstance& InSelf, const physx::PxFilterData& InSelfData)
		    : Scene(InScene), Self(InSelf), SelfData(InSelfData)
		{
		}
		physx::PxQueryHitType::Enum preFilter(const physx::PxFilterData&, const physx::PxShape* Shape, const physx::PxRigidActor* Actor, physx::PxHitFlags&) override
		{
			const FBodyInstance* Body = GetBody(Actor);
			// 같은 컴포넌트의 다른 바디(스켈레탈 본끼리)도 자기 자신으로 본다.
			if (Body == &Self || !Scene.IsRegisteredBody(Body) || Body->GetOwnerComponent() == Self.GetOwnerComponent())
				return physx::PxQueryHitType::eNONE;
			return PhysicsFilter::GetMutualResponse(SelfData, Shape->getQueryFilterData()) == ECR_Overlap
			           ? physx::PxQueryHitType::eTOUCH
			           : physx::PxQueryHitType::eNONE;
		}
		physx::PxQueryHitType::Enum postFilter(const physx::PxFilterData&, const physx::PxQueryHit&) override
		{
			return physx::PxQueryHitType::eTOUCH;
		}

	private:
		const FPhysScene& Scene;
		const FBodyInstance& Self;
		physx::PxFilterData SelfData;
	};
}

bool UWorld::LineTraceSingleByChannel(FHitResult& OutHit, const FVector& Start, const FVector& End, ECollisionChannel TraceChannel,
                                      const FCollisionQueryParams& Params) const
{
	OutHit = FHitResult();
	OutHit.Init(Start, End);
	const FPhysScene* Phys = PhysicsScene.get();
	const FVector Delta = End - Start;
	const float Length = Delta.Size();
	if (!Phys || !Phys->IsInitialized() || TraceChannel >= ECC_MAX || !std::isfinite(Length) || Length < MinQueryLength)
		return false;
	FChannelQueryFilter Filter(*Phys, TraceChannel, Params, false, false);
	physx::PxRaycastBuffer Buffer;
	Phys->GetPhysXScene()->raycast(PhysicsMath::ToPx(Start), PhysicsMath::ToPx(Delta / Length), Length, Buffer, physx::PxHitFlag::eDEFAULT,
	                               MakeQueryFilterData(Params, false), &Filter);
	if (!Buffer.hasBlock)
		return false;
	OutHit = MakeHitResult(Buffer.block, Start, End, true, false);
	return true;
}

bool UWorld::LineTraceMultiByChannel(TArray<FHitResult>& OutHits, const FVector& Start, const FVector& End, ECollisionChannel TraceChannel,
                                     const FCollisionQueryParams& Params) const
{
	OutHits.Empty();
	const FPhysScene* Phys = PhysicsScene.get();
	const FVector Delta = End - Start;
	const float Length = Delta.Size();
	if (!Phys || !Phys->IsInitialized() || TraceChannel >= ECC_MAX || !std::isfinite(Length) || Length < MinQueryLength)
		return false;
	FChannelQueryFilter Filter(*Phys, TraceChannel, Params, true, false);
	std::vector<physx::PxRaycastHit> Touches(MaxQueryHits);
	physx::PxRaycastBuffer Buffer(Touches.data(), MaxQueryHits);
	Phys->GetPhysXScene()->raycast(PhysicsMath::ToPx(Start), PhysicsMath::ToPx(Delta / Length), Length, Buffer, physx::PxHitFlag::eDEFAULT,
	                               MakeQueryFilterData(Params, false), &Filter);
	// PhysX는 Block보다 먼 Touch를 보고하지 않는다.
	for (physx::PxU32 Index = 0; Index < Buffer.nbTouches; ++Index)
		OutHits.Add(MakeHitResult(Buffer.touches[Index], Start, End, false, false));
	if (Buffer.hasBlock)
		OutHits.Add(MakeHitResult(Buffer.block, Start, End, true, false));
	SortTouchesThenBlock(OutHits, Buffer.hasBlock);
	return Buffer.hasBlock;
}

bool UWorld::SweepSingleByChannel(FHitResult& OutHit, const FVector& Start, const FVector& End, const FQuat& Rot, ECollisionChannel TraceChannel,
                                  const FCollisionShape& Shape, const FCollisionQueryParams& Params) const
{
	if (!IsUsableSweepShape(Shape))
		return LineTraceSingleByChannel(OutHit, Start, End, TraceChannel, Params);
	OutHit = FHitResult();
	OutHit.Init(Start, End);
	const FPhysScene* Phys = PhysicsScene.get();
	physx::PxGeometryHolder Geometry;
	physx::PxQuat Rotation;
	const FVector Delta = End - Start;
	const float Length = Delta.Size();
	if (!Phys || !Phys->IsInitialized() || TraceChannel >= ECC_MAX || !std::isfinite(Length) || !MakeGeometry(Shape, Rot, Geometry, Rotation))
		return false;
	// 길이 0 Sweep은 시작 위치의 겹침만 검사한다.
	const physx::PxVec3 Direction = Length >= MinQueryLength ? PhysicsMath::ToPx(Delta / Length) : physx::PxVec3(1, 0, 0);
	const float Distance = Length >= MinQueryLength ? Length : 0.0f;
	FChannelQueryFilter Filter(*Phys, TraceChannel, Params, false, false);
	physx::PxSweepBuffer Buffer;
	Phys->GetPhysXScene()->sweep(Geometry.any(), physx::PxTransform(PhysicsMath::ToPx(Start), Rotation), Direction, Distance, Buffer,
	                             physx::PxHitFlag::eDEFAULT | physx::PxHitFlag::eMTD, MakeQueryFilterData(Params, false), &Filter);
	if (!Buffer.hasBlock)
		return false;
	OutHit = MakeHitResult(Buffer.block, Start, End, true, true);
	return true;
}

bool UWorld::SweepMultiByChannel(TArray<FHitResult>& OutHits, const FVector& Start, const FVector& End, const FQuat& Rot, ECollisionChannel TraceChannel,
                                 const FCollisionShape& Shape, const FCollisionQueryParams& Params) const
{
	if (!IsUsableSweepShape(Shape))
		return LineTraceMultiByChannel(OutHits, Start, End, TraceChannel, Params);
	OutHits.Empty();
	const FPhysScene* Phys = PhysicsScene.get();
	physx::PxGeometryHolder Geometry;
	physx::PxQuat Rotation;
	const FVector Delta = End - Start;
	const float Length = Delta.Size();
	if (!Phys || !Phys->IsInitialized() || TraceChannel >= ECC_MAX || !std::isfinite(Length) || !MakeGeometry(Shape, Rot, Geometry, Rotation))
		return false;
	const physx::PxVec3 Direction = Length >= MinQueryLength ? PhysicsMath::ToPx(Delta / Length) : physx::PxVec3(1, 0, 0);
	const float Distance = Length >= MinQueryLength ? Length : 0.0f;
	FChannelQueryFilter Filter(*Phys, TraceChannel, Params, true, false);
	std::vector<physx::PxSweepHit> Touches(MaxQueryHits);
	physx::PxSweepBuffer Buffer(Touches.data(), MaxQueryHits);
	Phys->GetPhysXScene()->sweep(Geometry.any(), physx::PxTransform(PhysicsMath::ToPx(Start), Rotation), Direction, Distance, Buffer,
	                             physx::PxHitFlag::eDEFAULT | physx::PxHitFlag::eMTD, MakeQueryFilterData(Params, false), &Filter);
	for (physx::PxU32 Index = 0; Index < Buffer.nbTouches; ++Index)
		OutHits.Add(MakeHitResult(Buffer.touches[Index], Start, End, false, true));
	if (Buffer.hasBlock)
		OutHits.Add(MakeHitResult(Buffer.block, Start, End, true, true));
	SortTouchesThenBlock(OutHits, Buffer.hasBlock);
	return Buffer.hasBlock;
}

bool UWorld::OverlapMultiByChannel(TArray<FOverlapResult>& OutOverlaps, const FVector& Pos, const FQuat& Rot, ECollisionChannel TraceChannel,
                                   const FCollisionShape& Shape, const FCollisionQueryParams& Params) const
{
	OutOverlaps.Empty();
	const FPhysScene* Phys = PhysicsScene.get();
	physx::PxGeometryHolder Geometry;
	physx::PxQuat Rotation;
	if (!Phys || !Phys->IsInitialized() || TraceChannel >= ECC_MAX || !PhysicsMath::ToPx(Pos).isFinite() || !MakeGeometry(Shape, Rot, Geometry, Rotation))
		return false;
	FChannelQueryFilter Filter(*Phys, TraceChannel, Params, true, true);
	std::vector<physx::PxOverlapHit> Touches(MaxQueryHits);
	physx::PxOverlapBuffer Buffer(Touches.data(), MaxQueryHits);
	Phys->GetPhysXScene()->overlap(Geometry.any(), physx::PxTransform(PhysicsMath::ToPx(Pos), Rotation), Buffer, MakeQueryFilterData(Params, true), &Filter);
	bool bAnyBlock = false;
	for (physx::PxU32 Index = 0; Index < Buffer.nbTouches; ++Index)
	{
		const physx::PxOverlapHit& Hit = Buffer.touches[Index];
		const FBodyInstance* Body = GetBody(Hit.actor);
		UPrimitiveComponent* Component = Body ? Body->GetOwnerComponent() : nullptr;
		if (!Component)
			continue;
		const bool bBlocking = PhysicsFilter::GetResponse(Hit.shape->getQueryFilterData(), TraceChannel) == ECR_Block;
		// 같은 컴포넌트의 여러 형상은 하나로 합친다. 하나라도 Block이면 Block으로 본다.
		FOverlapResult* Existing = nullptr;
		for (FOverlapResult& Result : OutOverlaps)
			if (Result.Component == Component)
				Existing = &Result;
		if (Existing)
			Existing->bBlockingHit |= bBlocking;
		else
		{
			FOverlapResult Result;
			Result.Component = Component;
			if (Body->GetBodySetup())
				Result.BoneName = Body->GetBodySetup()->BoneName;
			Result.bBlockingHit = bBlocking;
			OutOverlaps.Add(Result);
		}
		bAnyBlock |= bBlocking;
	}
	return bAnyBlock;
}

void UWorld::UpdatePhysicsOverlaps()
{
	FPhysScene* Phys = PhysicsScene.get();
	if (!Phys || !Phys->IsInitialized())
		return;
	SCOPE_CYCLE_COUNTER(PhysicsStats::STAT_PhysUpdateOverlaps);
	PhysicsStats::FCounters& Counters = PhysicsStats::GCounters;

	// 1. 이 월드에서 씬에 들어 있는 바디와 그 컴포넌트. 스켈레탈처럼 바디가 여러 개인 컴포넌트도 있다.
	//    이벤트 순서를 고정하려고 컴포넌트 UUID, 바디 인덱스 순으로 정렬한다.
	std::vector<FBodyInstance*> SceneBodies;
	for (FBodyInstance* Body : Phys->GetBodies())
	{
		UPrimitiveComponent* Component = Body->GetOwnerComponent();
		if (Component && Component->IsRegistered() && Component->GetWorld() == this && Body->IsInScene())
			SceneBodies.push_back(Body);
	}
	std::sort(SceneBodies.begin(), SceneBodies.end(), [](const FBodyInstance* A, const FBodyInstance* B)
	          {
		          const uint32 UA = A->GetOwnerComponent()->GetUUID(), UB = B->GetOwnerComponent()->GetUUID();
		          return UA != UB ? UA < UB : A->InstanceBodyIndex < B->InstanceBodyIndex; });
	std::vector<UPrimitiveComponent*> Components;
	for (FBodyInstance* Body : SceneBodies)
		if (Components.empty() || Components.back() != Body->GetOwnerComponent())
			Components.push_back(Body->GetOwnerComponent());
	const std::unordered_set<UPrimitiveComponent*> Tracked(Components.begin(), Components.end());

	// 2. 현재 겹치는 쌍. 각 바디의 Query 형상으로 Scene Overlap을 수행하고 컴포넌트 쌍으로 모은다.
	std::vector<FComponentPair> Current;
	std::unordered_set<FComponentPair, FComponentPairHash> CurrentSet;
	std::vector<physx::PxOverlapHit> Touches(MaxQueryHits);
	for (FBodyInstance* BodyPtr : SceneBodies)
	{
		UPrimitiveComponent* Component = BodyPtr->GetOwnerComponent();
		if (!CanGeneratePhysicsOverlap(Component))
			continue;
		const FBodyInstance& Body = *BodyPtr;
		physx::PxRigidActor* Actor = Body.GetActor();
		++Counters.OverlapBodies.Current;
		const physx::PxFilterData SelfData = PhysicsFilter::MakeFilterData(Body);
		FBodyOverlapFilter Filter(*Phys, Body, SelfData);
		physx::PxQueryFilterData FilterData;
		FilterData.flags = physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC | physx::PxQueryFlag::ePREFILTER | physx::PxQueryFlag::eNO_BLOCK;
		for (physx::PxU32 ShapeIndex = 0; ShapeIndex < Actor->getNbShapes(); ++ShapeIndex)
		{
			physx::PxShape* Shape = nullptr;
			Actor->getShapes(&Shape, 1, ShapeIndex);
			if (!Shape->getFlags().isSet(physx::PxShapeFlag::eSCENE_QUERY_SHAPE))
				continue;
			physx::PxOverlapBuffer Buffer(Touches.data(), MaxQueryHits);
			Phys->GetPhysXScene()->overlap(Shape->getGeometry().any(), physx::PxShapeExt::getGlobalPose(*Shape, *Actor), Buffer, FilterData, &Filter);
			++Counters.OverlapQueries.Current;
			Counters.OverlapTouches.Current += Buffer.nbTouches;
			for (physx::PxU32 Index = 0; Index < Buffer.nbTouches; ++Index)
			{
				const FBodyInstance* OtherBody = GetBody(Buffer.touches[Index].actor);
				UPrimitiveComponent* Other = OtherBody ? OtherBody->GetOwnerComponent() : nullptr;
				if (!Other || Other == Component || !Tracked.contains(Other) || !CanGeneratePhysicsOverlap(Other) || Other->GetOwner() == Component->GetOwner())
					continue;
				const FComponentPair Pair = MakePair(Component, Other);
				if (CurrentSet.insert(Pair).second)
					Current.push_back(Pair);
			}
		}
	}

	Counters.OverlapPairs.Current += Current.size();

	// 3. 지난 프레임의 쌍. 상대가 바디를 잃었으면(충돌 끔·삭제) 그 쌍은 끝난 것으로 처리한다.
	std::vector<FComponentPair> Previous;
	std::unordered_set<FComponentPair, FComponentPairHash> PreviousSet;
	for (UPrimitiveComponent* Component : Components)
		for (const FOverlapInfo& Info : Component->GetOverlapInfos())
			if (Info.OtherComp)
			{
				const FComponentPair Pair = MakePair(Component, Info.OtherComp);
				if (PreviousSet.insert(Pair).second)
					Previous.push_back(Pair);
			}

	// 4. End → Begin. 콜백이 컴포넌트를 지울 수 있으므로 매번 등록 상태를 확인한다.
	for (const FComponentPair& Pair : Previous)
	{
		if (CurrentSet.contains(Pair))
			continue;
		Pair.A->EndComponentOverlap(Pair.B);
		Pair.B->EndComponentOverlap(Pair.A);
	}
	for (const FComponentPair& Pair : Current)
	{
		if (PreviousSet.contains(Pair) || !Pair.A->IsRegistered() || !Pair.B->IsRegistered())
			continue;
		Pair.A->BeginComponentOverlap(Pair.B);
		if (Pair.A->IsRegistered() && Pair.B->IsRegistered())
			Pair.B->BeginComponentOverlap(Pair.A);
	}
}
