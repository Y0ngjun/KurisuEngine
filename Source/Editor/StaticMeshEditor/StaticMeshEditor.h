#pragma once

#include "Editor/AssetEditor/AssetEditor.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Camera/ViewportCameraMovement.h"
#include <vector>

class FStaticMeshEditor : public FAssetEditor
{
public:
	FStaticMeshEditor(
	    class UEditorEngine& InEditorEngine,
	    class FEditorManager& InEditorManager,
	    const FString& InAssetPath,
	    class UStaticMesh& InMesh);

	////////////////////////////////////////////////////////////
	// 에디터 공통 인터페이스
	////////////////////////////////////////////////////////////

	// 틱을 돌립니다.
	void Tick(float DeltaTime) override;

	// 현재 상태를 렌더링합니다.
	void OnRenderPreview() override;

	////////////////////////////////////////////////////////////
	// 에디터 전용 함수
	////////////////////////////////////////////////////////////

	// 카메라를 메시에 포커싱합니다.
	void FocusMesh();

	// 뷰포트 상태를 설정합니다. (뷰포트가 보이지 않으면 입력을 무시합니다)
	void SetViewportState(uint32 Width, uint32 Height, const FViewportCameraInput& Input);

	////////////////////////////////////////////////////////////
	// Getter
	////////////////////////////////////////////////////////////

	const char* GetEditorTypeName() const override { return "StaticMesh"; }
	UStaticMesh* GetMesh() const { return Mesh; }

	////////////////////////////////////////////////////////////
	// 충돌(BodySetup) 편집
	////////////////////////////////////////////////////////////

	// 저장하지 않은 충돌 편집이 있는지. 닫을 때 확인 창을 띄운다.
	bool IsDirty() const override;
	bool ConfirmClose() override;

	// 이번 프레임에 충돌 설정이 바뀌었음을 알린다. 입력이 끝나면 한 번의 Undo 단계로 묶는다.
	void NotifyCollisionChanged();
	// 드래그 중인 입력이 끝났으면 대기 중인 변경을 Undo 기록으로 확정한다.
	void CommitCollisionEdit();
	bool CanUndoCollision() const { return HistoryPosition > 0 || bPendingEdit; }
	bool CanRedoCollision() const { return HistoryPosition < History.size(); }
	void UndoCollision();
	void RedoCollision();
	bool SaveCollision();
	void RevertCollision();

	bool bShowCollision = true;
	FString CollisionError;

	// 닫기 확인 창. 탭이 매 프레임 호출한다.
	void DrawClosePrompt();

	// 미리보기 뷰포트에 충돌 형상을 그린다.
	void OnRenderLine(class FLineBatcher& LineBatcher) override;

protected:
	// 이 에디터에 연결된 탭을 생성합니다.
	TUniquePtr<IEditorTab> CreateTab() override;

	// 리소스를 초기화합니다.
	bool InitializeResources() override;

	// 리소스를 정리합니다.
	void ShutdownResources() override;

private:
	class UStaticMesh* Mesh = nullptr;
	class UStaticMeshComponent* MeshComponent = nullptr;

	FViewportCameraInput CameraInput;

	uint32 ViewportWidth = 0;
	uint32 ViewportHeight = 0;

	bool bViewportVisible = false;
	bool bInitialFocusPending = true;

	struct FCollisionEdit
	{
		TArray<uint8> Before;
		TArray<uint8> After;
	};
	std::vector<FCollisionEdit> History;
	size_t HistoryPosition = 0;
	TArray<uint8> CommittedSnapshot; // 마지막으로 확정된 BodySetup 상태
	bool bPendingEdit = false;
	bool bClosePrompt = false;
};
