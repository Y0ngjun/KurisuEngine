#pragma once
#include "Core/Types.h"
#include "Math/Vector.h"
#include "Core/NameTypes.h"

class UPrimitiveComponent;
class AActor;

// UE OverlapResult.h. Overlap 쿼리 결과 하나. 같은 컴포넌트의 여러 형상은 한 번만 보고한다.
struct FOverlapResult
{
	UPrimitiveComponent* Component = nullptr;
	FName BoneName;
	// 쿼리 채널에 대해 Block 응답이면 true, Overlap 응답이면 false
	bool bBlockingHit = false;

	UPrimitiveComponent* GetComponent() const { return Component; }
	AActor* GetActor() const;
};

struct FHitResult
{
	int32 FaceIndex; // Hitted face index

	float Time; // For swept
	float Distance = FLT_MAX;

	FVector Location = FVector(); // For swept
	FVector ImpactPoint = FVector();
	FVector Normal; // For swept
	FVector ImpactNormal;

	FVector TraceStart;
	FVector TraceEnd;

	float PenetrationDepth;

	bool bBlockingHit = false;
	bool bStartPenetrating = false;

	UPrimitiveComponent* HitComponent = nullptr;
	// 맞은 바디의 BodySetup.BoneName (UE FHitResult::BoneName). 스켈레탈 다중 바디가 생기면 본을 구분한다.
	FName BoneName;
	// 이 쪽(이벤트를 받은 컴포넌트)에서 맞은 바디의 본 (UE FHitResult::MyBoneName). 시뮬레이션 Hit에서 채운다.
	FName MyBoneName;

	UPrimitiveComponent* GetComponent() const { return HitComponent; }
	AActor* GetActor() const;

	FHitResult()
	{
		Init();
	}

	explicit FHitResult(float InTime)
	{
		Init();
		Time = InTime;
	}

	// Initailize empty hit result
	inline void Init()
	{
		Time = 1.0f;
	}

	inline void Init(FVector Start, FVector End)
	{
		Time = 1.0f;
		TraceStart = Start;
		TraceEnd = End;
	}
};