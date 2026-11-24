#include "EnginePCH.h"
#include "PhysicsAssetEditorTab.h"
#include "PhysicsAssetEditor.h"
#include "Editor/SkeletalPreviewEditor/SkeletalPreviewViewportPanel.h"
#include "Panels/PhysicsAssetDetailsPanel.h"
#include "Panels/PhysicsAssetSkeletonTreePanel.h"
#include "imgui.h"

FPhysicsAssetEditorTab::FPhysicsAssetEditorTab(FPhysicsAssetEditor& InEditor)
	: FDockedAssetEditorTab(InEditor, "PhysicsAssetDockSpace", "PhysicsAssetPanelClass"), PhysicsAssetEditor(InEditor)
{
	RegisterPanel(MakeUnique<FSkeletalPreviewViewportPanel>(InEditor));
	RegisterPanel(MakeUnique<FPhysicsAssetDetailsPanel>(InEditor));
	RegisterPanel(MakeUnique<FPhysicsAssetSkeletonTreePanel>(InEditor));
}

void FPhysicsAssetEditorTab::OnRender(bool bActive)
{
	if (bActive && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
		PhysicsAssetEditor.Save();
	FDockedAssetEditorTab::OnRender(bActive);
}

bool FPhysicsAssetEditorTab::DrawFileMenuItems()
{
	if (ImGui::MenuItem("Save", "Ctrl+S"))
		PhysicsAssetEditor.Save();
	return true;
}

void FPhysicsAssetEditorTab::BeforeRender()
{
	// 패널이 그려지지 않은 프레임에는 프리뷰 입력을 비운다.
	PhysicsAssetEditor.SetViewportState(0, 0, {});
}

void FPhysicsAssetEditorTab::BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size)
{
	ImGui::DockBuilderAddNode(InDockSpaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(InDockSpaceId, Size);

	// UE처럼 왼쪽 Skeleton Tree, 가운데 뷰포트, 오른쪽 Details
	ImGuiID ViewportId = InDockSpaceId;
	const ImGuiID TreeId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Left, 0.22f, nullptr, &ViewportId);
	const ImGuiID DetailsId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Right, 0.30f, nullptr, &ViewportId);

	ImGui::DockBuilderDockWindow(Panels[0]->GetWindowName(), ViewportId);
	ImGui::DockBuilderDockWindow(Panels[1]->GetWindowName(), DetailsId);
	ImGui::DockBuilderDockWindow(Panels[2]->GetWindowName(), TreeId);
	ImGui::DockBuilderFinish(InDockSpaceId);
}
