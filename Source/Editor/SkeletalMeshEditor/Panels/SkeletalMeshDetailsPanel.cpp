#include "EnginePCH.h"
#include "SkeletalMeshDetailsPanel.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Math/Transform.h"
#include "Core/Misc/Paths.h"
#include "Render/SkeletalMesh.h"
#include <algorithm>
#include <vector>
#include "Editor/Details/PropertyEditor.h"

FSkeletalMeshDetailsPanel::FSkeletalMeshDetailsPanel(FSkeletalMeshEditor& InEditor)
    : FSkeletalMeshEditorPanel(InEditor, "Details")
{
}

void FSkeletalMeshDetailsPanel::DrawAssetSettings()
{
	auto* Mesh = Editor.GetMesh();
	if (!Mesh || !Mesh->IsSkeletalMeshAsset())
		return;
	if (Mesh->AssetNeedsReload())
	{
		ImGui::TextWrapped("Restart the editor to use the reimported mesh.");
		return;
	}
	if (!Editor.GetAssetMessage().empty())
		ImGui::TextWrapped("%s", Editor.GetAssetMessage().c_str());
	if (!EditError.empty())
		ImGui::TextWrapped("%s", EditError.c_str());

	const auto& PhysicsPath = Mesh->GetPhysicsAssetPath();
	if (ImGui::BeginCombo("Physics Asset", PhysicsPath.empty() ? "None" : PhysicsPath.c_str()))
	{
		if (ImGui::Selectable("None", PhysicsPath.empty()))
			Mesh->AssignPhysicsAssetPath({}, EditError);
		std::error_code Error;
		for (std::filesystem::recursive_directory_iterator It(FPaths::ProjectContentDir(), Error), End;
		     !Error && It != End; It.increment(Error))
		{
			if (It->path().extension() != ".physicsasset" || !It->is_regular_file(Error))
				continue;
			const auto Path = FPaths::ToUtf8(It->path());
			const auto Reference = SkeletalMeshAssetFile::ProjectContentReference(Path);
			if (!Reference.empty() && ImGui::Selectable(Reference.c_str(), Reference == PhysicsPath))
				Mesh->AssignPhysicsAssetPath(Path, EditError);
		}
		ImGui::EndCombo();
	}

	if (!ImGui::CollapsingHeader("Cloth"))
		return;
	const auto& LOD = Mesh->GetRenderData().LODRenderData[0];
	int32 SelectedSection = -1;
	if (ImGui::BeginCombo("Section", std::to_string(ClothSection).c_str()))
	{
		for (int32 Index = 0; Index < LOD.Sections.Num(); ++Index)
			if (ImGui::Selectable(std::to_string(Index).c_str(), Index == ClothSection))
				SelectedSection = Index;
		ImGui::EndCombo();
	}
	if (SelectedSection >= 0)
		ClothSection = SelectedSection;
	ImGui::DragFloat("New Cloth Max Distance", &ClothMaxDistance, 0.2f, 0.0f, 10000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (ImGui::Button("Create Cloth from Section"))
	{
		const std::vector<float> MaxDistances(LOD.BindVertices.Num(), std::max(0.0f, ClothMaxDistance));
		const int32 Index = Mesh->CreateClothingAssetFromSection(
		    0, ClothSection, "Cloth " + std::to_string(Mesh->GetClothingAssetCount() + 1), MaxDistances, EditError);
		if (Index >= 0)
		{
			ClothAsset = Index;
			ClothLOD = 0;
			Mesh->BindClothingSection(0, ClothSection, ClothAsset, ClothLOD, EditError);
		}
	}

	const auto* Selected = Mesh->GetClothingAsset(ClothAsset);
	if (ImGui::BeginCombo("Cloth Asset", Selected ? Selected->Name.c_str() : "None"))
	{
		for (int32 Index = 0; Index < Mesh->GetClothingAssetCount(); ++Index)
		{
			ImGui::PushID(Index);
			if (ImGui::Selectable(Mesh->GetClothingAsset(Index)->Name.c_str(), ClothAsset == Index))
			{
				ClothAsset = Index;
				ClothLOD = 0;
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	auto* Cloth = Mesh->GetClothingAsset(ClothAsset);
	if (!Cloth)
		return;

	ImGui::PushID(ClothAsset);
	char Name[256]{};
	std::snprintf(Name, sizeof(Name), "%s", Cloth->Name.c_str());
	bool Changed = ImGui::InputText("Name", Name, sizeof(Name));
	if (Changed)
		Cloth->Name = Name;

	if (FPropertyEditor::BeginPropertyGrid("ClothConfigGrid"))
	{
		const UScriptStruct& ConfigStruct = *FClothConfig::StaticStruct();
		for (const char* Member : {"GravityZ", "SolverFrequency", "Damping", "Stiffness", "Friction", "bEnableContinuousCollision"})
			Changed |= FPropertyEditor::DrawStructMember(Mesh, &Cloth->Config, ConfigStruct, Member);
		FPropertyEditor::EndPropertyGrid();
	}
	Changed |= ImGui::Checkbox("Use Physics Asset Collision", &Cloth->bUsePhysicsAssetCollision);
	ImGui::PopID();
	if (Changed)
		Mesh->MarkAssetDirty();

	ImGui::DragInt("Cloth LOD", &ClothLOD, 1.0f, 0, std::max(0, Cloth->LODs.Num() - 1), "%d", ImGuiSliderFlags_AlwaysClamp);
	ImGui::TextUnformatted(Mesh->FindClothingLOD(0, ClothSection) ? "Section bound" : "Section uses default skinning");
	if (ImGui::Button("Bind Section"))
		Mesh->BindClothingSection(0, ClothSection, ClothAsset, ClothLOD, EditError);
	ImGui::SameLine();
	if (ImGui::Button("Unbind Section"))
	{
		Mesh->UnbindClothingSection(0, ClothSection);
		EditError.clear();
	}
	ImGui::Separator();
}

void FSkeletalMeshDetailsPanel::DrawContent()
{
	DrawAssetSettings();
	const int32 BoneIndex = Editor.GetSelectedBoneIndex();

	if (ImGui::Button("Reset Transform"))
	{
		Editor.ResetBone(BoneIndex);
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset All Transform"))
	{
		Editor.ResetPose();
	}

	ImGui::Separator();

	FTransform Transform;
	if (!Editor.GetSelectedBoneLocalTransform(Transform))
	{
		ImGui::TextDisabled("Select a bone in Bone Hierarchy.");
		return;
	}

	// 선택이 바뀌면 이전 본의 편집 상태가 새 본에 이어지지 않도록 ID를 구분합니다.
	ImGui::PushID(BoneIndex);
	bool bChanged = false;
	// 레벨 디테일 패널과 같은 X/Y/Z 축 컨트롤을 쓴다 (축 버튼을 누르면 그 축만 초기화).
	bChanged |= FPropertyEditor::DrawVector3("Location", Transform.Location.V, 0.0f);
	// 드래그 중 값이 튀지 않도록 마지막 오일러 값을 기억한다
	static FRotationConversionCache RotationCache;
	FRotator Euler = RotationCache.QuatToRotator(Transform.Rotation);
	if (FPropertyEditor::DrawRotator("Rotation", Euler))
	{
		Transform.Rotation = RotationCache.RotatorToQuat(Euler);
		bChanged = true;
	}
	bChanged |= FPropertyEditor::DrawVector3("Scale", Transform.Scale.V, 1.0f);
	ImGui::PopID();

	if (bChanged)
	{
		Editor.SetSelectedBoneLocalTransform(Transform);
	}
}
