#pragma once

#include "Editor/SkeletalPreviewEditor/SkeletalPreviewEditor.h"
#include "Editor/Gizmo/Gizmo.h"

class UPhysicsAsset;
class FPhysicsBodyRenderer;

// UE Physics Asset Editor. .physicsasset 파일 하나를 열고, 그 에셋의 미리보기 스켈레탈 메시를 뷰포트에 띄운다.
// 지금은 메시 미리보기와 개수 표시까지만 있다 (바디·관절 그리기, 본 트리, 디테일은 다음 단계).
class FPhysicsAssetEditor : public FSkeletalPreviewEditor
{
public:
	FPhysicsAssetEditor(UEditorEngine& Engine, FEditorManager& Manager, const FString& Path);
	~FPhysicsAssetEditor() override;

	static bool IsPhysicsAssetPath(const FString& Path);

	const char* GetEditorTypeName() const override { return "Physics Asset"; }
	bool UsesPreviewSkybox() const override { return true; }
	bool IsDirty() const override;
	// 편집 중인 에셋을 열었던 파일에 저장한다. 그 파일이 메시에 연결된 에셋이면 메시 쪽 에셋도 다시 읽어 실행 바디에 반영한다.
	bool Save();
	// 부모가 그리는 본(기본은 꺼 둠) 위에 바디 형상을 그린다.
	void OnRenderLine(FLineBatcher& LineBatcher) override;
	// 바디를 반투명 면으로 칠한다 (선은 그 위에 테두리로 그린다).
	void OnRenderOverlay(const FSceneView& View) override;
	// 선택한 바디의 첫 형상 위에 기즈모를 띄운다. 드래그하면 형상의 위치(Center)·회전(Rotation)·크기를 바꾼다.
	const FGizmo* GetPreviewGizmo() const override { return BodyGizmo.GetTarget() ? &BodyGizmo : nullptr; }

	UPhysicsAsset* GetPhysicsAsset() const { return PhysicsAsset; }
	const FString& GetPreviewMeshPath() const { return PreviewMeshPath; }

	// 트리·뷰포트·디테일이 함께 보는 선택. 바디와 관절 중 하나만 고른다 (본만 고르면 둘 다 -1).
	int32 GetSelectedBodyIndex() const { return SelectedBodyIndex; }
	int32 GetSelectedConstraintIndex() const { return SelectedConstraintIndex; }
	void SelectBody(int32 BodyIndex);
	void SelectConstraint(int32 ConstraintIndex);
	// 바디·관절이 붙은 본 번호 (없으면 -1)
	int32 GetBodyBoneIndex(int32 BodyIndex) const;
	int32 GetConstraintBoneIndex(int32 ConstraintIndex) const;

	// W/E/R 또는 뷰포트 단축키로 바꾼다 (스켈레탈 메시 에디터와 같다).
	void SetGizmoMode(EGizmoMode Mode);
	EGizmoMode GetGizmoMode() const { return BodyGizmo.GetMode(); }
	bool IsGizmoDragging() const { return BodyGizmo.IsUsing(); }
	// 뷰포트 왼쪽 위 이동/회전/크기 버튼
	int32 GetViewportTransformMode() const override { return static_cast<int32>(BodyGizmo.GetMode()); }
	void SetViewportTransformMode(int32 Mode) override { SetGizmoMode(static_cast<EGizmoMode>(Mode)); }

protected:
	bool InitializeResources() override;
	void ShutdownResources() override;
	TUniquePtr<IEditorTab> CreateTab() override;
	// 본을 고르면(트리·뷰포트) 바디·관절 선택은 푼다.
	void OnBoneSelected() override;
	// 뷰포트 클릭으로 바디를 고른다 (본 그리기가 켜져 있고 바디가 안 맞으면 본을 고른다).
	void TickAfterWorld(float DeltaTime) override;
	bool IsViewportInteractionBusy() const override { return BodyGizmo.IsUsing(); }
	void CancelViewportInteraction() override { BodyGizmo.EndDrag(); }

private:
	// 마우스 위치에서 쏜 광선에 가장 먼저 맞는 바디 번호 (없으면 -1). Convex는 아직 고르지 않는다.
	int32 PickBody(const FVector2& MousePosition) const;
	void UpdateBodyPicking();
	// 선택한 바디의 첫 형상 위치·회전으로 기즈모 대상을 옮긴다. 선택이 없으면 기즈모를 끈다.
	void SyncBodyGizmo();
	// 기즈모 입력을 처리한다. 기즈모가 마우스를 썼으면 true (그 프레임은 바디 피킹을 하지 않는다).
	bool UpdateBodyGizmo();
	// 드래그 중인 기즈모 대상 Transform을 형상 값으로 되돌려 쓴다.
	void ApplyBodyGizmo();
	// 뷰포트가 포커스일 때 W/E/R로 기즈모 모드를 바꾼다.
	void UpdateGizmoModeKeys();

	// 에셋에 저장된 미리보기 메시 경로. 없으면(이전 파일) 과도기 저장 경로에서 메시 경로를 되찾는다.
	FString ResolvePreviewMeshPath() const;

	// 파일에서 읽은 편집 대상. 이 에디터가 소유한다.
	UPhysicsAsset* PhysicsAsset = nullptr;
	FString PreviewMeshPath;
	// 처음 그릴 때 만든다 (셰이더·기본 메시가 준비된 뒤)
	TUniquePtr<FPhysicsBodyRenderer> BodyRenderer;
	int32 SelectedBodyIndex = -1;
	int32 SelectedConstraintIndex = -1;
	// 기즈모가 따라갈 빈 컴포넌트 (스켈레탈 메시 에디터의 BoneGizmoTarget과 같은 방식)
	USceneComponent* BodyGizmoTarget = nullptr;
	FGizmo BodyGizmo;
	// 드래그 시작 때의 형상 값. 크기 배율은 이 값에 곱하고, Esc를 누르면 이 값으로 되돌린다.
	struct FShapeDragStart
	{
		int32 BodyIndex = -1;
		FMatrix BoneFrameInverse;
		FVector Center;
		FRotator Rotation;
		FTransform ConvexTransform;
		float Radius = 0.0f;
		float Length = 0.0f;
		FVector BoxSize;
	} DragStart;
};
