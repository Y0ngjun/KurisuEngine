#pragma once

#include "Editor/AssetEditor/DockedAssetEditorTab.h"

class FPhysicsAssetEditor;

// 물리 에셋 에디터 탭. 뷰포트와 에셋 패널들을 도킹 영역에 배치한다.
class FPhysicsAssetEditorTab : public FDockedAssetEditorTab
{
public:
	explicit FPhysicsAssetEditorTab(FPhysicsAssetEditor& InEditor);

private:
	void OnRender(bool bActive) override;
	void BeforeRender() override;
	bool DrawFileMenuItems() override;
	void BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size) override;

	FPhysicsAssetEditor& PhysicsAssetEditor;
};
