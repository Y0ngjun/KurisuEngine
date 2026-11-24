#include "EnginePCH.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Editor/AssetEditor/AssetEditorTab.h"
#include "Editor/AssetEditor/AssetEditor.h"
#include "Core/Misc/Paths.h"
#include <filesystem>

FAssetEditorTab::FAssetEditorTab(FAssetEditor& InEditor) : Editor(InEditor)
{
	// ### 뒤 ID는 처음 연 경로로 고정한다. 에디터 안에서 애셋이 바뀌어도(애니메이션 뷰어의 Asset Browser) 같은 창으로 남는다.
	TabId = FString("###AssetEditor:") + Editor.GetAssetPath();
}

const char* FAssetEditorTab::GetTabName() const
{
	// 보이는 이름은 지금 경로와 저장 상태로 만든다. 둘 중 하나가 바뀐 때만 다시 만든다.
	const bool bDirty = Editor.IsDirty();
	if (NamedPath != Editor.GetAssetPath() || bNamedDirty != bDirty)
	{
		NamedPath = Editor.GetAssetPath();
		bNamedDirty = bDirty;

		// 글자 단위로 자르도록 UTF-16으로 바꿔 줄인다 (UTF-8 바이트로 자르면 한글이 깨진다).
		constexpr size_t MaxChars = 24;
		constexpr size_t HeadChars = 10;
		std::wstring Stem = FPaths::FromUtf8(NamedPath).stem().wstring();
		if (Stem.size() > MaxChars)
			Stem = Stem.substr(0, HeadChars) + L"..." + Stem.substr(Stem.size() - (MaxChars - HeadChars - 3));

		TabName = FPaths::ToUtf8(std::filesystem::path(Stem)) + (bDirty ? "*" : "") +
		          " (" + Editor.GetEditorTypeName() + ")" + TabId;
	}
	return TabName.c_str();
}

bool FAssetEditorTab::ConfirmClose()
{
	return Editor.ConfirmClose();
}

bool FAssetEditorTab::HasUnsavedChanges() const
{
	return Editor.IsDirty();
}

void FAssetEditorTab::OnCloseRequested()
{
	Editor.Close();
}

void FAssetEditorTab::DrawMenuBar()
{
	// 기본 메뉴 바. 레벨 에디터와 같이 File 메뉴부터 둔다.

	if (EditorStyle::BeginMenuBarMenu("File"))
	{
		if (ImGui::MenuItem("Close Editor"))
		{
			Editor.Close();
		}

		ImGui::EndMenu();
	}
}

void FAssetEditorTab::OnRender(bool bActive)
{
}
