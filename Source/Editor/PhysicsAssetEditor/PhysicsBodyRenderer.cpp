#include "EnginePCH.h"
#include "PhysicsBodyRenderer.h"

#include "Asset/AssetManager.h"
#include "Physics/BodySetup.h"
#include "Render/Mesh.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"

namespace
{
	// PhysicsBodyShader.hlsl의 cbuffer와 같은 배치 (16바이트 단위)
	struct FPhysicsBodyConstants
	{
		FMatrix World;
		FMatrix ViewProjection;
		FVector4 Color;
		FVector ShapeScale;
		float HalfLength = 0.0f;
		FVector CameraPosition;
		float Padding = 0.0f;
	};
	static_assert(sizeof(FPhysicsBodyConstants) == 176, "PhysicsBodyShader cbuffer 배치와 맞아야 한다");

	constexpr float SphereMeshRadius = 100.0f;
}

FPhysicsBodyRenderer::FPhysicsBodyRenderer()
{
	PipelineState.Shader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/PhysicsBodyShader.hlsl");
	PipelineState.Topology = EPrimitiveTopology::TriangleList;
	PipelineState.RasterizerState = ERasterizerState::SolidBack;  // 앞면만 → 바디 하나는 한 겹으로 보인다
	PipelineState.BlendState = EBlendState::AlphaBlend;
	PipelineState.DepthStencilState = EDepthStencilState::Disabled; // 메시 안의 바디도 보이게
	ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FPhysicsBodyConstants));
	SphereMesh = UAssetManager::GetAssetByPath<UStaticMesh>("Sphere");
	CubeMesh = UAssetManager::GetAssetByPath<UStaticMesh>("GizmoCube");
}

FPhysicsBodyRenderer::~FPhysicsBodyRenderer() = default;

bool FPhysicsBodyRenderer::Begin(const FMatrix& InViewProjection, const FVector& InCameraPosition)
{
	if (!PipelineState.Shader || !ConstantBuffer || !SphereMesh || !CubeMesh)
		return false;
	ViewProjection = InViewProjection;
	CameraPosition = InCameraPosition;
	RenderCommand::BindPipelineState(PipelineState);
	return true;
}

void FPhysicsBodyRenderer::DrawBody(const UBodySetup& Setup, const FMatrix& World, const FVector& Scale, const FVector4& Color)
{
	if (Scale.X <= 0 || Scale.Y <= 0 || Scale.Z <= 0)
		return;
	const FKAggregateGeom& Geom = Setup.AggGeom;

	for (const FKSphereElem& Sphere : Geom.SphereElems)
	{
		if (Sphere.Radius <= 0)
			continue;
		const FMatrix Frame = FMatrix::MakeTranslation(World.TransformPosition(Sphere.Center));
		const float Size = Sphere.Radius * Scale.X / SphereMeshRadius;
		DrawShape(SphereMesh, Frame, FVector(Size, Size, Size), 0.0f, Color);
	}
	for (const FKSphylElem& Capsule : Geom.SphylElems)
	{
		if (Capsule.Radius <= 0 || Capsule.Length < 0)
			continue;
		// 캡슐 축 = 형상 로컬 Z
		const FMatrix Frame = FTransform(Capsule.Rotation, Capsule.Center).GetLocalMatrix() * World;
		const float Size = Capsule.Radius * Scale.X / SphereMeshRadius;
		DrawShape(SphereMesh, Frame, FVector(Size, Size, Size), Capsule.Length * 0.5f * Scale.X, Color);
	}
	for (const FKBoxElem& Box : Geom.BoxElems)
	{
		if (Box.X <= 0 || Box.Y <= 0 || Box.Z <= 0)
			continue;
		const FMatrix Frame = FTransform(Box.Rotation, Box.Center).GetLocalMatrix() * World;
		DrawShape(CubeMesh, Frame, FVector(Box.X, Box.Y, Box.Z), 0.0f, Color);
	}
}

void FPhysicsBodyRenderer::DrawShape(UStaticMesh* Mesh, const FMatrix& World, const FVector& ShapeScale, float HalfLength, const FVector4& Color)
{
	FPhysicsBodyConstants Constants;
	Constants.World = World;
	Constants.ViewProjection = ViewProjection;
	Constants.Color = Color;
	Constants.ShapeScale = ShapeScale;
	Constants.HalfLength = HalfLength;
	Constants.CameraPosition = CameraPosition;

	RenderCommand::BindMesh(Mesh);
	RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Constants, sizeof(Constants));
	RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	RenderCommand::DrawIndexed(Mesh->GetIndexBuffer()->GetIndexCount());
}
