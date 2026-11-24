#pragma once

struct ID3D11Device;
struct ID3D11DeviceContext;

struct FClothSmokeResult
{
	const char* FailedAt = nullptr;
	float FreeVertexZ = 0.0f;

	bool Passed() const { return FailedAt == nullptr; }
};

FClothSmokeResult RunCpuClothSmoke();
FClothSmokeResult RunDx11ClothSmoke(
    ID3D11Device* Device, ID3D11DeviceContext* Context);

void InitializeNvClothCallbacks();
