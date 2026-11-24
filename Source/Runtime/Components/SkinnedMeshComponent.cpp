#include "EnginePCH.h"
#include "SkinnedMeshComponent.h"
#include "Render/SkeletalMeshObject.h"
#include "Engine/SkeletalMeshSceneProxy.h"
#include "Engine/Scene.h"
#include "Collision/Ray.h"
#include <UObject/UObjectIterator.h>

#include "Render/SkinningStats.h"
#include "Engine/World.h"

#include <cfloat>

namespace
{
	TUniquePtr<FSkeletalMeshObject> MakeMeshObject(USkeletalMesh* Mesh)
	{
		if (USkinnedMeshComponent::bUseGPUSkinning)
		{
			TUniquePtr<FSkeletalMeshObjectGPUSkin> GPUObject = MakeUnique<FSkeletalMeshObjectGPUSkin>(Mesh);
			if (GPUObject->IsValid())
				return GPUObject;
			KRS_LOG(Warning, "GPU skinning unavailable; falling back to CPU skinning.");
		}
		return MakeUnique<FSkeletalMeshObjectCPUSkin>(Mesh);
	}

	bool BuildClothRenderVertices(
	    const USkeletalMesh& Mesh,
	    int32 SectionIndex,
	    const TArray<FVector>& Positions,
	    TArray<FClothRenderVertex>& Out)
	{
		const auto& LOD = Mesh.GetRenderData().LODRenderData[0];
		const auto* Cloth = Mesh.FindClothingLOD(0, SectionIndex);
		const int32 VertexCount = LOD.BindVertices.Num();

		if (!Cloth || !LOD.Sections.IsValidIndex(SectionIndex) ||
		    Positions.IsEmpty() ||
		    Positions.Num() != Cloth->RestPositions.Num() ||
		    Cloth->RenderToSimVertex.Num() != VertexCount ||
		    Cloth->MaxDistances.Num() != Positions.Num())
		{
			return false;
		}

		for (const FVector& P : Positions)
		{
			if (!std::isfinite(P.X) || !std::isfinite(P.Y) ||
			    !std::isfinite(P.Z))
			{
				return false;
			}
		}

		const auto& Section = LOD.Sections[SectionIndex];
		const uint32 IndexCount = static_cast<uint32>(LOD.Indices.Num());

		if (Section.IndexCount == 0 || Section.IndexCount % 3 != 0 ||
		    Section.StartIndex > IndexCount ||
		    Section.IndexCount > IndexCount - Section.StartIndex)
		{
			return false;
		}

		TArray<FVector> Normals, Tangents, Bitangents;
		Normals.Init(FVector(0, 0, 0), Positions.Num());
		Tangents.Init(FVector(0, 0, 0), VertexCount);
		Bitangents.Init(FVector(0, 0, 0), VertexCount);
		Out.Init(FClothRenderVertex{}, VertexCount);

		const uint32 End = Section.StartIndex + Section.IndexCount;
		for (uint32 I = Section.StartIndex; I < End; I += 3)
		{
			const uint32 R[] = {
			    LOD.Indices[I], LOD.Indices[I + 1], LOD.Indices[I + 2]};
			int32 S[3];

			for (int32 K = 0; K < 3; ++K)
			{
				if (R[K] >= static_cast<uint32>(VertexCount))
					return false;

				S[K] = Cloth->RenderToSimVertex[R[K]];
				if (!Positions.IsValidIndex(S[K]))
					return false;

				// MaxDistances가 0이면 시뮬레이션 정점이 아니므로 렌더 정점은 스킨 포즈를 따른다.
				if (Cloth->MaxDistances[S[K]] > 0.0f)
				{
					const FVector& P = Positions[S[K]];
					Out[R[K]].Position = FVector4(P.X, P.Y, P.Z, 1.0f);
				}
			}

			const FVector E1 = Positions[S[1]] - Positions[S[0]];
			const FVector E2 = Positions[S[2]] - Positions[S[0]];
			const FVector FaceNormal = FVector::Cross(E1, E2);
			if (FaceNormal.SizeSquared() <= 1.0e-12f)
				continue;

			for (int32 SimIndex : S)
				Normals[SimIndex] += FaceNormal;

			const FVector2 UV0 = LOD.BindVertices[R[0]].UV;
			const FVector2 D1 = LOD.BindVertices[R[1]].UV - UV0;
			const FVector2 D2 = LOD.BindVertices[R[2]].UV - UV0;
			const float Det = D1.X * D2.Y - D1.Y * D2.X;
			if (std::abs(Det) <= 1.0e-8f)
				continue;

			const FVector T = (E1 * D2.Y - E2 * D1.Y) * (1.0f / Det);
			const FVector B = (E2 * D1.X - E1 * D2.X) * (1.0f / Det);
			for (uint32 RenderIndex : R)
			{
				Tangents[RenderIndex] += T;
				Bitangents[RenderIndex] += B;
			}
		}

		for (int32 V = 0; V < VertexCount; ++V)
		{
			auto& Result = Out[V];
			if (Result.Position.W == 0.0f)
				continue;

			const auto& Bind = LOD.BindVertices[V];
			FVector N = Normals[Cloth->RenderToSimVertex[V]];
			N = N.SizeSquared() > 1.0e-12f
			        ? N.Normalized()
			        : Bind.Normal.Normalized();

			const FVector BindT(Bind.Tangent.X, Bind.Tangent.Y, Bind.Tangent.Z);
			FVector T = Tangents[V] - N * N.Dot(Tangents[V]);
			if (T.SizeSquared() <= 1.0e-12f)
				T = BindT - N * N.Dot(BindT);
			if (T.SizeSquared() <= 1.0e-12f)
			{
				const FVector Axis = std::abs(N.Z) < 0.9f
				                         ? FVector(0, 0, 1)
				                         : FVector(0, 1, 0);
				T = FVector::Cross(Axis, N);
			}

			T = T.Normalized();
			const float Sign = Bitangents[V].SizeSquared() > 1.0e-12f
			                       ? (FVector::Cross(N, T).Dot(Bitangents[V]) < 0.0f ? -1.0f : 1.0f)
			                       : Bind.Tangent.W;

			Result.Normal = FVector4(N.X, N.Y, N.Z, 0.0f);
			Result.Tangent = FVector4(T.X, T.Y, T.Z, Sign);
		}

		return true;
	}
}

USkinnedMeshComponent::USkinnedMeshComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

USkinnedMeshComponent::~USkinnedMeshComponent()
{
}

void USkinnedMeshComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	if (bBoneTransformsDirty || bForceSkinUpdate)
		UpdateBoneTransformsDeferred();
}

void USkinnedMeshComponent::OnUnregister()
{
	if (bRenderUpdateQueued)
	{
		bRenderUpdateQueued = false;
		if (UWorld* World = GetWorld())
			World->DequeueSkinnedMeshRenderUpdate(this);
	}
	Super::OnUnregister();
}

void USkinnedMeshComponent::SetSkeletalMesh(USkeletalMesh* InMesh)
{
	if (SkeletalMesh == InMesh)
		return;

	OnSkeletalMeshChanging();
	SkeletalMesh = InMesh;

	ClearOverrideMaterials();
	RecreateMeshObject();
	OnSkeletalMeshReplaced();
}

FPrimitiveSceneProxy* USkinnedMeshComponent::CreateSceneProxy()
{
	return new FSkeletalMeshSceneProxy(this);
}

FBox USkinnedMeshComponent::CalcLocalBounds() const
{
	return MeshObject ? MeshObject->GetBounds() : Super::CalcLocalBounds();
}

int32 USkinnedMeshComponent::GetNumMaterials() const
{
	return SkeletalMesh ? static_cast<int32>(SkeletalMesh->GetMaterialSlots().Num()) : 0;
}

FString USkinnedMeshComponent::GetMaterialSlotName(int32 SlotIndex) const
{
	if (!SkeletalMesh || SlotIndex < 0 || SlotIndex >= GetNumMaterials())
		return FString();
	return SkeletalMesh->GetMaterialSlots()[SlotIndex].Name;
}

UMaterial* USkinnedMeshComponent::GetDefaultMaterial(int32 SlotIndex) const
{
	return SkeletalMesh ? SkeletalMesh->GetMaterial(static_cast<uint32>(SlotIndex)) : nullptr;
}

bool USkinnedMeshComponent::LineTraceComponentLocal(const FRay& LocalRay, float& OutT)
{
	const TArray<FVertex>* Vertices = MeshObject ? MeshObject->GetCPUSkinnedVertices() : nullptr;
	if (!Vertices || !SkeletalMesh)
		return false;

	const TArray<uint32>& Indices = SkeletalMesh->GetRenderData().LODRenderData[0].Indices;
	bool bHit = false;
	for (int32 I = 0; I + 2 < Indices.Num(); I += 3)
	{
		float T = FLT_MAX;
		if (RayIntersectsTriangle(LocalRay, (*Vertices)[Indices[I]].Position,
		                          (*Vertices)[Indices[I + 1]].Position, (*Vertices)[Indices[I + 2]].Position, T) &&
		    T < OutT)
		{
			OutT = T;
			bHit = true;
		}
	}
	return bHit;
}

void USkinnedMeshComponent::UpdateBoneTransformsNow()
{
	bBoneTransformsDirty = false;
	if (!SkeletalMesh || !MeshObject)
		return;
	{
		SCOPE_CYCLE_COUNTER(SkinningStats::STAT_BoneTransforms);
		RefreshBoneTransforms();
	}
	SendRenderDynamicData();
}

void USkinnedMeshComponent::UpdateBoneTransformsDeferred()
{
	UWorld* World = GetWorld();
	if (!World || !World->CanDeferSkinnedMeshUpdates())
	{
		UpdateBoneTransformsNow();
		return;
	}
	bBoneTransformsDirty = false;
	if (!SkeletalMesh || !MeshObject)
		return;
	{
		SCOPE_CYCLE_COUNTER(SkinningStats::STAT_BoneTransforms);
		RefreshBoneTransforms();
	}
	if (!bRenderUpdateQueued)
	{
		bRenderUpdateQueued = true;
		World->QueueSkinnedMeshRenderUpdate(this);
	}
}

void USkinnedMeshComponent::FlushQueuedRenderUpdate()
{
	if (!bRenderUpdateQueued)
		return;
	bRenderUpdateQueued = false;
	if (SkeletalMesh && MeshObject && IsRegistered())
		SendRenderDynamicData();
}

void USkinnedMeshComponent::SetGPUSkinningEnabled(bool bEnable)
{
	if (bUseGPUSkinning == bEnable)
		return;
	bUseGPUSkinning = bEnable;
	for (TObjectIterator<USkinnedMeshComponent> It; It; ++It)
	{
		It->RefreshSkinningMode();
	}
	FStatRegistry::Reset();
}

void USkinnedMeshComponent::SetDebugWeightBone(int32 BoneIndex)
{
	if (DebugWeightBone == BoneIndex)
		return;
	DebugWeightBone = BoneIndex;
	if (MeshObject)
		MeshObject->SetDebugWeightBone(BoneIndex);
	// CPU 스키닝은 다시 돌아야 정점 색이 바뀐다. 틱이 없는 월드에서도 바로 보이도록 즉시 갱신한다
	UpdateBoneTransformsNow();
}

void USkinnedMeshComponent::RefreshBoneTransforms()
{
	const TArray<FBoneInfo>& Bones = SkeletalMesh->GetRefSkeleton();
	ComponentSpaceTransforms.SetNum(Bones.Num());
	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		const FMatrix Local = Bones[i].LocalBindPose.GetLocalMatrix();
		const int32 Parent = Bones[i].ParentIndex;
		ComponentSpaceTransforms[i] = Parent < 0 ? Local : Local * ComponentSpaceTransforms[Parent];
	}
}

void USkinnedMeshComponent::SendRenderDynamicData()
{
	MeshObject->Update(ComponentSpaceTransforms, ClothRenderVertices);
	if (MeshObject->UpdateBounds())
		OnTransformDirty();
	else if (FPrimitiveSceneProxy* Proxy = GetSceneProxy())
	{
		if (Proxy->GetScene() && Proxy->bCastsShadow)
		{
			Proxy->GetScene()->InvalidateShadowsInBounds(Proxy->GetBounds());
		}
	}
}

void USkinnedMeshComponent::RecreateMeshObject()
{
	const bool bValid = SkeletalMesh && !SkeletalMesh->GetRenderData().LODRenderData.IsEmpty();
	MeshObject = bValid ? MakeMeshObject(SkeletalMesh) : nullptr;
	if (MeshObject)
		MeshObject->SetDebugWeightBone(DebugWeightBone);
	ComponentSpaceTransforms.Reset();
	OnSkeletalMeshChanged();

	MarkRenderStateDirty();
	OnTransformDirty();
	bBoneTransformsDirty = true;
}

void USkinnedMeshComponent::RefreshSkinningMode()
{
	if (!MeshObject)
		return;
	MeshObject = MakeMeshObject(SkeletalMesh);
	if (MeshObject)
		MeshObject->SetDebugWeightBone(DebugWeightBone);
	UpdateBoneTransformsNow(); // 포즈는 그대로 두고 새 MeshObject에 다시 올린다
}

void USkinnedMeshComponent::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	if (Record.GetUnderlyingArchive().IsLoading())
		RecreateMeshObject();
}

void USkinnedMeshComponent::SetClothPositions(
    const TArray<FClothSectionPositions>& Sections)
{
	if (!MeshObject || !SkeletalMesh || Sections.IsEmpty())
	{
		ClearClothPositions();
		return;
	}

	TArray<FClothRenderVertex> Combined;
	TArray<FClothRenderVertex> SectionVertices;

	for (const auto& Section : Sections)
	{
		if (!BuildClothRenderVertices(*SkeletalMesh, Section.SectionIndex,
		                              Section.Positions, SectionVertices))
		{
			continue;
		}

		if (Combined.IsEmpty())
			Combined.Init(FClothRenderVertex{}, SectionVertices.Num());

		for (int32 Index = 0; Index < SectionVertices.Num(); ++Index)
		{
			if (SectionVertices[Index].Position.W != 0.0f &&
				Combined[Index].Position.W == 0.0f)
			{
				Combined[Index] = SectionVertices[Index];
			}
		}
	}

	if (Combined.IsEmpty())
	{
		ClearClothPositions();
		return;
	}

	ClothRenderVertices = std::move(Combined);
	QueueClothRenderUpdate();
}

void USkinnedMeshComponent::ClearClothPositions()
{
	if (ClothRenderVertices.IsEmpty())
		return;

	ClothRenderVertices.Reset();
	QueueClothRenderUpdate();
}

void USkinnedMeshComponent::QueueClothRenderUpdate()
{
	if (!MeshObject || !IsRegistered())
		return;

	if (ComponentSpaceTransforms.IsEmpty())
	{
		UpdateBoneTransformsNow();
		return;
	}

	UWorld* World = GetWorld();
	if (World && World->CanDeferSkinnedMeshUpdates())
	{
		if (!bRenderUpdateQueued)
		{
			bRenderUpdateQueued = true;
			World->QueueSkinnedMeshRenderUpdate(this);
		}
		return;
	}

	SendRenderDynamicData();
}
