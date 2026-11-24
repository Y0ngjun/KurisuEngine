#pragma once

#include "Editor/AssetEditor/AssetEditorPanel.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"

#include <vector>

// UE Physics Asset Editor의 Skeleton Tree.
// 기본은 UE처럼 본은 숨기고 바디만 계층으로 보여 준다 (바디의 부모 = 가장 가까운 조상 본의 바디).
// 옵션으로 본 전체 보기, 관절 줄 보기를 켤 수 있다 (UE 기본값도 둘 다 꺼짐).
// 줄을 누르면 에디터의 선택이 바뀌고 뷰포트에서 강조된다.
class FPhysicsAssetSkeletonTreePanel : public TAssetEditorPanel<FPhysicsAssetEditor>
{
public:
	explicit FPhysicsAssetSkeletonTreePanel(FPhysicsAssetEditor& InEditor);

protected:
	void DrawContent() override;

private:
	// 매 프레임 에셋에서 다시 만든다 (바디·관절이 편집으로 바뀌어도 따라가게).
	void BuildLookup();
	bool IsBoneVisible(int32 BoneIndex) const;
	bool IsBodyVisible(int32 BoneIndex) const;
	void DrawBone(int32 BoneIndex);       // 본 보기: 본 계층 + 바디·관절 줄
	void DrawBodyNode(int32 BoneIndex);   // 바디 보기: 바디 계층
	void DrawBodyRow(int32 BodyIndex);
	void DrawConstraintRow(int32 ConstraintIndex);

	char SearchBuffer[128] = {};
	bool bShowBones = false;
	bool bShowConstraints = false;

	std::vector<int32> BodyOfBone;                     // 본 → 바디 번호 (-1 = 없음)
	std::vector<std::vector<int32>> ConstraintsOfBone; // 본(관절의 자식 쪽) → 관절 번호들
	std::vector<std::vector<int32>> ChildBodyBones;    // 바디 본 → 바로 아래 바디 본들
	std::vector<int32> RootBodyBones;                  // 위에 바디가 없는 바디 본들
	std::vector<int32> BodyParentBone;                 // 본 → 가장 가까운 조상 바디 본 (-1 = 없음)

	// 뷰포트에서 선택이 바뀌면 그 줄까지 트리를 펼치고 스크롤한다 (트리에서 누른 선택은 제외).
	void UpdateScrollTarget();
	void ScrollIfTarget(bool bIsTarget);
	bool ShouldForceOpen(int32 BoneIndex) const;
	int32 LastSelectedBody = -1;
	int32 LastSelectedConstraint = -1;
	bool bSelectionChangedByTree = false;
	bool bScrollToSelection = false;
	std::vector<uint8> OpenForSelection;               // 선택 줄의 조상이라 펼쳐야 하는 본
	std::vector<uint8> BoneSubtreeMatches;             // 본 계층 기준: 이 본이나 자손이 검색어와 맞음
	std::vector<uint8> BodySubtreeMatches;             // 바디 계층 기준: 이 바디나 아래 바디가 검색어와 맞음
};
