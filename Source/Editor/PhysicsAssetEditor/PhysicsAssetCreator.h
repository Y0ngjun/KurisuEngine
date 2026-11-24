#pragma once

#include "Core/EngineString.h"
#include "Physics/PhysicsAssetUtils.h"

#include <filesystem>
#include <functional>

// 콘텐츠 드로어 스켈레탈 메시 우클릭 > Create > Physics Asset > Create / Create and Assign.
// 생성 옵션 창(New Physics Asset)을 띄운 뒤 FPhysicsAssetUtils::CreateFromSkeletalMesh로 만든다.
//   Create            : 저장 위치를 골라 새 .physicsasset 파일만 만든다 (메시에는 연결하지 않는다).
//   Create and Assign : 메시의 물리 에셋을 새로 만들어 교체하고 메시 경로 기준 파일에 저장한다.
class FPhysicsAssetCreator
{
public:
	// 스켈레탈 메시 우클릭 메뉴 안에서 부른다.
	void DrawMenuItems(const FString& MeshPath, const std::filesystem::path& Directory);
	// 팝업 밖(매 프레임)에서 부른다. 생성 옵션 창을 그린다.
	void ProcessCreateRequest();
	// 생성이 끝나면 저장한 .physicsasset 경로(에셋 키)를 넘긴다. 물리 에셋 에디터 열기에 쓴다.
	void SetCreatedCallback(std::function<void(const FString&)> InCallback) { OnCreated = std::move(InCallback); }

private:
	void DrawOptionsDialog();
	bool CreateAsset();

	std::function<void(const FString&)> OnCreated;

	FString MeshPath;
	std::filesystem::path CreateDirectory;
	FPhysAssetCreateParams Params;
	bool bAssign = false;
	bool bOpenDialog = false;
};
