#pragma once

#include "Physics/Cloth/ClothingAsset.h"
#include <filesystem>

struct FSkeletalMeshAssetBinding
{
	int32 RenderLOD = 0;
	int32 SectionIndex = -1;
	FString ClothingAssetId;
	int32 ClothLODIndex = 0;
	FString SectionSignature;
	bool Enabled = true;
};

struct FSkeletalMeshAssetData
{
	FString BakedMeshPath;
	FString PhysicsAssetPath;
	TArray<FClothingAsset> ClothingAssets;
	TArray<FSkeletalMeshAssetBinding> ClothingBindings;
};

namespace SkeletalMeshAssetFile
{
	bool IsAssetPath(const FString& Path);
	FString ProjectContentReference(const FString& Path);
	FString ResolveContentReference(const FString& Reference);
	std::filesystem::path DocumentPath(const std::filesystem::path& Bake);
	bool CreateDefault(const FString& BakePath, FSkeletalMeshAssetData& OutData, FString& OutError);
	bool Read(const std::filesystem::path& Path, FSkeletalMeshAssetData& OutData, FString& OutError);
	bool Write(const std::filesystem::path& Path, FSkeletalMeshAssetData& Data, FString& OutError);
	FString SectionSignature(const FSkeletalMeshData& Data, int32 RenderLOD, int32 SectionIndex);
	FString SectionSignature(const TArray<FVertex>& Vertices, const TArray<FSkinWeight>& Weights,
	                         const TArray<uint32>& Indices, const TArray<FBoneInfo>& Bones,
	                         const FSkeletalMeshSection& Section);
}
