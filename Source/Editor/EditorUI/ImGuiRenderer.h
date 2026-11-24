#pragma once

#include <imgui.h>
#include <Windows.h>

#include <d3d11.h>
#include <wrl/client.h>
#include <vector>

class FImGuiRenderer
{
public:
	FImGuiRenderer() = default;
	~FImGuiRenderer();

	bool Init(HWND WindowHandle, ID3D11Device* Device, ID3D11DeviceContext* DeviceContext);
	void Begin();
	void End();
	void Shutdown();
	// 다음 Begin()에서 창/도킹 배치를 엔진 기본 imgui.ini(새 프로젝트에 복사하는 파일) 값으로 되돌린다.
	static void RequestLayoutReset();
	bool DrawShadowMap(ID3D11ShaderResourceView* AtlasSRV, unsigned int AtlasIndex,
	                   ImVec2 Size, ImVec2 UV0, ImVec2 UV1, float Contrast);

private:
	void UpdateDpiScale();
	static void ApplyPendingLayoutReset();
	static inline bool bLayoutResetRequested = false;
	HWND MainWindowHandle = nullptr;
	ImGuiStyle UnscaledStyle;
	float AppliedDpiScale = 0.0f;
	unsigned int AppliedThemeRevision = 0;
	static void BindShadowPreview(const ImDrawList*, const ImDrawCmd* Command);
	static void SetShadowPreviewSlice(const ImDrawList*, const ImDrawCmd* Command);
	static void RestoreShadowPreview(const ImDrawList*, const ImDrawCmd* Command);
	Microsoft::WRL::ComPtr<ID3D11PixelShader> ShadowPreviewPS;
	Microsoft::WRL::ComPtr<ID3D11Buffer> ShadowPreviewCB;
	Microsoft::WRL::ComPtr<ID3D11SamplerState> ShadowPreviewSampler;
	Microsoft::WRL::ComPtr<ID3D11Buffer> PreviousPSConstants;
	std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> PreviewTextures;
};
