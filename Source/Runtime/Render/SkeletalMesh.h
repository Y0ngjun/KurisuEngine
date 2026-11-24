#pragma once

#include "Asset/RenderAsset.h"
#include "Math/Box.h"
#include "Vertex.h"
#include "SkeletalMeshData.h"
#include "MeshRenderData.h"
#include "Buffer.h"
#include "Material.h"
#include "Asset/SkeletalMeshAssetFile.h"

#include <unordered_map>
#include <vector>
#include <span>

#include "SkeletalMesh.generated.h"

class UPhysicsAsset;

struct FSkeletalMeshLODRenderData
{
	TArray<FVertex> BindVertices;
	TArray<FSkinWeight> SkinWeights;
	TArray<uint32> Indices;
	TArray<FSkeletalMeshSection> Sections;
	TUniquePtr<FIndexBuffer> IndexBuffer;
	FBox BindAABB;

	// GPU 스키닝 입력. 포즈와 무관하므로 에셋이 갖고 인스턴스끼리 공유한다
	TUniquePtr<FVertexBuffer> GpuSkinVertexBuffer;
	// 본마다 가중치가 있는 정점의 바인드 바운드. 정점이 없는 본은 Min > Max
	TArray<FBox> BoneBounds;
};

struct FSkeletalMeshRenderData
{
	TArray<FSkeletalMeshLODRenderData> LODRenderData;
};

UCLASS()
class USkeletalMesh : public URenderAsset
{
	GENERATED_BODY()
public:
	bool Build(FSkeletalMeshData&& ImportData, FString& OutError);
	void ApplyAssetData(FSkeletalMeshAssetData&& Data);
	bool IsSkeletalMeshAsset() const { return !AssetData.BakedMeshPath.empty(); }
	const FString& GetBakedMeshPath() const { return AssetData.BakedMeshPath; }
	const FString& GetPhysicsAssetPath() const { return AssetData.PhysicsAssetPath; }
	bool AssignPhysicsAssetPath(const FString& Path, FString& OutError);
	bool SaveSkeletalMeshAsset(FString& OutError);
	bool RevertSkeletalMeshAsset(FString& OutError);
	bool IsAssetDirty() const { return bAssetDirty; }
	bool AssetNeedsReload() const { return bAssetNeedsReload; }
	void MarkAssetNeedsReload() { bAssetNeedsReload = true; }
	void MarkAssetDirty() { bAssetDirty = IsSkeletalMeshAsset(); }
	int32 GetClothingAssetCount() const { return AssetData.ClothingAssets.Num(); }

	const TArray<FBoneInfo>& GetRefSkeleton() const { return RefSkeleton; }
	int32 GetNumBones() const { return RefSkeleton.Num(); }
	int32 FindBoneIndex(const FString& Name) const;
	const FSkeletalMeshRenderData& GetRenderData() const { return RenderData; }
	const TArray<FSkeletalMaterialSlot>& GetMaterialSlots() const { return MaterialSlots; }
	UMaterial* GetMaterial(uint32 SlotIndex) const;

	TSharedPtr<const FMeshRenderState> GetRenderState(const TArray<UMaterial*>& Materials);

	// UE USkeletalMesh::PhysicsAsset. 처음 부르면 저장된 .physicsasset을 읽고, 없으면 본 정점 범위로 자동 생성한다
	// (자동 생성본은 Dirty 상태이며 SavePhysicsAsset으로 저장한다). 메시가 이 에셋을 소유한다.
	UPhysicsAsset* GetPhysicsAsset() const;
	void SetPhysicsAsset(UPhysicsAsset* InPhysicsAsset);
	// 과도기 경로: Content/Physics/PhysicsAssets/<메시 경로>.physicsasset. 메시 에셋 저장 형식이 생기면 합친다.
	bool SavePhysicsAsset();
	// 저장된 파일 내용으로 되돌린다. 파일이 없으면 다시 자동 생성한다.
	bool RevertPhysicsAsset();

	int32 CreateClothingAssetFromSection(
	    int32 RenderLOD,
	    int32 SectionIndex,
	    const FString& Name,
	    std::span<const float> RenderMaxDistances,
	    FString& OutError,
	    std::span<const uint32> WeldGroups = {});

	bool BindClothingSection(
	    int32 RenderLOD, int32 SectionIndex,
	    int32 AssetIndex, int32 ClothLODIndex,
	    FString& OutError);

	bool UnbindClothingSection(int32 RenderLOD, int32 SectionIndex);

	const FClothingLODData* FindClothingLOD(
	    int32 RenderLOD, int32 SectionIndex) const;

	const TArray<FClothingSectionBinding>& GetClothingBindings() const
	{
		return ClothingBindings;
	}

	FClothingAsset* GetClothingAsset(int32 Index)
	{
		return AssetData.ClothingAssets.IsValidIndex(Index) ? &AssetData.ClothingAssets[Index] : nullptr;
	}
	const FClothingAsset* GetClothingAsset(int32 Index) const
	{
		return AssetData.ClothingAssets.IsValidIndex(Index) ? &AssetData.ClothingAssets[Index] : nullptr;
	}

private:
	struct FMaterialSlotsHash
	{
		size_t operator()(const TArray<UMaterial*>& Slots) const;
	};


	TArray<FBoneInfo> RefSkeleton;
	mutable UPhysicsAsset* PhysicsAsset = nullptr;
	TArray<FSkeletalMaterialSlot> MaterialSlots;
	TArray<UMaterial*> Materials;
	FSkeletalMeshRenderData RenderData;
	// UStaticMesh와 같은 캐시: 같은 머티리얼 조합의 컴포넌트끼리 섹션 목록 공유
	TMap<TArray<UMaterial*>, TWeakPtr<const FMeshRenderState>, FMaterialSlotsHash> RenderStates;

	FString GetSectionSignature(int32 RenderLOD, int32 SectionIndex) const;
	bool CanBindClothingSection(int32 RenderLOD, int32 SectionIndex, int32 AssetIndex, int32 ClothLODIndex) const;
	void RebuildClothingBindings();
	void ReplaceAssignedPhysicsAsset(UPhysicsAsset* Asset);

	FSkeletalMeshAssetData AssetData;
	bool bAssetDirty = false;
	bool bAssetNeedsReload = false;
	TArray<FClothingSectionBinding> ClothingBindings;
};