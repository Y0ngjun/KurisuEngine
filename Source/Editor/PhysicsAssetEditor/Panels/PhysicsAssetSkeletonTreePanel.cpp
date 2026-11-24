#include "EnginePCH.h"
#include "PhysicsAssetSkeletonTreePanel.h"

#include "Editor/EditorUI/EditorStyle.h"
#include "Physics/PhysicsAsset.h"
#include "Render/SkeletalMesh.h"
#include "imgui.h"

namespace
{
	const ImVec4 BodyIconColor(0.45f, 0.82f, 1.0f, 1.0f);       // 뷰포트 바디 색과 맞춘다
	const ImVec4 ConstraintTextColor(1.0f, 0.75f, 0.35f, 1.0f);

	const char* ShapeTypeName(const UBodySetup& Setup)
	{
		const FKAggregateGeom& Geom = Setup.AggGeom;
		const int32 Count = Geom.SphylElems.Num() + Geom.BoxElems.Num() + Geom.SphereElems.Num() + Geom.ConvexElems.Num();
		if (Count > 1)
			return "Multiple";
		if (Geom.SphylElems.Num() == 1)
			return "Capsule";
		if (Geom.BoxElems.Num() == 1)
			return "Box";
		if (Geom.SphereElems.Num() == 1)
			return "Sphere";
		if (Geom.ConvexElems.Num() == 1)
			return "Convex";
		return "Empty";
	}

	// 방금 그린 트리 줄의 라벨 앞자리에 바디 아이콘(작은 캡슐)을 그린다. 라벨은 앞에 공백을 두고 그린다.
	void DrawBodyIcon()
	{
		const ImVec2 Min = ImGui::GetItemRectMin();
		const ImVec2 Max = ImGui::GetItemRectMax();
		const float X = Min.x + ImGui::GetTreeNodeToLabelSpacing() + 1.0f;
		const float CenterY = (Min.y + Max.y) * 0.5f;
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(X, CenterY - 3.5f), ImVec2(X + 13.0f, CenterY + 3.5f),
		                                          ImGui::GetColorU32(BodyIconColor), 3.5f);
	}

	constexpr const char* IconPadding = "     ";

	constexpr ImGuiTreeNodeFlags RowFlags =
	    ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DrawLinesToNodes;
	constexpr ImGuiTreeNodeFlags LeafRowFlags =
	    RowFlags | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	constexpr ImGuiTreeNodeFlags ParentRowFlags =
	    RowFlags | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
}

FPhysicsAssetSkeletonTreePanel::FPhysicsAssetSkeletonTreePanel(FPhysicsAssetEditor& InEditor)
	: TAssetEditorPanel<FPhysicsAssetEditor>(InEditor, "Skeleton Tree", "PhysicsAssetPanel")
{
}

void FPhysicsAssetSkeletonTreePanel::BuildLookup()
{
	const TArray<FBoneInfo>& Bones = Editor.GetMesh()->GetRefSkeleton();
	const TArray<FEditorBoneItem>& Items = Editor.GetBoneItems();
	const UPhysicsAsset* Asset = Editor.GetPhysicsAsset();
	const int32 NumBones = Bones.Num();

	BodyOfBone.assign(NumBones, -1);
	ConstraintsOfBone.assign(NumBones, {});
	for (int32 Body = 0; Body < Asset->SkeletalBodySetups.Num(); ++Body)
		if (const int32 Bone = Editor.GetBodyBoneIndex(Body); Bone >= 0)
			BodyOfBone[Bone] = Body;
	for (int32 Constraint = 0; Constraint < Asset->ConstraintSetup.Num(); ++Constraint)
		if (const int32 Bone = Editor.GetConstraintBoneIndex(Constraint); Bone >= 0)
			ConstraintsOfBone[Bone].push_back(Constraint);

	// 바디 계층: 바디의 부모 = 가장 가까운 조상 본 중 바디가 있는 것. 부모 본이 항상 앞에 있으므로 앞에서부터 돈다.
	std::vector<int32> NearestBodyBone(NumBones, -1);   // 자기 자신 포함, 가장 가까운 바디 본
	BodyParentBone.assign(NumBones, -1);               // 자기 제외, 가장 가까운 조상 바디 본
	ChildBodyBones.assign(NumBones, {});
	RootBodyBones.clear();
	for (int32 Bone = 0; Bone < NumBones; ++Bone)
	{
		const int32 Parent = Bones[Bone].ParentIndex;
		BodyParentBone[Bone] = (Parent >= 0 && Parent < Bone) ? NearestBodyBone[Parent] : -1;
		NearestBodyBone[Bone] = BodyOfBone[Bone] >= 0 ? Bone : BodyParentBone[Bone];
		if (BodyOfBone[Bone] < 0)
			continue;
		if (BodyParentBone[Bone] >= 0)
			ChildBodyBones[BodyParentBone[Bone]].push_back(Bone);
		else
			RootBodyBones.push_back(Bone);
	}

	// 검색: 자식이 부모보다 뒤에 있으므로 뒤에서부터 돌며 부모로 올린다.
	BoneSubtreeMatches.assign(NumBones, 0);
	BodySubtreeMatches.assign(NumBones, 0);
	const bool bFiltering = SearchBuffer[0] != '\0';
	for (int32 Bone = NumBones - 1; Bone >= 0; --Bone)
	{
		const bool bNameMatches = !bFiltering || (Bone < Items.Num() && EditorStyle::ContainsIgnoreCase(Items[Bone].Name.c_str(), SearchBuffer));
		BoneSubtreeMatches[Bone] |= bNameMatches ? 1 : 0;
		const int32 Parent = Bones[Bone].ParentIndex;
		if (Parent >= 0 && Parent < Bone)
			BoneSubtreeMatches[Parent] |= BoneSubtreeMatches[Bone];

		if (BodyOfBone[Bone] >= 0)
		{
			BodySubtreeMatches[Bone] |= bNameMatches ? 1 : 0;
			if (BodyParentBone[Bone] >= 0)
				BodySubtreeMatches[BodyParentBone[Bone]] |= BodySubtreeMatches[Bone];
		}
	}
}

bool FPhysicsAssetSkeletonTreePanel::IsBoneVisible(int32 BoneIndex) const
{
	return BoneIndex >= 0 && BoneIndex < static_cast<int32>(BoneSubtreeMatches.size()) && BoneSubtreeMatches[BoneIndex];
}

bool FPhysicsAssetSkeletonTreePanel::IsBodyVisible(int32 BoneIndex) const
{
	return BoneIndex >= 0 && BoneIndex < static_cast<int32>(BodySubtreeMatches.size()) && BodySubtreeMatches[BoneIndex];
}

void FPhysicsAssetSkeletonTreePanel::DrawContent()
{
	if (!Editor.GetMesh() || !Editor.GetPhysicsAsset())
		return;

	EditorStyle::SearchBox("##PhysicsBoneSearch", SearchBuffer, sizeof(SearchBuffer));
	ImGui::Checkbox("Show Bones", &bShowBones);
	ImGui::SameLine();
	ImGui::Checkbox("Show Constraints", &bShowConstraints);
	ImGui::Separator();

	BuildLookup();
	UpdateScrollTarget();

	if (ImGui::BeginChild("##PhysicsSkeletonTree"))
	{
		if (bShowBones)
		{
			for (const int32 Root : Editor.GetRootBoneIndices())
				if (IsBoneVisible(Root))
					DrawBone(Root);
		}
		else
		{
			for (const int32 Root : RootBodyBones)
				if (IsBodyVisible(Root))
					DrawBodyNode(Root);
		}

		// 빈 곳을 누르면 선택을 푼다.
		if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
		{
			bSelectionChangedByTree = true;
			Editor.SelectBone(-1);
		}
	}
	ImGui::EndChild();
	// 검색에 가려 대상 줄이 안 그려졌어도 펼치기·스크롤은 이 프레임에서 끝낸다.
	bScrollToSelection = false;
}

void FPhysicsAssetSkeletonTreePanel::UpdateScrollTarget()
{
	const int32 Body = Editor.GetSelectedBodyIndex();
	const int32 Constraint = Editor.GetSelectedConstraintIndex();
	if (Body != LastSelectedBody || Constraint != LastSelectedConstraint)
	{
		// 트리에서 누른 선택은 이미 보이는 줄이므로 스크롤하지 않는다.
		bScrollToSelection = !bSelectionChangedByTree && (Body >= 0 || Constraint >= 0);
		LastSelectedBody = Body;
		LastSelectedConstraint = Constraint;
	}
	bSelectionChangedByTree = false;
	if (!bScrollToSelection)
		return;

	// 대상 줄이 들어 있는 본과 그 조상들을 펼친다.
	const TArray<FBoneInfo>& Bones = Editor.GetMesh()->GetRefSkeleton();
	OpenForSelection.assign(Bones.Num(), 0);
	const int32 Target = Body >= 0 ? Editor.GetBodyBoneIndex(Body) : Editor.GetConstraintBoneIndex(Constraint);
	for (int32 Bone = Target; Bone >= 0 && Bone < Bones.Num();)
	{
		OpenForSelection[Bone] = 1;
		const int32 Next = bShowBones ? Bones[Bone].ParentIndex : BodyParentBone[Bone];
		if (Next >= Bone)	// 부모는 항상 앞에 있다. 아니면 깨진 데이터이므로 멈춘다.
			break;
		Bone = Next;
	}
}

bool FPhysicsAssetSkeletonTreePanel::ShouldForceOpen(int32 BoneIndex) const
{
	return bScrollToSelection && BoneIndex >= 0 && BoneIndex < static_cast<int32>(OpenForSelection.size()) && OpenForSelection[BoneIndex];
}

void FPhysicsAssetSkeletonTreePanel::ScrollIfTarget(bool bIsTarget)
{
	if (!bScrollToSelection || !bIsTarget)
		return;
	ImGui::SetScrollHereY(0.5f);
	bScrollToSelection = false;
}

void FPhysicsAssetSkeletonTreePanel::DrawBodyNode(int32 BoneIndex)
{
	const TArray<FEditorBoneItem>& Items = Editor.GetBoneItems();
	const int32 Body = BodyOfBone[BoneIndex];
	if (BoneIndex >= Items.Num() || Body < 0)
		return;
	ImGui::PushID(BoneIndex);

	bool bHasVisibleChild = false;
	for (const int32 Child : ChildBodyBones[BoneIndex])
		bHasVisibleChild |= IsBodyVisible(Child);
	const bool bHasRows = bHasVisibleChild || (bShowConstraints && !ConstraintsOfBone[BoneIndex].empty());

	ImGuiTreeNodeFlags Flags = bHasRows ? ParentRowFlags : LeafRowFlags;
	if (Editor.GetSelectedBodyIndex() == Body)
		Flags |= ImGuiTreeNodeFlags_Selected;
	if ((SearchBuffer[0] != '\0' || ShouldForceOpen(BoneIndex)) && bHasRows)
		ImGui::SetNextItemOpen(true);

	// UE처럼 바디 줄은 본 이름으로 보인다.
	const bool bOpen = ImGui::TreeNodeEx("Body", Flags, "%s%s", IconPadding, Items[BoneIndex].Name.c_str());
	DrawBodyIcon();
	// 관절 줄을 숨겼으면 선택한 관절 대신 그 관절이 붙은 바디 줄로 스크롤한다.
	const int32 SelectedConstraint = Editor.GetSelectedConstraintIndex();
	ScrollIfTarget(Editor.GetSelectedBodyIndex() == Body ||
	               (!bShowConstraints && SelectedConstraint >= 0 && Editor.GetConstraintBoneIndex(SelectedConstraint) == BoneIndex));
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
	{
		bSelectionChangedByTree = true;
		Editor.SelectBody(Body);
	}
	if (const USkeletalBodySetup* Setup = Editor.GetPhysicsAsset()->SkeletalBodySetups[Body]; Setup && ImGui::IsItemHovered())
		ImGui::SetTooltip("%s", ShapeTypeName(*Setup));

	if (bOpen && bHasRows)
	{
		if (bShowConstraints)
			for (const int32 Constraint : ConstraintsOfBone[BoneIndex])
				DrawConstraintRow(Constraint);
		for (const int32 Child : ChildBodyBones[BoneIndex])
			if (IsBodyVisible(Child))
				DrawBodyNode(Child);
		ImGui::TreePop();
	}
	ImGui::PopID();
}

void FPhysicsAssetSkeletonTreePanel::DrawBone(int32 BoneIndex)
{
	const TArray<FEditorBoneItem>& Items = Editor.GetBoneItems();
	if (BoneIndex >= Items.Num())
		return;
	ImGui::PushID(BoneIndex);

	const int32 Body = BodyOfBone[BoneIndex];
	const std::vector<int32>& Constraints = ConstraintsOfBone[BoneIndex];
	bool bHasVisibleChild = false;
	for (const int32 Child : Items[BoneIndex].ChildBoneIndices)
		bHasVisibleChild |= IsBoneVisible(Child);
	const bool bHasRows = Body >= 0 || (bShowConstraints && !Constraints.empty()) || bHasVisibleChild;

	ImGuiTreeNodeFlags Flags = bHasRows ? ParentRowFlags : LeafRowFlags;
	const bool bBoneSelected = Editor.GetSelectedBoneIndex() == BoneIndex && Editor.GetSelectedBodyIndex() < 0 && Editor.GetSelectedConstraintIndex() < 0;
	if (bBoneSelected)
		Flags |= ImGuiTreeNodeFlags_Selected;
	if ((SearchBuffer[0] != '\0' || ShouldForceOpen(BoneIndex)) && bHasRows)
		ImGui::SetNextItemOpen(true);

	// 바디가 없는 본은 흐리게
	if (Body < 0)
		ImGui::PushStyleColor(ImGuiCol_Text, EditorColor::TextMuted);
	const bool bOpen = ImGui::TreeNodeEx("Bone", Flags, "%s", Items[BoneIndex].Name.c_str());
	if (Body < 0)
		ImGui::PopStyleColor();
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
	{
		bSelectionChangedByTree = true;
		Editor.SelectBone(BoneIndex);
	}

	if (bOpen && bHasRows)
	{
		if (Body >= 0)
			DrawBodyRow(Body);
		if (bShowConstraints)
			for (const int32 Constraint : Constraints)
				DrawConstraintRow(Constraint);
		for (const int32 Child : Items[BoneIndex].ChildBoneIndices)
			if (IsBoneVisible(Child))
				DrawBone(Child);
		ImGui::TreePop();
	}
	ImGui::PopID();
}

void FPhysicsAssetSkeletonTreePanel::DrawBodyRow(int32 BodyIndex)
{
	const USkeletalBodySetup* Setup = Editor.GetPhysicsAsset()->SkeletalBodySetups[BodyIndex];
	ImGuiTreeNodeFlags Flags = LeafRowFlags;
	if (Editor.GetSelectedBodyIndex() == BodyIndex)
		Flags |= ImGuiTreeNodeFlags_Selected;

	ImGui::TreeNodeEx("Body", Flags, "%sBody  (%s)", IconPadding, Setup ? ShapeTypeName(*Setup) : "?");
	DrawBodyIcon();
	const int32 SelectedConstraint = Editor.GetSelectedConstraintIndex();
	ScrollIfTarget(Editor.GetSelectedBodyIndex() == BodyIndex ||
	               (!bShowConstraints && SelectedConstraint >= 0 && Editor.GetConstraintBoneIndex(SelectedConstraint) == Editor.GetBodyBoneIndex(BodyIndex)));
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
	{
		bSelectionChangedByTree = true;
		Editor.SelectBody(BodyIndex);
	}
}

void FPhysicsAssetSkeletonTreePanel::DrawConstraintRow(int32 ConstraintIndex)
{
	const UPhysicsConstraintTemplate* Template = Editor.GetPhysicsAsset()->ConstraintSetup[ConstraintIndex];
	ImGuiTreeNodeFlags Flags = LeafRowFlags;
	if (Editor.GetSelectedConstraintIndex() == ConstraintIndex)
		Flags |= ImGuiTreeNodeFlags_Selected;

	ImGui::PushID(ConstraintIndex);
	ImGui::PushStyleColor(ImGuiCol_Text, ConstraintTextColor);
	// 관절은 이 본(자식)과 부모 바디 본을 잇는다.
	ImGui::TreeNodeEx("Constraint", Flags, "Constraint  -> %s",
	                  Template ? Template->DefaultInstance.ConstraintBone2.ToString().c_str() : "?");
	ImGui::PopStyleColor();
	ScrollIfTarget(Editor.GetSelectedConstraintIndex() == ConstraintIndex);
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
	{
		bSelectionChangedByTree = true;
		Editor.SelectConstraint(ConstraintIndex);
	}
	ImGui::PopID();
}
