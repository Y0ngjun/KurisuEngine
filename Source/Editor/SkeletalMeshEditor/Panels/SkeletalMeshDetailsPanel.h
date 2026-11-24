#pragma once

#include "SkeletalMeshEditorPanel.h"

// 메시 상세 정보 UI를 확장하기 위한 패널 골격입니다.
class FSkeletalMeshDetailsPanel : public FSkeletalMeshEditorPanel
{
public:
	explicit FSkeletalMeshDetailsPanel(FSkeletalMeshEditor& InEditor);

protected:
	void DrawContent() override;

private:
	void DrawAssetSettings();
	FString EditError;
	int32 ClothSection = 0;
	int32 ClothAsset = 0;
	int32 ClothLOD = 0;
	float ClothMaxDistance = 25.0f;
};
