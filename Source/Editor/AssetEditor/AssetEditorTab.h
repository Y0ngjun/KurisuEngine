#pragma once

#include "Editor/EditorUI/EditorTab.h"
#include "Core/EngineString.h"

class FAssetEditor;

class FAssetEditorTab : public IEditorTab
{
public:
	explicit FAssetEditorTab(FAssetEditor& InEditor);

	// 탭의 메뉴 바를 그립니다.
	void DrawMenuBar() override;

	// 탭의 렌더링을 수행합니다.
	void OnRender(bool bActive) override;

	// 탭이 닫히기 전에 호출됩니다.
	void OnCloseRequested() override;

	////////////////////////////////////////////////////////////
	// Getter
	////////////////////////////////////////////////////////////

	// 보이는 이름 = 파일 이름(확장자 제외, 길면 가운데를 ...으로 줄임) + 저장 안 됨이면 * + (에디터 종류).
	// *를 이름 바로 뒤에 둬서 탭이 좁아져 뒤쪽이 잘려도 * 는 남게 한다 (UE 탭과 같은 모양). ### 뒤 ID는 같아 창 배치가 유지된다.
	const char* GetTabName() const override;
	bool CanClose() const override { return true; }
	bool ConfirmClose() override;
	bool HasUnsavedChanges() const override;

private:
	FAssetEditor& Editor;
	FString TabId;
	// GetTabName이 경로나 저장 상태가 바뀌었을 때 다시 만든다
	mutable FString NamedPath;
	mutable bool bNamedDirty = false;
	mutable FString TabName;
};
