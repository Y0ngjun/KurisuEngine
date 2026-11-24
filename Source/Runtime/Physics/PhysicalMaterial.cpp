#include "EnginePCH.h"
#include "Physics/PhysicalMaterial.h"
#include "Physics/BodySetup.h"
#include "Components/PrimitiveComponent.h"
#include "UObject/UObjectIterator.h"
#include <cmath>
#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "Core/Misc/Paths.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"
#include "Serialization/Formatter/JsonArchiveInputFormatter.h"
#include <fstream>

void UPhysicalMaterial::Serialize(FStructuredArchive::FRecord Record)
{
	FArchive& Ar = Record.GetUnderlyingArchive();
	TOptional<float> LegacyDynamicFriction;
	if (Ar.IsLoading() && !Ar.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate))
	{
		if (auto Slot = Record.TryEnterField("Properties", false))
		{
			auto Properties = Slot.GetValue().EnterRecord();
			if (auto Current = Properties.TryEnterField("Friction", false))
			{
				float Value = 0.5f;
				Current.GetValue() << Value;
			}
			else if (auto Old = Properties.TryEnterField("DynamicFriction", false))
			{
				float Value = 0.5f;
				Old.GetValue() << Value;
				LegacyDynamicFriction = Value;
			}
		}
	}
	Super::Serialize(Record);
	if (LegacyDynamicFriction.IsSet())
		Friction = LegacyDynamicFriction.GetValue();
}

bool UPhysicalMaterial::IsValidMaterial() const
{
	return std::isfinite(StaticFriction) && StaticFriction >= 0 &&
	       std::isfinite(Friction) && Friction >= 0 &&
	       std::isfinite(Restitution) && Restitution >= 0 && Restitution <= 1 &&
	       std::isfinite(Density) && Density > 0 &&
	       std::isfinite(TireFrictionScale) && TireFrictionScale >= 0;
}

void UPhysicalMaterial::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	bDirty = true;
	if (!IsValidMaterial())
	{
		StaticFriction = std::isfinite(StaticFriction) ? std::max(StaticFriction, 0.0f) : 0.5f;
		Friction = std::isfinite(Friction) ? std::max(Friction, 0.0f) : 0.5f;
		Restitution = std::isfinite(Restitution) ? std::clamp(Restitution, 0.0f, 1.0f) : 0.1f;
		Density = std::isfinite(Density) ? std::max(Density, 0.001f) : 1.0f;
		TireFrictionScale = std::isfinite(TireFrictionScale) ? std::max(TireFrictionScale, 0.0f) : 1.0f;
	}
	for (TObjectIterator<UBodySetup> It; It; ++It)
		if (It->PhysMaterial == this || It->DefaultInstance.PhysMaterialOverride == this)
			It->InvalidatePhysicsData();
	for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
		if (It->GetBodyInstance().PhysMaterialOverride == this)
			It->RecreatePhysicsState();
}

void UPhysicalMaterial::SerializeReference(FStructuredArchive::FSlot Slot, UPhysicalMaterial*& Material)
{
	FArchive& Ar = Slot.GetUnderlyingArchive();
	if (Ar.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate))
	{
		UObject* Ref = Material;
		Slot << Ref;
		if (Ar.IsLoading())
			Material = Cast<UPhysicalMaterial>(Ref);
		return;
	}
	auto Record = Slot.EnterRecord();
    if (auto Reference = Record.TryEnterField("Asset", Ar.IsSaving() && Material && !Material->GetAssetPath().empty()))
    {
        FString Path = Material ? Material->GetAssetPath() : FString{};
        Reference.GetValue() << Path;
        if (Ar.IsLoading()) Material = UAssetManager::LoadPhysicalMaterial(Path);
        return;
    }
    // Legacy unnamed materials remain readable; Content Browser assets serialize by path.
    if (auto Value = Record.TryEnterField("Material", Ar.IsSaving() && Material))
	{
		if (Ar.IsLoading())
			Material = NewObject<UPhysicalMaterial>();
		Material->Serialize(Value.GetValue().EnterRecord());
	}
	else if (Ar.IsLoading())
		Material = nullptr;
}


bool UPhysicalMaterial::IsPhysicalMaterialPath(const FString& Path)
{
    FString Extension = FPaths::FromUtf8(Path).extension().string();
    std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char C){ return static_cast<char>(std::tolower(C)); });
    return Extension == ".physmat";
}

bool UPhysicalMaterial::SaveAsset(const FString& InPath)
{
    const FString Path = InPath.empty() ? AssetPath : InPath;
    if (!IsPhysicalMaterialPath(Path) || !IsValidMaterial()) return false;
    const FString Resolved = FAssetUtils::ResolveAssetFilePath(Path);
    if (Resolved.empty()) return false;
    const auto FilePath = FPaths::FromUtf8(Resolved);
    TArray<uint8> Bytes;
    FMemoryWriter Writer(Bytes);
    {
        FJsonArchiveOutputFormatter Formatter(Writer);
        FStructuredArchive Archive(Formatter);
        auto Root = Archive.Open().EnterRecord();
        int32 Version = 1;
        Root << SA_VALUE("PhysicalMaterialVersion", Version);
        Serialize(Root.EnterRecord("Object"));
        Archive.Close();
    }
    if (Writer.IsError()) return false;
    std::error_code Error;
    std::filesystem::create_directories(FilePath.parent_path(), Error);
    if (Error) return false;
    auto Temporary = FilePath; Temporary += ".tmp";
    std::ofstream File(Temporary, std::ios::binary | std::ios::trunc);
    File.write(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num());
    File.flush(); if (!File) return false; File.close();
    if (!MoveFileExW(Temporary.c_str(), FilePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return false;
    AssetPath = FAssetUtils::GetAssetKey(Resolved);
    MarkSaved();
    UAssetManager::RegisterPhysicalMaterial(this);
    return true;
}

bool UPhysicalMaterial::ReloadAsset()
{
    if (AssetPath.empty()) return false;
    std::ifstream File(FPaths::FromUtf8(FAssetUtils::ResolveAssetFilePath(AssetPath)), std::ios::binary | std::ios::ate);
    if (!File) return false;
    const auto Size = File.tellg();
    if (Size <= 0 || Size > 1024 * 1024) return false;
    TArray<uint8> Bytes; Bytes.SetNum(static_cast<int32>(Size));
    File.seekg(0); File.read(reinterpret_cast<char*>(Bytes.GetData()), Bytes.Num());
    if (!File) return false;
    // Parse into a temporary object so a failed reload cannot corrupt a shared live material.
    auto Candidate = std::unique_ptr<UPhysicalMaterial>(NewObject<UPhysicalMaterial>());
    FMemoryReader Reader(Bytes, true);
    FJsonArchiveInputFormatter Formatter(Reader);
    if (Reader.IsError()) return false;
    FStructuredArchive Archive(Formatter);
    auto Root = Archive.Open().EnterRecord();
    int32 Version = 0; Root << SA_VALUE("PhysicalMaterialVersion", Version);
    Candidate->Serialize(Root.EnterRecord("Object"));
    Archive.Close();
    if (Reader.IsError() || Version != 1 || !Candidate->IsValidMaterial()) return false;
    StaticFriction = Candidate->StaticFriction; Friction = Candidate->Friction;
    Restitution = Candidate->Restitution; Density = Candidate->Density; TireFrictionScale = Candidate->TireFrictionScale;
    if (const FProperty* Property = GetClass()->FindPropertyByName("Density")) PostEditChangeProperty(*Property);
    MarkSaved();
    return true;
}
