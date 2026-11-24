#pragma once

#include "Render/PipelineState.h"

class FConstantBuffer;
class FLineBatcher;
class UBodySetup;
class UStaticMesh;

// 물리 에셋 에디터의 반투명 바디 그리기 (기즈모 렌더러처럼 에디터 오버레이에서 직접 그린다).
// 구 메시 하나로 구·캡슐, 정육면체 메시 하나로 박스를 그린다. 크기는 셰이더 상수로 넘긴다 (PhysicsBodyShader.hlsl).
// 깊이 검사를 하지 않아 메시 안에 있는 바디도 보인다. Convex는 아직 선으로만 그린다.
class FPhysicsBodyRenderer
{
public:
	FPhysicsBodyRenderer();
	~FPhysicsBodyRenderer();

	// 한 프레임 시작. 셰이더·메시가 없으면 false (그리지 않는다).
	bool Begin(const FMatrix& ViewProjection, const FVector& CameraPosition);
	// UBodySetup::DrawDebug와 같은 규칙: World = 스케일 없는 본 프레임, Scale = 컴포넌트 스케일
	void DrawBody(const UBodySetup& Setup, const FMatrix& World, const FVector& Scale, const FVector4& Color);

private:
	void DrawShape(UStaticMesh* Mesh, const FMatrix& World, const FVector& ShapeScale, float HalfLength, const FVector4& Color);

	FPipelineState PipelineState;
	TUniquePtr<FConstantBuffer> ConstantBuffer;
	UStaticMesh* SphereMesh = nullptr; // 엔진 기본 "Sphere" (반지름 100)
	UStaticMesh* CubeMesh = nullptr;   // 엔진 기본 "GizmoCube" (한 변 1)
	FMatrix ViewProjection = FMatrix::Identity;
	FVector CameraPosition = FVector::ZeroVector;
};
