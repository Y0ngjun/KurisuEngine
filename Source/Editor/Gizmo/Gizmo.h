#pragma once

#include "Collision/Ray.h"
#include "Math/Transform.h"
#include "Components/SceneComponent.h"
#include "Components/LocalLightComponent.h"

class UCameraComponent;
class FGizmoRenderer;
struct FSceneView;

enum class EGizmoMode
{
	Location,
	Rotation,
	Scale,
	Select,
};

enum class EGizmoSpace
{
	Local,
	World,
};

class FGizmo // 상태 + 로직
{
public:
	FGizmo();
	~FGizmo();

	// 이 기즈모를 View의 카메라 기준으로 그린다. 씬 위에 올리는 패스 안에서 부른다.
	// UE의 FWidget::Render와 같은 자리다. UE는 Render(View, PDI, ViewportClient)이고 이 엔진은 View만 받는다.
	void Render(const FSceneView* View) const;

	// Gizmo 대상 설정
	void SetTarget(USceneComponent* InTarget, bool InbUseActorBounds = false)
	{
		if (Target != InTarget)
			EndDrag();
		Target = InTarget;
		bUseActorBounds = InbUseActorBounds;
	}
	USceneComponent* GetTarget() const { return Target; }

	// Gizmo 모드(이동, 회전, 크기)
	void SetMode(EGizmoMode InMode)
	{
		if (Mode != InMode)
			EndDrag();
		Mode = InMode;
	}
	EGizmoMode GetMode() const { return Mode; }

	void SetSpace(EGizmoSpace InSpace)
	{
		if (Space != InSpace)
			EndDrag();
		Space = InSpace;
	}
	EGizmoSpace GetSpace() const { return Space; }

	// 켜면 이동과 크기는 Step 단위로, 회전은 고정 각도 단위로 맞춘다.
	void SetLocationSnap(bool bEnabled, float Step)
	{
		bLocationSnapEnabled = bEnabled && Step > 0.0f;
		LocationSnapSize = bLocationSnapEnabled ? Step : 0.0f;
	}
	// 화면에 보이는 기즈모 크기 배율 (기본 1). 그리기와 축 피킹에 같이 곱한다. 물리 에셋 에디터처럼 작은 대상에 쓴다.
	void SetScreenScale(float InScale) { ScreenScale = InScale > 0.0f ? InScale : 1.0f; }
	float GetScreenScale() const { return ScreenScale; }

	bool IsLocationSnapEnabled() const { return bLocationSnapEnabled; }
	float GetLocationSnapSize() const { return LocationSnapSize; }

	// Active View의 Ray와 행렬만 사용해 Hover·Drag 상태를 갱신한다.
	void Update(const FRay& MouseRay, const FVector2& MousePos,
	            const FMatrix& ViewProj, int ScreenW, int ScreenH,
	            bool bMouseDown, const FVector& CameraLocation, bool bCameraOrthographic, bool bMousePressed = true);

	bool IsUsing() const { return DraggingAxis >= 0; }
	int GetHoveredAxis() const { return HoveredAxis; }

	int PickAxis(const FVector2& MousePos, const FMatrix& ViewProj, int ScreenW, int ScreenH);

	int PickLinearAxis(const FVector2& MousePos, const FMatrix& ViewProj, int ScreenW, int ScreenH);
	int PickRotationAxis(const FVector2& MousePos, const FMatrix& ViewProj, int ScreenW, int ScreenH);

	void BeginDrag(int Axis, const FRay& MouseRay, const FVector2& MousePos);
	void UpdateDrag(const FRay& MouseRay, const FVector2& MousePos);
	void EndDrag();

	float ComputeAngleOnPlane(const FVector& Point, int Axis) const;

	inline FTransform GetTransform() const { return Target ? Target->GetTransform() : FTransform(); }
	inline FVector GetLocation() const { return Target ? Target->GetRelativeLocation() : FVector(0, 0, 0); }
	inline FRotator GetRotation() const { return Target ? Target->GetRelativeRotation() : FRotator(0, 0, 0); }
	inline FVector GetScale() const { return Target ? Target->GetRelativeScale3D() : FVector(0, 0, 0); }
	FRotator GetAxisRotation() const;

	FVector GetRenderLocation() const;
	// 직교는 실제 피벗에서 픽셀 크기를 보정하고, 원근은 기존 표시 위치를 유지한다.
	FVector GetRenderLocationForView(const FVector& CameraLocation, bool bCameraOrthographic) const;
	FVector GetCameraLocation() const;
	static float GetRenderScaleForView(const FMatrix& ViewProj, const FVector& Origin, float ViewWidth);
	FVector GetAxisDirection(int Axis) const;

private:
	EGizmoMode Mode = EGizmoMode::Location;
	EGizmoSpace Space = EGizmoSpace::Local;
	bool bLocationSnapEnabled = false;
	float ScreenScale = 1.0f;
	float LocationSnapSize = 10.0f; // cm
	FTransform Transform = FTransform();
	USceneComponent* Target = nullptr;
	bool bUseActorBounds = false;

	int HoveredAxis = -1;
	int DraggingAxis = -1;

	FVector2 DragStartMousePos;
	FVector DragStartPoint;
	FVector DragStartLocation;
	FRotator DragStartRotation;
	FVector DragStartScale;
	float DragStartGizmoScale = 1.0f;
	FVector DragAxisDirection;
	FVector DragStartRenderLocation;

	FVector DragPlaneNormal;

	FVector ViewCameraLocation{};
	bool bViewCameraOrthographic = false;
	float ViewGizmoScale = 1.0f;
	FVector ViewCameraDirection{1.0f, 0.0f, 0.0f};

	float DragStartAngle;
	float RingRadius = 1.0f;

	// Light Component인 경우 Radius를 증감하기 위한 멤버변수와 함수
	float DragStartRadius = 0.0f;
	bool ApplyScaleToLight(float Factor);
	ULocalLightComponent* GetTargetLight() const;

	// 축 메시와 셰이더로 실제 그리기를 맡는 도구. 기즈모마다 하나씩 갖는다.
	TUniquePtr<FGizmoRenderer> Renderer;
};
