#pragma once

#include "MeshComponent.h"
#include "Render/SkeletalMesh.h"
#include "SkinnedMeshComponent.generated.h"

class FSkeletalMeshObject;

struct FClothSectionPositions
{
	int32 SectionIndex = INDEX_NONE;
	TArray<FVector> Positions;
};

UCLASS()
class USkinnedMeshComponent : public UMeshComponent
{
	GENERATED_BODY()
public:
	USkinnedMeshComponent();
	virtual ~USkinnedMeshComponent();

	virtual void TickComponent(float DeltaTime);
	virtual void OnUnregister() override;

	void SetSkeletalMesh(USkeletalMesh* InMesh);
	USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }
	FSkeletalMeshObject* GetMeshObject() const { return MeshObject.get(); }

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBox CalcLocalBounds() const override;

	// 머티리얼 슬롯은 스켈레탈 에셋이 결정한다.
	virtual int32 GetNumMaterials() const override;
	virtual FString GetMaterialSlotName(int32 SlotIndex) const override;
	virtual UMaterial* GetDefaultMaterial(int32 SlotIndex) const override;

	virtual bool LineTraceComponentLocal(const FRay& LocalRay, float& Out) override;

	// 엔진 전체에 적용한다. 값을 바꾸면 RefreshSkinningMode로 기존 컴포넌트에 반영한다
	// 기본은 GPU. CPU 스키닝은 프레임마다 정점 전체를 CPU에서 변환해 Debug에서 캐릭터당 수 ms가 든다.
	// 지원하지 않는 환경이면 MakeMeshObject가 CPU로 되돌린다.
	inline static bool bUseGPUSkinning = true;
	inline static bool bForceSkinUpdate = false;

	void RefreshSkinningMode();

	void MarkBoneTransformsDirty() { bBoneTransformsDirty = true; }
	// 본 포즈와 렌더(스키닝)를 바로 갱신한다. 에디터·프리뷰 등 즉시 결과가 필요한 곳에서 쓴다.
	void UpdateBoneTransformsNow();
	// 본 포즈는 바로 갱신하고, 게임 월드 Tick 중이면 스키닝은 Tick 끝으로 미뤄 프레임당 한 번만 한다.
	void UpdateBoneTransformsDeferred();
	// UWorld가 게임 월드 Tick 끝에서 부른다.
	void FlushQueuedRenderUpdate();

	static void SetGPUSkinningEnabled(bool bEnable);

	const TArray<FMatrix>& GetComponentSpaceTransform() const { return ComponentSpaceTransforms; }

	void SetDebugWeightBone(int32 BoneIndex);
	int32 GetDebugWeightBone() const { return DebugWeightBone; }

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	
	void SetClothPositions(const TArray<FClothSectionPositions>& Sections);
	void ClearClothPositions();

protected:
	virtual void RefreshBoneTransforms();
	void SendRenderDynamicData();
	virtual void OnSkeletalMeshChanged() {};
	virtual void OnSkeletalMeshChanging() {};
	virtual void OnSkeletalMeshReplaced() {};

	TArray<FMatrix> ComponentSpaceTransforms;

private:
	void RecreateMeshObject();
	void QueueClothRenderUpdate();


	int32 DebugWeightBone = -1;

	UPROPERTY()
	USkeletalMesh* SkeletalMesh = nullptr;
	TUniquePtr<FSkeletalMeshObject> MeshObject;
	bool bBoneTransformsDirty = true;
	// UWorld의 스키닝 큐에 들어가 있는지. 한 프레임에 여러 번 갱신돼도 한 번만 넣는다.
	bool bRenderUpdateQueued = false;

	TArray<FClothRenderVertex> ClothRenderVertices;
};
