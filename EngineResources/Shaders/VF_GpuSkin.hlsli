// FGpuSkinVertex. 본 행렬로 스키닝한 결과를 로컬 정점처럼 월드로 옮긴다
#include "GpuSkinCommon.hlsli"

struct FVertexFactoryInput
{
    float3 Position     : POSITION;
    float3 Normal       : NORMAL;
    float4 Tangent      : TANGENT;
    float2 UV           : TEXCOORD;
    float4 Color        : COLOR;
    uint4  BlendIndices : BLENDINDICES;
    float4 BlendWeights : BLENDWEIGHT;
    uint   VertexID     : SV_VertexID;
};

struct FClothRenderVertex
{
    float4 Position;
    float4 Normal;
    float4 Tangent;
};

StructuredBuffer<FClothRenderVertex> ClothVertices : register(t15);

FVFIntermediates VF_GetIntermediates(FVertexFactoryInput Input, float4x4 World, float4x4 NormalMatrix)
{
    uint Count, Stride;
    ClothVertices.GetDimensions(Count, Stride);
    
    if (Input.VertexID < Count)
    {
        const FClothRenderVertex Cloth = ClothVertices[Input.VertexID];
        if (Cloth.Position.w != 0.0f)
        {
            return VF_LocalToWorld(
                Cloth.Position.xyz, Cloth.Normal.xyz, Cloth.Tangent,
                Input.UV, Input.Color, World, NormalMatrix);
        }

    }
    
    float3 Position = Input.Position;
    float3 Normal = Input.Normal;
    float4 Tangent = Input.Tangent;
    GpuSkinVertex(Input.BlendIndices, Input.BlendWeights, Position, Normal, Tangent);
    return VF_LocalToWorld(Position, Normal, Tangent, Input.UV, Input.Color, World, NormalMatrix);
}
