#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Physics/AggregateGeom.h"
#include "Physics/PhysicalMaterial.h"
#include "Physics/BodyInstance.h"
#include <unordered_map>
#include "BodySetup.generated.h"

struct FStaticMeshData;
class FLineBatcher;
struct FMatrix;
struct FVector4;
namespace physx { class PxPhysics; class PxConvexMesh; }

UCLASS()
class UBodySetupCore : public UObject
{
	GENERATED_BODY()
public:
	FName BoneName;
	UPROPERTY(EditAnywhere)
	EPhysicsType PhysicsType = EPhysicsType::Default;
	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;
};

// Asset-owned collision description. No scene or PxRigidActor belongs here.
UCLASS()
class UBodySetup : public UBodySetupCore
{
	GENERATED_BODY()
public:
	UBodySetup();
	UPROPERTY(EditAnywhere)
	FKAggregateGeom AggGeom;
	UPROPERTY(EditAnywhere)
	FBodyInstance DefaultInstance;
	UPROPERTY(EditAnywhere)
	UPhysicalMaterial* PhysMaterial = nullptr;
	// 메시 경계에 맞춘 형상 하나를 만든다 (AggGeom은 건드리지 않는다).
	static FKBoxElem MakeFitBox(const FStaticMeshData& Mesh);
	static FKSphereElem MakeFitSphere(const FStaticMeshData& Mesh);
	static FKSphylElem MakeFitCapsule(const FStaticMeshData& Mesh);
	static FKConvexElem MakeFitConvex(const FStaticMeshData& Mesh);
	// 기존 형상을 모두 지우고 맞춘 형상 하나로 바꾼다.
	void FitBox(const FStaticMeshData& Mesh, bool bInvalidate = true);
	void FitSphere(const FStaticMeshData& Mesh);
	void FitCapsule(const FStaticMeshData& Mesh);
	void FitConvex(const FStaticMeshData& Mesh);
	physx::PxConvexMesh* CreateConvexMesh(const FKConvexElem& Element, physx::PxPhysics& Physics);
	// 편집 중 미리보기용 와이어프레임. World는 컴포넌트 월드 행렬(스케일 포함), Scale은 그 스케일.
	// 바디 생성 규칙과 같게, 비균등 스케일에서 만들어지지 않는 형상은 그리지 않는다.
	void DrawDebug(FLineBatcher& Lines, const FMatrix& World, const FVector& Scale, const FVector4& Color);
	size_t GetCookedConvexCount() const { return CookedConvexData.size(); }
	virtual void Serialize(FStructuredArchive::FRecord Record) override;
	void InvalidatePhysicsData();
	bool IsDirty() const { return bDirty; }
	void MarkSaved() { bDirty = false; }
	virtual void PostEditChangeProperty(const FProperty& Property) override;

private:
	bool bDirty = false;
	std::unordered_map<FString, TArray<uint8>> CookedConvexData;
};

// 쿠킹된 컨벡스 메시의 폴리곤 테두리를 그린다. ToWorld는 메시 로컬 → 월드.
void DrawConvexMeshDebug(FLineBatcher& Lines, const physx::PxConvexMesh& Mesh, const FMatrix& ToWorld, const FVector4& Color);
