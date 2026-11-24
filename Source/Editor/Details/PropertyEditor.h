#pragma once

struct ImFont;
class UObject;
class FProperty;
struct FRotator;
class UScriptStruct;
struct FPropertyMeta;
struct FBodyInstance;
#include "Core/EngineString.h"
#include <functional>
#include <imgui.h>

namespace FPropertyEditor
{
	// USTRUCT 하나를 그리는 함수
	using FStructDrawer = bool (*)(UObject* Owner, void* StructPtr, const UScriptStruct& Struct, const FPropertyMeta& OuterMeta, ImFont* CustomFont);

	// 이 구조체를 그릴 때 기본 멤버 나열 대신 Drawer를 쓴다. 같은 구조체를 다시 등록하면 덮어쓴다.
	void RegisterStructDrawer(const UScriptStruct* Struct, FStructDrawer Drawer);
	// 에셋 슬롯(썸네일 + 드롭다운)이 에디터의 다른 부분에 부탁하는 일. 에디터 엔진이 시작할 때 등록한다.
	struct FAssetSlotCallbacks
	{
		std::function<void(const FString&)> Open;            // 에셋 에디터로 연다
		std::function<ImTextureID(const FString&)> Thumbnail; // 썸네일. 없으면 ImTextureID_Invalid
	};
	void SetAssetSlotCallbacks(FAssetSlotCallbacks Callbacks);
	// 구조체 멤버 하나를 기본 위젯(라벨 | 값)으로 그린다. 커스텀 Drawer 안에서 쓴다. 값이 바뀌면 true.
	bool DrawStructMember(UObject* Owner, void* StructPtr, const UScriptStruct& Struct, const char* MemberName,
	                      ImFont* CustomFont = nullptr, const FPropertyMeta* MetaOverride = nullptr);

	// Details 패널과 같은 2열(이름 | 값) 표. 안에서 DrawStructMember/DrawProperty를 부르면 한 줄씩 들어간다.
	// Begin이 true를 돌려준 경우에만 End를 부른다.
	bool BeginPropertyGrid(const char* TableId);
	void EndPropertyGrid();

	// DetailsPanel.cpp의 DrawProperty를 그대로 옮긴다 (값이 바뀌면 PostEditChangeProperty 호출).
	bool DrawProperty(UObject* Object, const FProperty& Property, ImFont* CustomFont = nullptr);
	// 기반 클래스부터 CPF_Edit 속성을 CollapsingHeader로 묶어 그린다 (DetailsPanel의 루프를 옮김).
	bool DrawObjectProperties(UObject* Object, ImFont* CustomFont = nullptr);

	// 레벨 디테일 패널의 트랜스폼 줄과 같은 모양: 왼쪽 라벨, 오른쪽 X(빨강)/Y(초록)/Z(파랑) 버튼 + 드래그 칸.
	// 축 버튼을 누르면 ResetValue로 되돌린다. 값이 바뀌면 true.
	bool DrawVector3(const char* Label, float* Values, float ResetValue);
	// FRotator를 Roll=X, Pitch=Y, Yaw=Z 순서로 보여 준다 (레벨 디테일 패널과 같은 축 순서).
	bool DrawRotator(const char* Label, FRotator& Rotator);

	// UE Collision 카테고리와 같은 모양: Collision Presets 드롭다운, (Custom일 때만 편집되는) Collision Enabled·Object Type,
	// 접힌 Collision Responses 표(채널 | Ignore | Overlap | Block). 프로퍼티 표(BeginPropertyGrid) 안에서 부른다. 바뀌면 true.
	bool DrawCollisionSettings(UObject* Owner, FBodyInstance& Body, bool bReadOnly, ImFont* CustomFont = nullptr);
}
// DetailsPanel.cpp는 이 헤더를 include하고 원래 함수 대신 FPropertyEditor::를 부르도록 바꾼다.