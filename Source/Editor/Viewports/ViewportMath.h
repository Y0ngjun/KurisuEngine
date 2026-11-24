#pragma once
#include "Math/EngineMath.h"

namespace EditorViewportMath
{
	inline FMatrix PerspectiveGizmoProjection(const FMatrix& SceneProjection, float FarClip)
	{
		// Perspective handles live 10cm in front of the camera. Scene near clipping
		// (normally 10cm) must not cut this editor overlay. Keep its screen X/Y/W.
		constexpr float GizmoNearClip = 0.01f;
		FMatrix Projection = SceneProjection;
		Projection.M[0][2] = FarClip / (FarClip - GizmoNearClip);
		Projection.M[3][2] = -GizmoNearClip * Projection.M[0][2];
		return Projection;
	}

	inline FVector ScreenRight(const FVector& Forward)
	{
		const FVector ReferenceUp = std::abs(Forward.Z) > 0.99f ? FVector(0, 1, 0) : FVector(0, 0, 1);
		return ReferenceUp.Cross(Forward).Normalized();
	}
	// Editor grid values count 10cm steps; a 100cm cube spans 10 cells at 1.
	inline float GridCellSize(int32 GridValue)
	{
		return FMath::Max(GridValue, 1) * 10.0f;
	}

	// Normalized gizmo axes are 1.5 units long. Keep them 90 screen pixels long.
	inline float GizmoWorldScale(const FMatrix& ViewProjection, const FVector& Origin, float ViewWidth)
	{
		const float ProjectionScale = std::sqrt(
		    ViewProjection.M[0][0] * ViewProjection.M[0][0] +
		    ViewProjection.M[1][0] * ViewProjection.M[1][0] +
		    ViewProjection.M[2][0] * ViewProjection.M[2][0]);
		const FVector4 Clip = FVector4(Origin, 1.0f) * ViewProjection;
		return 120.0f * std::abs(Clip.W) / FMath::Max(ViewWidth * ProjectionScale, 1.0e-8f);
	}

	inline FMatrix OrthographicProjection(float Width, float Aspect, float HalfDepth)
	{
		// Editor orthographic views include both sides of their pan plane.
		return FMatrix(0, 0, 0.5f / HalfDepth, 0,
		               2.0f / Width, 0, 0, 0,
		               0, 2.0f * Aspect / Width, 0, 0,
		               0, 0, 0.5f, 1);
	}
}
