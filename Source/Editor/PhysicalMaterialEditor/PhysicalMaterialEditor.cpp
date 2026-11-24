#include "EnginePCH.h"
#include "PhysicalMaterialEditor.h"
#include "Editor/AssetEditor/AssetEditorTab.h"
#include "Editor/Details/PropertyEditor.h"
#include "Editor/KurisuEd/EditorUndo.h"
#include "Physics/PhysicalMaterial.h"
#include "imgui.h"

namespace
{
class FPhysicalMaterialEditorTab : public FAssetEditorTab
{
public:
    explicit FPhysicalMaterialEditorTab(FPhysicalMaterialEditor& Editor) : FAssetEditorTab(Editor), Owner(Editor) {}
    void OnRender(bool bActive) override { if (bActive) Owner.Draw(); }
private:
    FPhysicalMaterialEditor& Owner;
};
}

FPhysicalMaterialEditor::FPhysicalMaterialEditor(UEditorEngine& Engine, FEditorManager& Manager, const FString& Path, UPhysicalMaterial& InMaterial)
    : FAssetEditor(Engine, Manager, Path), Material(InMaterial) {}

TUniquePtr<IEditorTab> FPhysicalMaterialEditor::CreateTab() { return MakeUnique<FPhysicalMaterialEditorTab>(*this); }
bool FPhysicalMaterialEditor::IsDirty() const { return Material.IsDirty(); }
bool FPhysicalMaterialEditor::ConfirmClose() { if (!IsDirty()) return true; bClosePrompt = true; return false; }

void FPhysicalMaterialEditor::CommitEdit()
{
    if (PendingBefore.IsEmpty()) return;
    TArray<uint8> After;
    EditorUndoUtils::CaptureObjectBytes(&Material, After);
    History.resize(HistoryPosition);
    History.push_back({std::move(PendingBefore), std::move(After)});
    HistoryPosition = History.size();
    PendingBefore.Empty();
}

void FPhysicalMaterialEditor::Draw()
{
    ImGui::PushID(this);
    ImGui::BeginChild("PhysicalMaterialProperties", ImVec2(0,0), ImGuiChildFlags_AlwaysUseWindowPadding);
    if (ImGui::Button("Save") || (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)))
    { CommitEdit(); Error = Material.SaveAsset() ? "" : "Could not save the physical material."; }
    ImGui::SameLine();
    ImGui::BeginDisabled(HistoryPosition == 0 && PendingBefore.IsEmpty());
    if (ImGui::Button("Undo") || (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)))
    { CommitEdit(); if (HistoryPosition > 0) EditorUndoUtils::ApplyObjectBytes(&Material, History[--HistoryPosition].Before); }
    ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::BeginDisabled(HistoryPosition >= History.size());
    if (ImGui::Button("Redo") || (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)))
    { if (HistoryPosition < History.size()) EditorUndoUtils::ApplyObjectBytes(&Material, History[HistoryPosition++].After); }
    ImGui::EndDisabled();
    ImGui::TextUnformatted(Material.GetAssetPath().c_str());
    ImGui::Separator();
    TArray<uint8> Before;
    EditorUndoUtils::CaptureObjectBytes(&Material, Before);
    if (FPropertyEditor::DrawObjectProperties(&Material) && PendingBefore.IsEmpty()) PendingBefore = std::move(Before);
    if (!ImGui::IsAnyItemActive()) CommitEdit();
    if (!Error.empty()) ImGui::TextWrapped("%s", Error.c_str());
    if (bClosePrompt) { ImGui::OpenPopup("Unsaved Physical Material"); bClosePrompt = false; }
    if (ImGui::BeginPopupModal("Unsaved Physical Material", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Save changes before closing?");
        if (ImGui::Button("Save and Close"))
        { CommitEdit(); if (Material.SaveAsset()) { ImGui::CloseCurrentPopup(); Close(); } else Error = "Could not save the physical material."; }
        ImGui::SameLine();
        if (ImGui::Button("Discard"))
        { if (Material.ReloadAsset()) { ImGui::CloseCurrentPopup(); Close(); } else Error = "Could not reload the physical material."; }
        ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::EndChild(); ImGui::PopID();
}
