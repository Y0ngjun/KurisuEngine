#include "EnginePCH.h"
#include "StaticMeshDetailsPanel.h"
#include "Editor/StaticMeshEditor/StaticMeshEditor.h"
#include "Editor/Details/PropertyEditor.h"
#include "Physics/BodySetup.h"
#include "Render/Mesh.h"
#include "UObject/Property.h"
#include "imgui.h"

namespace
{
	// 구조체의 편집 가능한 멤버를 부모 구조체부터 Details 표 한 줄씩 그린다.
	// 배열 멤버는 표 편집기가 없으므로 건너뛴다.
	bool DrawElementMembers(UObject* Owner, void* Element, const UScriptStruct& Struct)
	{
		TArray<const UStruct*> Chain;
		for (const UStruct* Current = &Struct; Current; Current = Current->GetSuperStruct())
		{
			Chain.Add(Current);
		}
		bool bChanged = false;
		if (!FPropertyEditor::BeginPropertyGrid("##ElementGrid"))
		{
			return false;
		}
		for (auto It = Chain.rbegin(); It != Chain.rend(); ++It)
		{
			for (const FProperty* Member : (*It)->GetProperties())
			{
				if (!Member->HasAnyFlags(CPF_Edit) || CastField<FArrayProperty>(Member))
				{
					continue;
				}
				bChanged |= FPropertyEditor::DrawStructMember(Owner, Element, Struct, Member->Name.c_str());
			}
		}
		FPropertyEditor::EndPropertyGrid();
		return bChanged;
	}

	// 한 종류의 형상 배열을 접이식 목록으로 그린다. 각 원소의 오른쪽 Remove로 지운다.
	template <typename TElem>
	bool DrawElementList(UBodySetup& Setup, TArray<TElem>& Elements, const char* Title, const char* ItemName)
	{
		if (Elements.IsEmpty())
		{
			return false;
		}
		// ID에 접두사를 붙인다. 그냥 Title이면 다른 위젯과 ID가 겹칠 수 있다.
		const FString Header = FString(Title) + " (" + std::to_string(Elements.Num()) + ")###ShapeList_" + Title;
		if (!ImGui::CollapsingHeader(Header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			return false;
		}
		bool bChanged = false;
		int32 RemoveIndex = -1;
		ImGui::PushID(Title);
		for (int32 Index = 0; Index < Elements.Num(); ++Index)
		{
			TElem& Element = Elements[Index];
			ImGui::PushID(Index);
			FString Label = FString(ItemName) + " " + std::to_string(Index);
			const FString ElementName = Element.Name.ToString();
			if (!ElementName.empty() && ElementName != "None")
			{
				Label += " - " + ElementName;
			}
			const bool bOpen = ImGui::TreeNodeEx("##Element", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap |
			                                                      ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth,
			                                     "%s", Label.c_str());
			const float RemoveWidth = ImGui::CalcTextSize("Remove").x + ImGui::GetStyle().FramePadding.x * 2.0f;
			ImGui::SameLine(ImGui::GetContentRegionMax().x - RemoveWidth);
			if (ImGui::Button("Remove"))
			{
				RemoveIndex = Index;
			}
			if (bOpen)
			{
				bChanged |= DrawElementMembers(&Setup, &Element, *TElem::StaticStruct());
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		ImGui::PopID();
		if (RemoveIndex >= 0)
		{
			Elements.RemoveAt(RemoveIndex);
			bChanged = true;
		}
		return bChanged;
	}
}

FStaticMeshDetailsPanel::FStaticMeshDetailsPanel(FStaticMeshEditor& InEditor)
    : FStaticMeshEditorPanel(InEditor, "Details")
{
}

void FStaticMeshDetailsPanel::DrawContent()
{
	UStaticMesh* Mesh = Editor.GetMesh();
	UBodySetup* Setup = Mesh ? Mesh->GetBodySetup() : nullptr;
	if (!Setup)
	{
		return;
	}

	// 툴바: 저장 / 되돌리기 / Undo / Redo / 충돌 메뉴. 단축키는 이 패널에 포커스가 있을 때만 받는다.
	const bool bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	const bool bCtrl = ImGui::GetIO().KeyCtrl;
	if (ImGui::Button("Save") || (bFocused && bCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)))
	{
		Editor.CommitCollisionEdit();
		Editor.SaveCollision();
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(!Setup->IsDirty());
	if (ImGui::Button("Revert"))
	{
		Editor.RevertCollision();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!Editor.CanUndoCollision());
	if (ImGui::Button("Undo") || (bFocused && bCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)))
	{
		Editor.UndoCollision();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!Editor.CanRedoCollision());
	if (ImGui::Button("Redo") || (bFocused && bCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)))
	{
		Editor.RedoCollision();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Collision##Menu"))
	{
		ImGui::OpenPopup("##CollisionMenu");
	}

	bool bChanged = false;

	// UE 스태틱 메시 에디터의 Collision 메뉴. Add는 메시 경계에 맞춘 형상을 덧붙인다.
	if (ImGui::BeginPopup("##CollisionMenu"))
	{
		const FStaticMeshData& MeshData = Mesh->MeshData;
		if (ImGui::MenuItem("Add Box Simplified Collision"))
		{
			Setup->AggGeom.BoxElems.Add(UBodySetup::MakeFitBox(MeshData));
			bChanged = true;
		}
		if (ImGui::MenuItem("Add Sphere Simplified Collision"))
		{
			Setup->AggGeom.SphereElems.Add(UBodySetup::MakeFitSphere(MeshData));
			bChanged = true;
		}
		if (ImGui::MenuItem("Add Capsule Simplified Collision"))
		{
			Setup->AggGeom.SphylElems.Add(UBodySetup::MakeFitCapsule(MeshData));
			bChanged = true;
		}
		if (ImGui::MenuItem("Auto Convex Collision"))
		{
			// 기존 컨벡스는 새 헐로 바꾸고, 다른 형상은 그대로 둔다.
			Setup->AggGeom.ConvexElems.Empty();
			Setup->AggGeom.ConvexElems.Add(UBodySetup::MakeFitConvex(MeshData));
			bChanged = true;
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Remove Collision", nullptr, false, Setup->AggGeom.GetElementCount() > 0))
		{
			Setup->AggGeom.EmptyElements();
			bChanged = true;
		}
		ImGui::EndPopup();
	}

	if (!Editor.CollisionError.empty())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", Editor.CollisionError.c_str());
	}

	// 에셋 단위 설정 (UE의 Collision Presets / Simple Collision Physical Material).
	if (ImGui::CollapsingHeader("Collision", ImGuiTreeNodeFlags_DefaultOpen) && FPropertyEditor::BeginPropertyGrid("##CollisionSettings"))
	{
		// 구조체 멤버를 직접 그리므로 PostEditChangeProperty가 불리지 않는다. 바뀌면 직접 반영한다.
		// 메시를 쓰는 컴포넌트 중 Use Default Collision인 것이 따르는 에셋 기본 충돌 (UE Static Mesh Editor의 Collision Presets)
		if (FPropertyEditor::DrawCollisionSettings(Setup, Setup->DefaultInstance, false))
		{
			Setup->InvalidatePhysicsData();
			Editor.NotifyCollisionChanged();
		}
		// DrawProperty는 PostEditChangeProperty로 이 메시를 쓰는 바디를 다시 만든다.
		if (const FProperty* Property = Setup->GetClass()->FindPropertyByName("PhysMaterial"))
		{
			if (FPropertyEditor::DrawProperty(Setup, *Property))
			{
				Editor.NotifyCollisionChanged();
			}
		}
		FPropertyEditor::EndPropertyGrid();
	}

	bChanged |= DrawElementList(*Setup, Setup->AggGeom.BoxElems, "Boxes", "Box");
	bChanged |= DrawElementList(*Setup, Setup->AggGeom.SphereElems, "Spheres", "Sphere");
	bChanged |= DrawElementList(*Setup, Setup->AggGeom.SphylElems, "Capsules", "Capsule");
	bChanged |= DrawElementList(*Setup, Setup->AggGeom.ConvexElems, "Convex", "Convex");

	if (bChanged)
	{
		// 형상 배열을 직접 바꿨으므로 쿠킹 캐시와 이 메시를 쓰는 바디를 다시 만든다.
		Setup->InvalidatePhysicsData();
		Editor.NotifyCollisionChanged();
	}
}
