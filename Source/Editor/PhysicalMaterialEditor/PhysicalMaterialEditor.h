#pragma once
#include "Editor/AssetEditor/AssetEditor.h"
#include <vector>
class UPhysicalMaterial;

class FPhysicalMaterialEditor : public FAssetEditor
{
public:
    FPhysicalMaterialEditor(UEditorEngine& Engine, FEditorManager& Manager, const FString& Path, UPhysicalMaterial& InMaterial);
    const char* GetEditorTypeName() const override { return "Physical Material"; }
    bool IsDirty() const override;
    bool ConfirmClose() override;
    void Draw();
protected:
    bool InitializeResources() override { return true; }
    TUniquePtr<IEditorTab> CreateTab() override;
private:
    void CommitEdit();
    UPhysicalMaterial& Material;
    struct FEdit { TArray<uint8> Before; TArray<uint8> After; };
    std::vector<FEdit> History;
    size_t HistoryPosition = 0;
    TArray<uint8> PendingBefore;
    FString Error;
    bool bClosePrompt = false;
};
