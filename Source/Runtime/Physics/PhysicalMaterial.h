#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "PhysicalMaterial.generated.h"

UCLASS()
class UPhysicalMaterial : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0", Delta = "0.01"))
	float StaticFriction = 0.5f;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0", Delta = "0.01"))
	float Friction = 0.5f;
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0", ClampMax = "1", Delta = "0.01"))
	float Restitution = 0.1f;
	// Same unit as Unreal: g/cm^3. Convert to kg/cm^3 for this engine's PhysX world.
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.001", Delta = "0.1"))
	float Density = 1.0f;
	// 차량 타이어가 이 재질 위에서 내는 마찰의 배율 (UE4 UPhysicalMaterial::TireFrictionScale 대응, 6.4).
	// 1이면 기본 도로, 얼음은 0.2 안팎, 잔디/흙은 0.6 안팎. 일반 물체끼리의 마찰(Friction)과는 별개다.
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0", Delta = "0.01"))
	float TireFrictionScale = 1.0f;
	const FString& GetAssetPath() const { return AssetPath; }
	void SetAssetPath(const FString& Path) { AssetPath = Path; }
	bool IsDirty() const { return bDirty; }
	void MarkSaved() { bDirty = false; }
	bool SaveAsset(const FString& Path = {});
	bool ReloadAsset();
	static bool IsPhysicalMaterialPath(const FString& Path);
	bool IsValidMaterial() const;
	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;
	virtual void PostEditChangeProperty(const FProperty& Property) override;
	static void SerializeReference(FStructuredArchive::FSlot Slot, UPhysicalMaterial*& Material);
private:
	FString AssetPath;
	bool bDirty = false;
};
