#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>
#include <imgui_internal.h>

// 에디터 UI 색 팔레트와 테마. 패널에서 색을 직접 적지 말고 EditorColor의 이름을 쓴다.
// 구조: 테마마다 FEditorPalette 하나 -> 현재 팔레트 -> EditorColor 이름(현재 팔레트 값을 가리키는 참조) -> 패널.
//  - EditorColor::BgPanel 같은 이름은 항상 현재 테마 값을 읽는다. 테마를 바꾸면 다음에 그릴 때 바로 바뀐다.
//  - 그래서 패널에서 색을 constexpr이나 static 변수, 멤버 기본값으로 복사해 두면 안 된다. 그리는 시점에 읽는다.
//    오래 들고 있어야 하면 &EditorColor::Icon 처럼 주소를 저장한다.
//  - 새 색이 필요하면 FEditorPalette에 칸을 만들고 세 테마 모두에 값을 채운다.
// 테마와 상관없는 색: 콘텐츠 자체의 색(머티리얼, 사용자 선택 색, 3D 기즈모), XYZ 축색, 창 닫기 버튼 색.
// 크기는 96 DPI 기준으로 정의하고 패널에서 EditorStyle::Scale로 변환한다.

enum class EEditorTheme : int
{
	Dark,   // 언리얼 5 Dark 계열: 무채색 회색 + 파란 강조색
	Light,  // Unity Light 계열: 밝은 회색 + 어두운 글자
	Kurisu, // 마스코트 마키세 크리스 투톤: 적갈색 틀 + 아이보리 본문 + 바이올렛 선택
	Count
};

struct FEditorPalette
{
	// 배경. 아래로 갈수록 위에 얹히는 면이다.
	ImU32 BgDeepest;     // 창 바닥, 로그·목록 안쪽
	ImU32 BgPanel;       // 패널, 사이드바
	ImU32 BgRaised;      // 패널 위에 얹는 띠, 교차 행, 분할선
	ImU32 BgInput;       // 입력칸·체크박스 바탕. 어두운 테마는 패널보다 어둡게, 밝은 테마는 더 밝게
	ImU32 BgElevated;    // 카드, 버튼, 타임라인 행
	ImU32 BgElevatedAlt; // BgElevated의 교차 행
	ImU32 BgHeader;      // 눈금자, 빈 뷰포트
	ImU32 BgAccentHover; // 마우스를 올렸을 때 강조색이 살짝 도는 배경
	ImU32 BgOverlay;     // 뷰포트 위에 뜨는 툴바
	ImU32 Scrim;         // 뷰포트 위 통계 글자 뒤, 모달 뒤 어둠

	// 테두리
	ImU32 Border;
	ImU32 BorderStrong;

	// 글자와 아이콘
	ImU32 TextPrimary;
	ImU32 TextSecondary;
	ImU32 TextMuted;
	ImU32 TextDisabled;
	ImU32 TextOnAccent; // Accent로 채운 면 위의 글자

	// 강조색
	ImU32 Accent;
	ImU32 AccentHover;
	ImU32 AccentActive;
	ImU32 AccentBright; // 선택 테두리, 재생 헤드처럼 가는 선
	ImU32 AccentDim;    // 구분선, 약한 테두리, 눌린 입력칸
	ImU32 AccentText;   // 배경 위 강조 글자, 링크

	// 상태
	ImU32 Error;
	ImU32 Warning;
	ImU32 Success;
	ImU32 SuccessDim;

	// 종류를 구분하는 색. 배경 위에서 잘 보이도록 테마마다 밝기를 맞춘다.
	ImU32 CategoryBlue;
	ImU32 CategoryPurple;
	ImU32 CategoryGreen;
	ImU32 CategoryOrange;
	ImU32 CategoryRed;
	ImU32 CategoryTeal;
	ImU32 CategoryYellow;

	// 호버 하이라이트, 눈금선처럼 배경 위에 반투명하게 덮는 색의 바탕. 어두운 테마는 흰색, 밝은 테마는 검은색.
	ImU32 OverlayBase;

	// 3D 뷰포트 위 어두운 막(SceneScrim) 위의 강조 글자. 뷰포트 장면은 테마와 상관없으므로 막 위에서 읽히는 밝은 색을 쓴다.
	ImU32 SceneOverlayTitle;

	// 프레임: 제목 표시줄, 메뉴 바, 도킹 탭 줄 바탕과 그 위 글자. 0이면 BgDeepest / TextPrimary / TextSecondary를 쓴다.
	// 본문과 다른 색의 틀을 두르고 싶은 테마(Kurisu 투톤)만 채운다.
	ImU32 BgFrame;
	ImU32 TextOnFrame;
	ImU32 TextOnFrameMuted;

	// 목록·트리 선택 바탕의 기준색. 0이면 Accent를 쓴다. 강조색과 다른 색으로 선택을 칠하고 싶은 테마만 채운다.
	ImU32 SelectionBase;
	// 선택된 Selectable·TreeNode 행의 글자색. 0이면 TextPrimary 그대로 쓴다. 패널이 글자색을 직접 지정한 행은 그 색을 따른다.
	ImU32 SelectionText;

	// 파생 역할. 테마 정의에서 채우지 않고 FinalizePalette가 위 색으로 계산한다.
	ImU32 Selection;
	ImU32 SelectionHover;
	ImU32 Icon;
	ImU32 IconMuted;
};

namespace EditorColor
{
	// 테마와 상관없이 항상 같은 색.
	inline constexpr ImU32 Transparent = IM_COL32(0, 0, 0, 0);
	inline constexpr ImU32 Ink = IM_COL32(20, 20, 22, 255); // 색칠된 밝은 면 위의 어두운 선·글자

	// 3D 뷰포트 위에 겹쳐 그리는 통계 글자. 장면은 테마를 따르지 않으므로 항상 어두운 막 + 밝은 글자.
	inline constexpr ImU32 SceneScrim = IM_COL32(0, 0, 0, 140);
	inline constexpr ImU32 SceneOverlayText = IM_COL32(236, 236, 236, 255);

	// 프로퍼티 축 버튼과 곡선 편집기에서 같은 XYZ 색상을 쓴다.
	inline constexpr ImU32 AxisX = IM_COL32(230, 80, 80, 255);
	inline constexpr ImU32 AxisY = IM_COL32(80, 200, 80, 255);
	inline constexpr ImU32 AxisZ = IM_COL32(90, 140, 240, 255);

	// 제목 표시줄 닫기 버튼. Windows 기본 색을 따른다.
	inline constexpr ImU32 CloseHover = IM_COL32(232, 17, 35, 255);
	inline constexpr ImU32 CloseHeld = IM_COL32(241, 112, 122, 255);

	// 반투명 흰색. 이미지 틴트(255면 원본 그대로)와 색칠된 면 위의 흰 글자에 쓴다.
	constexpr ImU32 White(int Alpha)
	{
		return IM_COL32(255, 255, 255, Alpha);
	}
	// 팔레트 색의 불투명도만 바꾼다.
	constexpr ImU32 WithAlpha(ImU32 Color, int Alpha)
	{
		return (Color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(Alpha) << IM_COL32_A_SHIFT);
	}
}

namespace EditorTheme
{
	constexpr FEditorPalette FinalizePalette(FEditorPalette P)
	{
		// 목록 선택은 SelectionBase가 있으면 그 색, 없으면 강조색으로 칠한다.
		const ImU32 SelectionBase = P.SelectionBase ? P.SelectionBase : P.Accent;
		if (!P.BgFrame)
			P.BgFrame = P.BgDeepest;
		if (!P.TextOnFrame)
			P.TextOnFrame = P.TextPrimary;
		if (!P.TextOnFrameMuted)
			P.TextOnFrameMuted = P.TextSecondary;
		P.Selection = EditorColor::WithAlpha(SelectionBase, 100);
		P.SelectionHover = EditorColor::WithAlpha(SelectionBase, 150);
		P.Icon = P.TextSecondary;
		P.IconMuted = EditorColor::WithAlpha(P.TextMuted, 220);
		return P;
	}

	// Dark: 언리얼 5 기본 Dark 테마 계열. 무채색 회색 면, 입력칸은 패널보다 어둡게 파고들고 테두리는 어두운 선이다.
	// 강조색은 언리얼 Primary 파랑(#0070E0)을 그대로 쓴다.
	constexpr FEditorPalette MakeDarkPalette()
	{
		FEditorPalette P{};
		P.BgDeepest = IM_COL32(21, 21, 21, 255); // UE Background #151515
		P.BgPanel = IM_COL32(36, 36, 36, 255);   // UE Panel #242424
		P.BgRaised = IM_COL32(44, 44, 44, 255);
		P.BgInput = IM_COL32(18, 18, 18, 255);    // UE Input #0F0F0F보다 한 단계 밝게
		P.BgElevated = IM_COL32(56, 56, 56, 255); // UE Dropdown #383838
		P.BgElevatedAlt = IM_COL32(62, 62, 62, 255);
		P.BgHeader = IM_COL32(47, 47, 47, 255);      // UE Header #2F2F2F
		P.BgAccentHover = IM_COL32(72, 72, 72, 255); // 호버는 무채색으로 밝게 (UE Hover #575757 쪽)
		P.BgOverlay = IM_COL32(28, 28, 28, 238);
		P.Scrim = IM_COL32(0, 0, 0, 140);
		P.Border = IM_COL32(26, 26, 26, 255);         // 면보다 어두운 선 (Unity #232323)
		P.BorderStrong = IM_COL32(87, 87, 87, 255);   // 스크롤바 손잡이 (Unity #5F5F5F)
		P.TextPrimary = IM_COL32(210, 210, 210, 255); // Unity #D2D2D2, UE #C0C0C0 사이
		P.TextSecondary = IM_COL32(189, 189, 189, 255);
		P.TextMuted = IM_COL32(143, 143, 143, 255);
		P.TextDisabled = IM_COL32(102, 102, 102, 255);
		P.TextOnAccent = IM_COL32(255, 255, 255, 255);
		P.Accent = IM_COL32(0, 112, 224, 255);        // UE Primary #0070E0
		P.AccentHover = IM_COL32(14, 134, 255, 255);  // UE PrimaryHover #0E86FF
		P.AccentActive = IM_COL32(0, 80, 160, 255);   // UE PrimaryPress #0050A0
		P.AccentBright = IM_COL32(38, 187, 255, 255); // UE AccentBlue #26BBFF
		P.AccentDim = IM_COL32(18, 51, 90, 255);
		P.AccentText = IM_COL32(90, 160, 255, 255);
		P.Error = IM_COL32(239, 75, 75, 255);
		P.Warning = IM_COL32(255, 184, 0, 255); // UE Warning #FFB800
		P.Success = IM_COL32(80, 200, 110, 255);
		P.SuccessDim = IM_COL32(30, 84, 46, 255);
		P.CategoryBlue = IM_COL32(90, 190, 230, 255);
		P.CategoryPurple = IM_COL32(170, 120, 220, 255);
		P.CategoryGreen = IM_COL32(108, 176, 86, 255);
		P.CategoryOrange = IM_COL32(225, 165, 52, 255);
		P.CategoryRed = IM_COL32(204, 62, 62, 255);
		P.CategoryTeal = IM_COL32(76, 185, 128, 255);
		P.CategoryYellow = IM_COL32(230, 200, 80, 255);
		P.OverlayBase = IM_COL32(255, 255, 255, 255);
		P.SceneOverlayTitle = IM_COL32(110, 175, 245, 255);
		return FinalizePalette(P);
	}

	// Light: Unity Light 테마 계열. 회색 창 위에 더 밝은 버튼과 흰 입력칸을 올리고, 테두리는 중간 회색 선이다.
	constexpr FEditorPalette MakeLightPalette()
	{
		FEditorPalette P{};
		P.BgDeepest = IM_COL32(184, 184, 184, 255);
		P.BgPanel = IM_COL32(200, 200, 200, 255);    // Unity window #C8C8C8
		P.BgRaised = IM_COL32(190, 190, 190, 255);   // 교차 행·띠는 패널보다 살짝 어둡게
		P.BgInput = IM_COL32(240, 240, 240, 255);    // Unity input #F0F0F0
		P.BgElevated = IM_COL32(228, 228, 228, 255); // Unity button #E4E4E4
		P.BgElevatedAlt = IM_COL32(218, 218, 218, 255);
		P.BgHeader = IM_COL32(212, 212, 212, 255);
		P.BgAccentHover = IM_COL32(236, 236, 236, 255); // Unity button hover #ECECEC
		P.BgOverlay = IM_COL32(214, 214, 214, 238);
		P.Scrim = IM_COL32(0, 0, 0, 90);
		P.Border = IM_COL32(153, 153, 153, 255); // Unity border #999999
		P.BorderStrong = IM_COL32(138, 138, 138, 255);
		P.TextPrimary = IM_COL32(12, 12, 12, 255); // Unity text #090909
		P.TextSecondary = IM_COL32(44, 44, 44, 255);
		P.TextMuted = IM_COL32(78, 78, 78, 255);
		P.TextDisabled = IM_COL32(128, 128, 128, 255);
		P.TextOnAccent = IM_COL32(255, 255, 255, 255);
		P.Accent = IM_COL32(58, 114, 176, 255); // Unity selection #3A72B0
		P.AccentHover = IM_COL32(74, 133, 198, 255);
		P.AccentActive = IM_COL32(44, 93, 135, 255);
		P.AccentBright = IM_COL32(29, 95, 196, 255);
		P.AccentDim = IM_COL32(150, 195, 251, 255); // Unity pressed #96C3FB
		P.AccentText = IM_COL32(29, 80, 135, 255);  // Unity focus #1D5087
		P.Error = IM_COL32(140, 12, 12, 255);
		P.Warning = IM_COL32(96, 66, 0, 255);
		P.Success = IM_COL32(10, 80, 30, 255);
		P.SuccessDim = IM_COL32(176, 214, 184, 255);
		P.CategoryBlue = IM_COL32(20, 120, 180, 255);
		P.CategoryPurple = IM_COL32(118, 70, 180, 255);
		P.CategoryGreen = IM_COL32(60, 128, 40, 255);
		P.CategoryOrange = IM_COL32(190, 110, 10, 255);
		P.CategoryRed = IM_COL32(180, 40, 40, 255);
		P.CategoryTeal = IM_COL32(20, 136, 100, 255);
		P.CategoryYellow = IM_COL32(150, 122, 10, 255);
		P.OverlayBase = IM_COL32(0, 0, 0, 255);
		P.SceneOverlayTitle = IM_COL32(110, 175, 245, 255);
		return FinalizePalette(P);
	}

	// Kurisu: 리치 버건디 프레임과 실험실 가운 아이보리 본문.
	// 선명한 로고와 이어지는 적갈색 강조, 부드러운 바이올렛 선택색을 쓴다.
	constexpr FEditorPalette MakeKurisuPalette()
	{
		FEditorPalette P{};
		P.BgFrame = IM_COL32(160, 71, 62, 255); // 리치 버건디 #A0473E
		P.TextOnFrame = IM_COL32(255, 247, 243, 255);
		P.TextOnFrameMuted = IM_COL32(244, 228, 223, 255);
		P.BgDeepest = IM_COL32(236, 220, 207, 255);
		P.BgPanel = IM_COL32(247, 238, 229, 255); // 시안의 따뜻한 아이보리 #F7EEE5
		P.BgRaised = IM_COL32(236, 229, 221, 255);
		P.BgInput = IM_COL32(255, 255, 255, 255);
		P.BgElevated = IM_COL32(251, 248, 244, 255);
		P.BgElevatedAlt = IM_COL32(243, 237, 230, 255);
		P.BgHeader = IM_COL32(233, 224, 214, 255);
		P.BgAccentHover = IM_COL32(246, 227, 221, 255);
		P.BgOverlay = IM_COL32(247, 238, 229, 238);
		P.Scrim = IM_COL32(40, 10, 8, 90);
		P.Border = IM_COL32(205, 191, 179, 255);
		P.BorderStrong = IM_COL32(168, 150, 138, 255);
		P.TextPrimary = IM_COL32(42, 26, 26, 255);
		P.TextSecondary = IM_COL32(78, 58, 56, 255);
		P.TextMuted = IM_COL32(118, 96, 92, 255);
		P.TextDisabled = IM_COL32(168, 150, 142, 255);
		P.TextOnAccent = IM_COL32(255, 255, 255, 255);
		P.Accent = IM_COL32(183, 84, 70, 255); // 시안의 버건디 버튼 #B75446
		P.AccentHover = IM_COL32(195, 98, 85, 255);
		P.AccentActive = IM_COL32(160, 71, 62, 255);
		P.AccentBright = IM_COL32(183, 84, 70, 255);
		P.AccentDim = IM_COL32(239, 201, 193, 255);
		P.AccentText = IM_COL32(150, 61, 51, 255);
		P.Error = IM_COL32(150, 20, 30, 255);
		P.Warning = IM_COL32(110, 74, 0, 255);
		P.Success = IM_COL32(20, 96, 44, 255);
		P.SuccessDim = IM_COL32(196, 226, 200, 255);
		P.CategoryBlue = IM_COL32(20, 120, 180, 255);
		P.CategoryPurple = IM_COL32(118, 70, 180, 255);
		P.CategoryGreen = IM_COL32(60, 128, 40, 255);
		P.CategoryOrange = IM_COL32(190, 110, 10, 255);
		P.CategoryRed = IM_COL32(180, 40, 40, 255);
		P.CategoryTeal = IM_COL32(20, 136, 100, 255);
		P.CategoryYellow = IM_COL32(150, 122, 10, 255);
		P.OverlayBase = IM_COL32(60, 20, 16, 255);
		P.SceneOverlayTitle = IM_COL32(246, 150, 118, 255);
		// 버건디와 함께 채도를 한 단계 더 높인 리치 시안의 바이올렛.
		P.SelectionBase = IM_COL32(176, 143, 229, 255); // #B08FE5
		P.SelectionText = IM_COL32(101, 59, 153, 255);  // 짙은 바이올렛 #653B99
		return FinalizePalette(P);
	}

	// 프로젝트 브라우저 전용 팔레트. 에디터 테마와 상관없이 딥 블랙 바탕 + 적갈색 강조색으로 고정한다.
	// 브라우저는 프로젝트를 열기 전에 떠서 editor.ini 테마를 알 수 없고, 마스코트 일러스트와 어울리는 이 색을 쓴다.
	constexpr FEditorPalette MakeProjectBrowserPalette()
	{
		FEditorPalette P{};
		P.BgDeepest = IM_COL32(13, 13, 16, 255);
		P.BgPanel = IM_COL32(18, 18, 21, 255);
		P.BgRaised = IM_COL32(24, 24, 28, 255);
		P.BgInput = IM_COL32(24, 24, 28, 255);
		P.BgElevated = IM_COL32(30, 30, 34, 255);
		P.BgElevatedAlt = IM_COL32(36, 36, 41, 255);
		P.BgHeader = IM_COL32(42, 42, 47, 255);
		P.BgAccentHover = IM_COL32(50, 40, 41, 255);
		P.BgOverlay = IM_COL32(16, 16, 19, 238);
		P.Scrim = IM_COL32(0, 0, 0, 140);
		P.Border = IM_COL32(46, 44, 48, 255);
		P.BorderStrong = IM_COL32(96, 92, 94, 255);
		P.TextPrimary = IM_COL32(246, 244, 244, 255);
		P.TextSecondary = IM_COL32(205, 203, 204, 255);
		P.TextMuted = IM_COL32(165, 162, 165, 255);
		P.TextDisabled = IM_COL32(120, 120, 124, 255);
		P.TextOnAccent = IM_COL32(246, 244, 244, 255);
		P.Accent = IM_COL32(158, 51, 43, 255);
		P.AccentHover = IM_COL32(191, 69, 59, 255);
		P.AccentActive = IM_COL32(115, 33, 28, 255);
		P.AccentBright = IM_COL32(215, 86, 74, 255);
		P.AccentDim = IM_COL32(96, 36, 32, 255);
		P.AccentText = IM_COL32(235, 128, 86, 255);
		P.Error = IM_COL32(242, 97, 87, 255);
		P.Warning = IM_COL32(236, 186, 74, 255);
		P.Success = IM_COL32(112, 196, 124, 255);
		P.SuccessDim = IM_COL32(30, 84, 46, 255);
		P.CategoryBlue = IM_COL32(90, 190, 230, 255);
		P.CategoryPurple = IM_COL32(170, 120, 220, 255);
		P.CategoryGreen = IM_COL32(108, 176, 86, 255);
		P.CategoryOrange = IM_COL32(225, 165, 52, 255);
		P.CategoryRed = IM_COL32(204, 62, 62, 255);
		P.CategoryTeal = IM_COL32(76, 185, 128, 255);
		P.CategoryYellow = IM_COL32(230, 200, 80, 255);
		P.OverlayBase = IM_COL32(255, 255, 255, 255);
		P.SceneOverlayTitle = IM_COL32(235, 128, 86, 255);
		return FinalizePalette(P);
	}

	constexpr FEditorPalette MakePalette(EEditorTheme Theme)
	{
		switch (Theme)
		{
		case EEditorTheme::Dark:
			return MakeDarkPalette();
		case EEditorTheme::Light:
			return MakeLightPalette();
		default:
			return MakeKurisuPalette();
		}
	}

	inline constexpr EEditorTheme DefaultTheme = EEditorTheme::Kurisu;

	// 현재 테마 상태. 직접 쓰지 말고 EditorStyle::SetTheme을 거친다.
	namespace Detail
	{
		inline constinit FEditorPalette Active = MakePalette(DefaultTheme);
		inline EEditorTheme ActiveTheme = DefaultTheme;
		inline unsigned int Revision = 0; // 테마가 바뀔 때마다 증가. 스타일 사본을 가진 쪽이 비교해 다시 칠한다.
	}
}

namespace EditorColor
{
	// 현재 테마 팔레트를 가리키는 이름. 읽기 전용이며 테마를 바꾸면 값이 따라 바뀐다.
	inline const ImU32& BgFrame = EditorTheme::Detail::Active.BgFrame;
	inline const ImU32& TextOnFrame = EditorTheme::Detail::Active.TextOnFrame;
	inline const ImU32& TextOnFrameMuted = EditorTheme::Detail::Active.TextOnFrameMuted;
	inline const ImU32& BgDeepest = EditorTheme::Detail::Active.BgDeepest;
	inline const ImU32& BgPanel = EditorTheme::Detail::Active.BgPanel;
	inline const ImU32& BgRaised = EditorTheme::Detail::Active.BgRaised;
	inline const ImU32& BgInput = EditorTheme::Detail::Active.BgInput;
	inline const ImU32& BgElevated = EditorTheme::Detail::Active.BgElevated;
	inline const ImU32& BgElevatedAlt = EditorTheme::Detail::Active.BgElevatedAlt;
	inline const ImU32& BgHeader = EditorTheme::Detail::Active.BgHeader;
	inline const ImU32& BgAccentHover = EditorTheme::Detail::Active.BgAccentHover;
	inline const ImU32& BgOverlay = EditorTheme::Detail::Active.BgOverlay;
	inline const ImU32& Scrim = EditorTheme::Detail::Active.Scrim;

	inline const ImU32& Border = EditorTheme::Detail::Active.Border;
	inline const ImU32& BorderStrong = EditorTheme::Detail::Active.BorderStrong;

	inline const ImU32& TextPrimary = EditorTheme::Detail::Active.TextPrimary;
	inline const ImU32& TextSecondary = EditorTheme::Detail::Active.TextSecondary;
	inline const ImU32& TextMuted = EditorTheme::Detail::Active.TextMuted;
	inline const ImU32& TextDisabled = EditorTheme::Detail::Active.TextDisabled;
	inline const ImU32& TextOnAccent = EditorTheme::Detail::Active.TextOnAccent;

	inline const ImU32& Accent = EditorTheme::Detail::Active.Accent;
	inline const ImU32& AccentHover = EditorTheme::Detail::Active.AccentHover;
	inline const ImU32& AccentActive = EditorTheme::Detail::Active.AccentActive;
	inline const ImU32& AccentBright = EditorTheme::Detail::Active.AccentBright;
	inline const ImU32& AccentDim = EditorTheme::Detail::Active.AccentDim;
	inline const ImU32& AccentText = EditorTheme::Detail::Active.AccentText;

	inline const ImU32& Error = EditorTheme::Detail::Active.Error;
	inline const ImU32& Warning = EditorTheme::Detail::Active.Warning;
	inline const ImU32& Success = EditorTheme::Detail::Active.Success;
	inline const ImU32& SuccessDim = EditorTheme::Detail::Active.SuccessDim;

	inline const ImU32& CategoryBlue = EditorTheme::Detail::Active.CategoryBlue;
	inline const ImU32& CategoryPurple = EditorTheme::Detail::Active.CategoryPurple;
	inline const ImU32& CategoryGreen = EditorTheme::Detail::Active.CategoryGreen;
	inline const ImU32& CategoryOrange = EditorTheme::Detail::Active.CategoryOrange;
	inline const ImU32& CategoryRed = EditorTheme::Detail::Active.CategoryRed;
	inline const ImU32& CategoryTeal = EditorTheme::Detail::Active.CategoryTeal;
	inline const ImU32& CategoryYellow = EditorTheme::Detail::Active.CategoryYellow;

	inline const ImU32& SceneOverlayTitle = EditorTheme::Detail::Active.SceneOverlayTitle;

	// UI 역할 이름. 선택은 강조색, 종류는 분류색을 사용한다.
	inline const ImU32& Selection = EditorTheme::Detail::Active.Selection;
	inline const ImU32& SelectionHover = EditorTheme::Detail::Active.SelectionHover;
	inline const ImU32& SelectionText = EditorTheme::Detail::Active.SelectionText;
	inline const ImU32& Icon = EditorTheme::Detail::Active.Icon;
	inline const ImU32& IconMuted = EditorTheme::Detail::Active.IconMuted;
	inline const ImU32& AssetFolder = BgHeader;
	inline const ImU32& AssetAnimation = CategoryGreen;
	inline const ImU32& AssetAnimBlueprint = CategoryBlue;
	inline const ImU32& AssetBlendSpace = CategoryGreen;
	inline const ImU32& AssetParticle = CategoryBlue;
	inline const ImU32& AssetStaticMesh = CategoryBlue;
	inline const ImU32& AssetSkeletalMesh = CategoryPurple;
	inline const ImU32& AssetTexture = CategoryRed;
	inline const ImU32& AssetScene = CategoryOrange;
	inline const ImU32& AssetWidget = CategoryBlue;
	inline const ImU32& AssetScript = CategoryPurple;
	inline const ImU32& AssetMaterial = CategoryTeal;
	inline const ImU32& AssetFont = CategoryTeal;
	inline const ImU32& AssetFbx = CategoryBlue;
	inline const ImU32& AssetFile = TextMuted;
	inline const ImU32& ModuleRequired = CategoryYellow;
	inline const ImU32& ModuleSpawn = CategoryRed;
	inline const ImU32& ModuleTypeData = CategoryBlue;
	inline const ImU32& ModuleBeam = CategoryPurple;
	inline const ImU32& ModuleTrail = CategoryTeal;
	inline const ImU32& ModuleDefault = TextDisabled;

	// 호버 하이라이트·눈금선처럼 배경 위에 덮는 반투명 색. 어두운 테마는 밝게, 밝은 테마는 어둡게 덮는다.
	inline ImU32 Overlay(int Alpha)
	{
		return WithAlpha(EditorTheme::Detail::Active.OverlayBase, Alpha);
	}

	// ImVec4를 받는 API(TextColored 등)용 변환.
	inline ImVec4 ToVec4(ImU32 Color)
	{
		return ImGui::ColorConvertU32ToFloat4(Color);
	}
}

namespace EditorStyle
{
	inline float Scale(float Value)
	{
		return Value * ImGui::GetStyle().FontScaleDpi;
	}

	// 축 버튼 등 고유 분류색을 가진 버튼도 동일한 hover/active 규칙을 쓴다.
	// 호출자는 버튼을 그린 뒤 PopStyleColor(3)으로 복원한다.
	inline void PushTintedButton(ImU32 Color)
	{
		const ImVec4 Base = EditorColor::ToVec4(Color);
		ImGui::PushStyleColor(ImGuiCol_Button, Base);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImLerp(Base, EditorColor::ToVec4(EditorColor::White(255)), 0.15f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImLerp(Base, EditorColor::ToVec4(EditorColor::Ink), 0.20f));
	}

	struct FPopupAnchor
	{
		ImVec2 Min;
		ImVec2 Max;
	};

	inline FPopupAnchor LastItemAnchor()
	{
		return {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
	}

	// 버튼 아래에 붙이고, 아래 공간이 부족하면 위로 연다. 긴 목록은 스크롤한다.
	inline bool BeginButtonPopup(const char* Id, const FPopupAnchor& Anchor, float Width = 0.0f)
	{
		if (!ImGui::IsPopupOpen(Id))
			return false;

		const ImGuiViewport* Viewport = ImGui::GetWindowViewport();
		ImVec2 WorkMin = Viewport->WorkPos;
		ImVec2 WorkMax(WorkMin.x + Viewport->WorkSize.x, WorkMin.y + Viewport->WorkSize.y);
		const ImVec2 Center((Anchor.Min.x + Anchor.Max.x) * 0.5f, (Anchor.Min.y + Anchor.Max.y) * 0.5f);
		for (const ImGuiPlatformMonitor& Monitor : ImGui::GetPlatformIO().Monitors)
		{
			if (Center.x >= Monitor.MainPos.x && Center.y >= Monitor.MainPos.y &&
			    Center.x < Monitor.MainPos.x + Monitor.MainSize.x && Center.y < Monitor.MainPos.y + Monitor.MainSize.y)
			{
				WorkMin = Monitor.WorkPos;
				WorkMax = ImVec2(WorkMin.x + Monitor.WorkSize.x, WorkMin.y + Monitor.WorkSize.y);
				break;
			}
		}

		const float Gap = Scale(4.0f);
		const float Below = ImMax(1.0f, WorkMax.y - Anchor.Max.y - Gap);
		const float Above = ImMax(1.0f, Anchor.Min.y - WorkMin.y - Gap);
		const bool bAbove = Below < ImMin(Scale(320.0f), Above);
		const float MaxWidth = ImMax(1.0f, WorkMax.x - WorkMin.x);
		const float MinWidth = ImMin(MaxWidth, ImMax(Anchor.Max.x - Anchor.Min.x, Width));
		const float MaxHeight = ImMin(Scale(480.0f), bAbove ? Above : Below);
		const ImGuiContext& Context = *ImGui::GetCurrentContext();
		const ImGuiWindow* PopupWindow = Context.OpenPopupStack[Context.BeginPopupStack.Size].Window;
		const float PopupWidth = ImClamp(PopupWindow ? PopupWindow->SizeFull.x : MinWidth, MinWidth, MaxWidth);
		ImGui::SetNextWindowSizeConstraints(ImVec2(MinWidth, 0.0f), ImVec2(MaxWidth, MaxHeight));
		ImGui::SetNextWindowPos(ImVec2(ImClamp(Anchor.Min.x, WorkMin.x, WorkMax.x - PopupWidth),
		                               bAbove ? Anchor.Min.y - Gap : Anchor.Max.y + Gap),
		                        ImGuiCond_Always, ImVec2(0.0f, bAbove ? 1.0f : 0.0f));
		return ImGui::BeginPopup(Id, ImGuiWindowFlags_NoMove);
	}

	inline void PushSelectedButton(bool bSelected)
	{
		if (!bSelected)
			return;
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_CheckboxSelectedBg));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorColor::AccentHover);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, EditorColor::AccentActive);
		ImGui::PushStyleColor(ImGuiCol_Text, EditorColor::TextOnAccent);
	}

	inline void PopSelectedButton(bool bSelected)
	{
		if (bSelected)
			ImGui::PopStyleColor(4);
	}

	// 아웃라이너처럼 패널 맨 위에 두는 꽉 찬 폭의 검색창. 내용이 바뀌면 true.
	inline bool SearchBox(const char* Id, char* Buffer, size_t BufferSize)
	{
		ImGui::SetNextItemWidth(-FLT_MIN);
		return ImGui::InputTextWithHint(Id, "Search...", Buffer, BufferSize);
	}

	// 대소문자를 무시한 부분 문자열 검색. 빈 패턴은 항상 일치한다.
	inline bool ContainsIgnoreCase(const char* Text, const char* Pattern)
	{
		if (!Pattern || !*Pattern)
			return true;
		if (!Text)
			return false;
		for (; *Text; ++Text)
		{
			const char* T = Text;
			const char* P = Pattern;
			while (*T && *P && ImToUpper(*T) == ImToUpper(*P))
			{
				++T;
				++P;
			}
			if (!*P)
				return true;
		}
		return false;
	}

	inline void SameLineOrWrap(float NextWidth)
	{
		const float Right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
		if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + NextWidth <= Right)
			ImGui::SameLine();
	}

	// ------------------------------------------------
	// Theme
	// ------------------------------------------------

	inline const char* GetThemeName(EEditorTheme Theme)
	{
		switch (Theme)
		{
		case EEditorTheme::Dark:
			return "Dark";
		case EEditorTheme::Light:
			return "Light";
		case EEditorTheme::Kurisu:
			return "Kurisu";
		default:
			return "Kurisu";
		}
	}

	// editor.ini 값처럼 이름으로 테마를 찾는다. 대소문자는 무시한다.
	inline bool ParseThemeName(const char* Name, EEditorTheme& OutTheme)
	{
		if (!Name)
			return false;
		for (int Index = 0; Index < static_cast<int>(EEditorTheme::Count); ++Index)
		{
			const EEditorTheme Theme = static_cast<EEditorTheme>(Index);
			if (ImStricmp(Name, GetThemeName(Theme)) == 0)
			{
				OutTheme = Theme;
				return true;
			}
		}
		return false;
	}

	inline EEditorTheme GetTheme()
	{
		return EditorTheme::Detail::ActiveTheme;
	}
	// 테마가 바뀔 때마다 증가한다. ImGuiStyle 사본을 들고 있는 쪽(DPI 원본 스타일 등)이 비교해 다시 칠한다.
	inline unsigned int GetThemeRevision()
	{
		return EditorTheme::Detail::Revision;
	}

	// 기본 위젯 색을 현재 팔레트로 채운다. 크기·여백은 건드리지 않으므로 DPI 배율이 적용된 스타일에도 쓸 수 있다.
	inline void ApplyColors(ImGuiStyle& style)
	{
		ImVec4* colors = style.Colors;

		// 모든 기본 위젯은 동일한 역할 이름을 사용한다. 색 변경은 FEditorPalette에서만 한다.
		colors[ImGuiCol_Text] = EditorColor::ToVec4(EditorColor::TextPrimary);
		colors[ImGuiCol_TextDisabled] = EditorColor::ToVec4(EditorColor::TextDisabled);
		colors[ImGuiCol_TextLink] = EditorColor::ToVec4(EditorColor::AccentText);
		// ImGuiStyle에는 선택 행 글자색 칸이 없어서 Selectable·TreeNode가 읽는 전역 값으로 넘긴다.
		ImGui::SetSelectedTextColor(EditorColor::SelectionText);
		colors[ImGuiCol_TextSelectedBg] = EditorColor::ToVec4(EditorColor::Selection);
		colors[ImGuiCol_WindowBg] = EditorColor::ToVec4(EditorColor::BgPanel);
		colors[ImGuiCol_ChildBg] = EditorColor::ToVec4(EditorColor::Transparent);
		colors[ImGuiCol_PopupBg] = EditorColor::ToVec4(EditorColor::BgElevated);
		colors[ImGuiCol_DockingEmptyBg] = EditorColor::ToVec4(EditorColor::BgDeepest);
		colors[ImGuiCol_Border] = EditorColor::ToVec4(EditorColor::Border);
		colors[ImGuiCol_BorderShadow] = EditorColor::ToVec4(EditorColor::Transparent);
		colors[ImGuiCol_FrameBg] = EditorColor::ToVec4(EditorColor::BgInput);
		colors[ImGuiCol_FrameBgHovered] = EditorColor::ToVec4(EditorColor::BgAccentHover);
		colors[ImGuiCol_FrameBgActive] = EditorColor::ToVec4(EditorColor::AccentDim);
		colors[ImGuiCol_InputTextCursor] = EditorColor::ToVec4(EditorColor::TextPrimary);
		colors[ImGuiCol_TitleBg] = EditorColor::ToVec4(EditorColor::BgFrame);
		colors[ImGuiCol_TitleBgActive] = EditorColor::ToVec4(EditorColor::BgFrame);
		colors[ImGuiCol_TitleBgCollapsed] = EditorColor::ToVec4(EditorColor::BgFrame);
		colors[ImGuiCol_MenuBarBg] = EditorColor::ToVec4(EditorColor::BgFrame);
		colors[ImGuiCol_ScrollbarBg] = EditorColor::ToVec4(EditorColor::BgDeepest);
		colors[ImGuiCol_ScrollbarGrab] = EditorColor::ToVec4(EditorColor::BorderStrong);
		colors[ImGuiCol_ScrollbarGrabHovered] = EditorColor::ToVec4(EditorColor::TextDisabled);
		colors[ImGuiCol_ScrollbarGrabActive] = EditorColor::ToVec4(EditorColor::AccentActive);
		colors[ImGuiCol_CheckboxSelectedBg] = EditorColor::ToVec4(EditorColor::Accent);
		colors[ImGuiCol_CheckMark] = EditorColor::ToVec4(EditorColor::TextPrimary);
		colors[ImGuiCol_SliderGrab] = EditorColor::ToVec4(EditorColor::TextDisabled);
		colors[ImGuiCol_SliderGrabActive] = EditorColor::ToVec4(EditorColor::AccentHover);
		colors[ImGuiCol_Button] = EditorColor::ToVec4(EditorColor::BgElevated);
		colors[ImGuiCol_ButtonHovered] = EditorColor::ToVec4(EditorColor::BgAccentHover);
		colors[ImGuiCol_ButtonActive] = EditorColor::ToVec4(EditorColor::AccentActive);
		colors[ImGuiCol_Header] = EditorColor::ToVec4(EditorColor::Selection);
		colors[ImGuiCol_HeaderHovered] = EditorColor::ToVec4(EditorColor::SelectionHover);
		colors[ImGuiCol_HeaderActive] = EditorColor::ToVec4(EditorColor::AccentActive);
		colors[ImGuiCol_Separator] = EditorColor::ToVec4(EditorColor::Border);
		colors[ImGuiCol_SeparatorHovered] = EditorColor::ToVec4(EditorColor::BorderStrong);
		colors[ImGuiCol_SeparatorActive] = EditorColor::ToVec4(EditorColor::Accent);
		colors[ImGuiCol_ResizeGrip] = EditorColor::ToVec4(EditorColor::BorderStrong);
		colors[ImGuiCol_ResizeGripHovered] = EditorColor::ToVec4(EditorColor::TextMuted);
		colors[ImGuiCol_ResizeGripActive] = EditorColor::ToVec4(EditorColor::Accent);
		colors[ImGuiCol_Tab] = EditorColor::ToVec4(EditorColor::BgDeepest);
		colors[ImGuiCol_TabHovered] = EditorColor::ToVec4(EditorColor::BgAccentHover);
		colors[ImGuiCol_TabSelected] = EditorColor::ToVec4(EditorColor::BgElevated);
		colors[ImGuiCol_TabDimmed] = EditorColor::ToVec4(EditorColor::BgDeepest);
		colors[ImGuiCol_TabDimmedSelected] = EditorColor::ToVec4(EditorColor::BgRaised);
		colors[ImGuiCol_TabSelectedOverline] = EditorColor::ToVec4(EditorColor::Accent);
		colors[ImGuiCol_TabDimmedSelectedOverline] = EditorColor::ToVec4(EditorColor::AccentDim);
		colors[ImGuiCol_DockingPreview] = EditorColor::ToVec4(EditorColor::WithAlpha(EditorColor::Accent, 166));
		colors[ImGuiCol_PlotLines] = EditorColor::ToVec4(EditorColor::TextMuted);
		colors[ImGuiCol_PlotLinesHovered] = EditorColor::ToVec4(EditorColor::AccentHover);
		colors[ImGuiCol_PlotHistogram] = EditorColor::ToVec4(EditorColor::TextSecondary);
		colors[ImGuiCol_PlotHistogramHovered] = EditorColor::ToVec4(EditorColor::AccentHover);
		colors[ImGuiCol_TableHeaderBg] = EditorColor::ToVec4(EditorColor::BgElevated);
		colors[ImGuiCol_TableBorderStrong] = EditorColor::ToVec4(EditorColor::Border);
		colors[ImGuiCol_TableBorderLight] = EditorColor::ToVec4(EditorColor::BgRaised);
		colors[ImGuiCol_TableRowBg] = EditorColor::ToVec4(EditorColor::Transparent);
		colors[ImGuiCol_TableRowBgAlt] = EditorColor::ToVec4(EditorColor::BgRaised);
		colors[ImGuiCol_TreeLines] = EditorColor::ToVec4(EditorColor::Border);
		colors[ImGuiCol_DragDropTarget] = EditorColor::ToVec4(EditorColor::AccentHover);
		colors[ImGuiCol_DragDropTargetBg] = EditorColor::ToVec4(EditorColor::WithAlpha(EditorColor::Accent, 46));
		colors[ImGuiCol_UnsavedMarker] = EditorColor::ToVec4(EditorColor::Accent);
		colors[ImGuiCol_NavCursor] = EditorColor::ToVec4(EditorColor::Accent);
		colors[ImGuiCol_NavWindowingHighlight] = EditorColor::ToVec4(EditorColor::Accent);
		colors[ImGuiCol_NavWindowingDimBg] = EditorColor::ToVec4(EditorColor::Scrim);
		colors[ImGuiCol_ModalWindowDimBg] = EditorColor::ToVec4(EditorColor::Scrim);
	}

	// 테마를 바꾼다. 패널이 그리는 색은 다음에 그릴 때 바로 바뀌고, ImGui 기본 위젯 색은 여기서 즉시 다시 칠한다.
	// 사본 스타일을 가진 쪽은 GetThemeRevision으로 변경을 감지한다.
	inline void SetTheme(EEditorTheme Theme)
	{
		if (static_cast<int>(Theme) < 0 || Theme >= EEditorTheme::Count)
			Theme = EditorTheme::DefaultTheme;
		if (Theme == EditorTheme::Detail::ActiveTheme)
			return;

		EditorTheme::Detail::Active = EditorTheme::MakePalette(Theme);
		EditorTheme::Detail::ActiveTheme = Theme;
		++EditorTheme::Detail::Revision;
		if (ImGui::GetCurrentContext())
			ApplyColors(ImGui::GetStyle());
	}

	// 테마 목록에 없는 팔레트를 잠시 쓴다(프로젝트 브라우저). 이후 SetTheme을 부르면 어떤 테마든 다시 적용된다.
	inline void UseCustomPalette(const FEditorPalette& Palette)
	{
		EditorTheme::Detail::Active = Palette;
		EditorTheme::Detail::ActiveTheme = EEditorTheme::Count;
		++EditorTheme::Detail::Revision;
		if (ImGui::GetCurrentContext())
			ApplyColors(ImGui::GetStyle());
	}

	// 메뉴 바(프레임 색) 위의 최상위 메뉴. 라벨만 프레임 글자색으로 그리고, 펼친 팝업 안은 평소 글자색을 쓴다.
	// 팝업 안의 하위 메뉴는 ImGui::BeginMenu를 그대로 쓴다.
	inline bool BeginMenuBarMenu(const char* Label, bool bEnabled = true)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, EditorColor::TextOnFrame);
		const bool bOpen = ImGui::BeginMenu(Label, bEnabled);
		ImGui::PopStyleColor();
		return bOpen;
	}

	// 메뉴 안에 테마 선택 항목을 그린다. 테마를 바꿨으면 true.
	inline bool ThemeMenuItems()
	{
		bool bChanged = false;
		for (int Index = 0; Index < static_cast<int>(EEditorTheme::Count); ++Index)
		{
			const EEditorTheme Theme = static_cast<EEditorTheme>(Index);
			if (ImGui::MenuItem(GetThemeName(Theme), nullptr, GetTheme() == Theme) && GetTheme() != Theme)
			{
				SetTheme(Theme);
				bChanged = true;
			}
		}
		return bChanged;
	}

	// 스타일 전체를 초기값으로 되돌리고 현재 테마 색과 96 DPI 기준 크기를 적용한다.
	inline void Apply()
	{
		ImGui::GetStyle() = ImGuiStyle();
		ImGuiStyle& style = ImGui::GetStyle();
		ApplyColors(style);

		// ------------------------------------------------
		// Layout & Spacing
		// ------------------------------------------------

		// Padding
		style.WindowPadding = ImVec2(8.0f, 8.0f);
		style.FramePadding = ImVec2(8.0f, 4.0f);
		style.ItemSpacing = ImVec2(8.0f, 4.0f);
		style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
		style.CellPadding = ImVec2(6.0f, 4.0f);
		style.IndentSpacing = 18.0f;

		// Widget Sizes
		style.GrabMinSize = 10.0f;
		style.ScrollbarSize = 14.0f;

		// Rounding
		style.WindowRounding = 0.0f;
		style.ChildRounding = 2.0f;
		style.FrameRounding = 2.0f;
		style.PopupRounding = 4.0f;
		style.ScrollbarRounding = 7.0f;
		style.TabRounding = 2.0f;

		// Borders
		style.FrameBorderSize = 1.0f;
		style.TabBorderSize = 1.0f;

		// Minimum Window Size
		style.WindowMinSize = ImVec2(160.0f, 100.0f);
	}
}
