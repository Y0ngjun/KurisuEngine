#include "EnginePCH.h"
#include "PhysicsAssetDetailsPanel.h"

#include "Editor/Details/PropertyEditor.h"
#include "Physics/PhysicsAsset.h"
#include "imgui.h"

namespace
{
	// 표 안에 읽기 전용 한 줄 (이름 | 값)
	void DrawReadOnlyRow(const char* Label, const char* Value)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(Label);
		ImGui::TableSetColumnIndex(1);
		ImGui::BeginDisabled();
		ImGui::TextUnformatted(Value);
		ImGui::EndDisabled();
	}

	// 형상 하나: 접을 수 있는 줄 아래에 지정한 멤버만 그린다.
	template <typename TElem>
	bool DrawElem(UObject* Owner, TElem& Elem, const char* Kind, int32 Index, std::initializer_list<const char*> Members)
	{
		bool bChanged = false;
		ImGui::PushID(Kind);
		ImGui::PushID(Index);
		if (ImGui::TreeNodeEx("Elem", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth, "%s %d", Kind, Index))
		{
			if (FPropertyEditor::BeginPropertyGrid("ElemGrid"))
			{
				for (const char* Member : Members)
					bChanged |= FPropertyEditor::DrawStructMember(Owner, &Elem, *TElem::StaticStruct(), Member);
				FPropertyEditor::EndPropertyGrid();
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
		ImGui::PopID();
		return bChanged;
	}
}

FPhysicsAssetDetailsPanel::FPhysicsAssetDetailsPanel(FPhysicsAssetEditor& InEditor)
	: TAssetEditorPanel<FPhysicsAssetEditor>(InEditor, "Details", "PhysicsAssetPanel")
{
}

void FPhysicsAssetDetailsPanel::DrawContent()
{
	UPhysicsAsset* Asset = Editor.GetPhysicsAsset();
	if (!Asset)
		return;

	const int32 BodyIndex = Editor.GetSelectedBodyIndex();
	if (BodyIndex >= 0 && BodyIndex < Asset->SkeletalBodySetups.Num() && Asset->SkeletalBodySetups[BodyIndex])
		DrawBodyDetails(*Asset, BodyIndex);
	else
		DrawAssetInfo(*Asset);
}

void FPhysicsAssetDetailsPanel::DrawAssetInfo(const UPhysicsAsset& Asset)
{
	ImGui::TextDisabled("Asset");
	ImGui::TextWrapped("%s", Editor.GetAssetPath().c_str());
	ImGui::TextDisabled("Preview Mesh");
	ImGui::TextWrapped("%s", Editor.GetPreviewMeshPath().c_str());
	ImGui::Separator();
	ImGui::Text("Bodies: %d", Asset.SkeletalBodySetups.Num());
	ImGui::Text("Constraints: %d", Asset.ConstraintSetup.Num());
	ImGui::Spacing();
	ImGui::TextDisabled("Select a body to edit it.");
}

void FPhysicsAssetDetailsPanel::DrawBodyDetails(UPhysicsAsset& Asset, int32 BodyIndex)
{
	USkeletalBodySetup* Setup = Asset.SkeletalBodySetups[BodyIndex];
	bool bChanged = false;

	// 선택이 바뀌면 이전 바디의 편집 상태(펼침·드래그)가 이어지지 않게 ID를 나눈다.
	ImGui::PushID(BodyIndex);

	if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (FPropertyEditor::BeginPropertyGrid("PhysicsGrid"))
		{
			// UBodySetupCore::PhysicsType. DrawProperty가 PostEditChangeProperty까지 부른다.
			if (const FProperty* PhysicsType = Setup->GetClass()->FindPropertyByName("PhysicsType"))
				bChanged |= FPropertyEditor::DrawProperty(Setup, *PhysicsType);
			FPropertyEditor::EndPropertyGrid();
		}
	}

	if (ImGui::CollapsingHeader("Body Setup", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (FPropertyEditor::BeginPropertyGrid("BodySetupGrid"))
		{
			DrawReadOnlyRow("Bone Name", Setup->BoneName.ToString().c_str());
			FPropertyEditor::EndPropertyGrid();
		}

		ImGui::PushID("Primitives");
		if (ImGui::TreeNodeEx("Primitives", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
		{
			FKAggregateGeom& Geom = Setup->AggGeom;
			for (int32 i = 0; i < Geom.SphereElems.Num(); ++i)
				bChanged |= DrawElem(Setup, Geom.SphereElems[i], "Sphere", i, {"Center", "Radius"});
			for (int32 i = 0; i < Geom.BoxElems.Num(); ++i)
				bChanged |= DrawElem(Setup, Geom.BoxElems[i], "Box", i, {"Center", "Rotation", "X", "Y", "Z"});
			for (int32 i = 0; i < Geom.SphylElems.Num(); ++i)
				bChanged |= DrawElem(Setup, Geom.SphylElems[i], "Capsule", i, {"Center", "Rotation", "Radius", "Length"});
			// Convex는 꼭짓점 배열이라 숫자로 고치지 않는다. 개수만 보여 준다.
			for (int32 i = 0; i < Geom.ConvexElems.Num(); ++i)
				ImGui::BulletText("Convex %d (%d verts)", i, Geom.ConvexElems[i].VertexData.Num());
			if (Geom.GetElementCount() == 0)
				ImGui::TextDisabled("No primitives");
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	ImGui::PopID();

	// 형상 값은 구조체 멤버로 바로 고치므로 여기서 에셋을 무효화해 Dirty 표시와 실행 바디 재생성을 한다.
	if (bChanged)
		Asset.InvalidatePhysicsData();
}
