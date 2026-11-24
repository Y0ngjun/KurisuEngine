#pragma once

struct ID3D11Device;
struct ID3D11DeviceContext;
struct FClothingLODData;
struct FClothCollisionBinding;
struct FClothCollisionFrame;
struct FClothConfig;

enum class EClothBackend
{
	CPU,
	DX11,
};

class FClothSystem
{
public:
	FClothSystem();
	~FClothSystem();

	bool Init(EClothBackend Backend,
	          ID3D11Device* Device = nullptr,
	          ID3D11DeviceContext* Context = nullptr);
	bool IsInitialized() const;

private:
	friend class FClothScene;

	struct FImpl;
	TUniquePtr<FImpl> Impl;
};

class FClothScene
{
public:
	FClothScene();
	~FClothScene();

	bool Init(FClothSystem& System);

	bool RemoveCloth(uint64 InstanceId);
	int32 GetClothCount() const;

	bool Tick(float DeltaTime);

	bool AddClothingMesh(uint64 InstanceId, const FClothingLODData& Data,
	                     const FClothConfig& Config);
	bool ReadClothPositions(uint64 InstanceId, TArray<FVector>& OutPositions) const;
	bool UpdateClothInputs(
	    uint64 InstanceId,
	    const FClothingLODData& Data,
	    const TArray<FVector>& SkinnedPositions,
	    const FVector& WorldLocation,
	    const FQuat& WorldRotation,
	    float GravityZ,
		const FClothCollisionBinding& Binding,
		const FClothCollisionFrame& Frame,
	    bool bReset);

	bool SetClothActive(uint64 InstanceId, bool bActive);

private:
	struct FImpl;
	TUniquePtr<FImpl> Impl;
};
