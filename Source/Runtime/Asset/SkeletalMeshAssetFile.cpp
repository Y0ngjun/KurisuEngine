#include "EnginePCH.h"
#include "SkeletalMeshAssetFile.h"
#include "Asset/AssetUtils.h"
#include "Asset/FbxImporter/FbxMeshBake.h"
#include "Core/Misc/Guid.h"
#include "Core/Misc/Paths.h"
#include "Serialization/Formatter/JsonArchiveInputFormatter.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"
#include "Serialization/MathSerialization.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/StructuredArchive.h"
#include <bit>
#include <fstream>
#include <limits>
#include <unordered_set>

namespace
{
	constexpr int32 CurrentVersion = 1;

	template <typename T, typename F>
	void SerializeArray(FStructuredArchiveRecord Record, FArchiveFieldName Name, TArray<T>& Values, F Serialize)
	{
		int32 Count = Values.Num();
		auto Array = Record.EnterArray(Name, Count);
		if (Record.GetUnderlyingArchive().IsLoading())
			Values.SetNum(Count);
		for (T& Value : Values)
			Serialize(Array.EnterElement(), Value);
	}

	void SerializeLOD(FStructuredArchiveSlot Slot, FClothingLODData& Data)
	{
		auto Record = Slot.EnterRecord();
		Record << SA_VALUE("SourceRenderLOD", Data.SourceRenderLOD)
		       << SA_VALUE("SourceSectionIndex", Data.SourceSectionIndex)
		       << SA_VALUE("SourceSectionSignature", Data.SourceSectionSignature);
		const auto Value = [](FStructuredArchiveSlot Item, auto& Entry) { Item << Entry; };
		SerializeArray(Record, "RestPositions", Data.RestPositions, Value);
		SerializeArray(Record, "MaxDistances", Data.MaxDistances, Value);
		SerializeArray(Record, "TriangleIndices", Data.TriangleIndices, Value);
		SerializeArray(Record, "RenderToSimVertex", Data.RenderToSimVertex, Value);
		SerializeArray(Record, "SkinWeights", Data.SkinWeights, [](FStructuredArchiveSlot Item, FSkinWeight& Skin)
		{
			auto SkinRecord = Item.EnterRecord();
			int32 Count = MaxBoneInfluences;
			{
				auto Indices = SkinRecord.EnterArray("BoneIndices", Count);
				if (Count != MaxBoneInfluences)
					throw std::runtime_error("Invalid Cloth bone index count");
				for (uint8& Index : Skin.BoneIndex)
					Indices << Index;
			}
			auto Weights = SkinRecord.EnterArray("Weights", Count);
			if (Count != MaxBoneInfluences)
				throw std::runtime_error("Invalid Cloth weight count");
			for (float& Weight : Skin.Weight)
				Weights << Weight;
		});
	}

	void Serialize(FStructuredArchiveRecord Record, FSkeletalMeshAssetData& Data)
	{
		int32 Version = Record.GetUnderlyingArchive().IsLoading() ? 0 : CurrentVersion;
		Record << SA_VALUE("Version", Version);
		if (Version != CurrentVersion)
			throw std::runtime_error("Unsupported .skeletalmesh version");
		Record << SA_VALUE("BakedMeshPath", Data.BakedMeshPath);
		if (auto Field = Record.TryEnterField("PhysicsAssetPath", !Data.PhysicsAssetPath.empty()))
			Field.GetValue() << Data.PhysicsAssetPath;
		SerializeArray(Record, "ClothingAssets", Data.ClothingAssets, [](FStructuredArchiveSlot Slot, FClothingAsset& Asset)
		{
			auto Entry = Slot.EnterRecord();
			Entry << SA_VALUE("Id", Asset.Id) << SA_VALUE("Name", Asset.Name);
			SerializeArray(Entry, "LODs", Asset.LODs, SerializeLOD);
			auto Config = Entry.EnterRecord("Config");
			Config << SA_VALUE("GravityZ", Asset.Config.GravityZ)
			       << SA_VALUE("SolverFrequency", Asset.Config.SolverFrequency)
			       << SA_VALUE("Damping", Asset.Config.Damping)
			       << SA_VALUE("Stiffness", Asset.Config.Stiffness)
			       << SA_VALUE("Friction", Asset.Config.Friction)
			       << SA_VALUE("bEnableContinuousCollision", Asset.Config.bEnableContinuousCollision);
			Entry << SA_VALUE("bUsePhysicsAssetCollision", Asset.bUsePhysicsAssetCollision)
			      << SA_VALUE("CollisionPhysicsAssetPath", Asset.CollisionPhysicsAssetPath);
		});
		SerializeArray(Record, "ClothingBindings", Data.ClothingBindings, [](FStructuredArchiveSlot Slot, FSkeletalMeshAssetBinding& Binding)
		{
			Slot.EnterRecord() << SA_VALUE("RenderLOD", Binding.RenderLOD)
			                   << SA_VALUE("SectionIndex", Binding.SectionIndex)
			                   << SA_VALUE("ClothingAssetId", Binding.ClothingAssetId)
			                   << SA_VALUE("ClothLODIndex", Binding.ClothLODIndex)
			                   << SA_VALUE("SectionSignature", Binding.SectionSignature)
			                   << SA_VALUE("Enabled", Binding.Enabled);
		});
	}

	void Validate(const FSkeletalMeshAssetData& Data)
	{
		using namespace SkeletalMeshAssetFile;
		if (!FFbxMeshBake::IsBakedPath(Data.BakedMeshPath) || ResolveContentReference(Data.BakedMeshPath).empty())
			throw std::runtime_error("Invalid project-relative baked mesh path");
		if (!Data.PhysicsAssetPath.empty() &&
		    (ResolveContentReference(Data.PhysicsAssetPath).empty() ||
		     !Data.PhysicsAssetPath.ends_with(".physicsasset")))
			throw std::runtime_error("Invalid project-relative Physics Asset path");
		std::unordered_set<FString> Ids;
		for (const auto& Cloth : Data.ClothingAssets)
		{
			FGuid Id;
			if (!FGuid::Parse(Cloth.Id, Id) || !Id.IsValid() || Id.ToString() != Cloth.Id || !Ids.insert(Cloth.Id).second)
				throw std::runtime_error("Invalid or duplicate Cloth ID");
		}
	}
}

bool SkeletalMeshAssetFile::IsAssetPath(const FString& Path)
{
	return _wcsicmp(FPaths::FromUtf8(Path).extension().c_str(), L".skeletalmesh") == 0;
}

FString SkeletalMeshAssetFile::ProjectContentReference(const FString& Path)
{
	const FString File = FAssetUtils::ResolveAssetFilePath(Path);
	if (File.empty() || !FPaths::HasProject())
		return {};
	std::error_code Error;
	const auto Relative = std::filesystem::relative(FPaths::FromUtf8(File), FPaths::ProjectDir(), Error);
	if (Error || Relative.empty() || Relative.is_absolute() || *Relative.begin() != L"Content")
		return {};
	return FPaths::ToUtf8(Relative);
}

FString SkeletalMeshAssetFile::ResolveContentReference(const FString& Reference)
{
	const auto Path = FPaths::FromUtf8(Reference);
	if (Path.empty() || Path.has_root_path() || *Path.begin() != L"Content")
		return {};
	for (const auto& Part : Path)
		if (Part == L"..")
			return {};
	const FString File = FPaths::ToUtf8(FPaths::ProjectDir() / Path);
	return ProjectContentReference(File).empty() ? FString{} : File;
}

std::filesystem::path SkeletalMeshAssetFile::DocumentPath(const std::filesystem::path& Bake)
{
	auto Path = Bake;
	return Path.replace_extension().replace_extension(".skeletalmesh");
}

bool SkeletalMeshAssetFile::CreateDefault(const FString& BakePath, FSkeletalMeshAssetData& OutData, FString& OutError)
{
	OutData = {};
	OutData.BakedMeshPath = ProjectContentReference(BakePath);
	if (OutData.BakedMeshPath.empty())
	{
		OutError = "Skeletal mesh bake must be inside project Content";
		return false;
	}
	const FString Legacy = "Content/Physics/PhysicsAssets/" + FAssetUtils::GetAssetKey(BakePath) + ".physicsasset";
	std::error_code Error;
	if (std::filesystem::is_regular_file(FPaths::ProjectDir() / FPaths::FromUtf8(Legacy), Error))
		OutData.PhysicsAssetPath = Legacy;
	return true;
}

bool SkeletalMeshAssetFile::Read(const std::filesystem::path& Path, FSkeletalMeshAssetData& OutData, FString& OutError)
{
	OutError.clear();
	try
	{
		std::ifstream File(Path, std::ios::binary | std::ios::ate);
		const auto Size = static_cast<std::streamoff>(File.tellg());
		if (!File || Size <= 0 || Size > (std::numeric_limits<int32>::max)())
			throw std::runtime_error("Cannot open file or invalid size");
		TArray<uint8> Bytes;
		Bytes.SetNum(static_cast<int32>(Size));
		File.seekg(0);
		if (!File.read(reinterpret_cast<char*>(Bytes.GetData()), Size))
			throw std::runtime_error("Cannot read file");
		FMemoryReader Reader(Bytes, true);
		FJsonArchiveInputFormatter Formatter(Reader);
		if (Reader.IsError())
			throw std::runtime_error("Invalid JSON");
		FStructuredArchive Archive(Formatter);
		FSkeletalMeshAssetData Data;
		Serialize(Archive.Open().EnterRecord(), Data);
		if (Reader.IsError())
			throw std::runtime_error("Invalid asset data");
		Validate(Data);
		OutData = std::move(Data);
		return true;
	}
	catch (const std::exception& Exception)
	{
		OutError = FPaths::ToUtf8(Path) + ": " + Exception.what();
		return false;
	}
}

bool SkeletalMeshAssetFile::Write(const std::filesystem::path& Path, FSkeletalMeshAssetData& Data, FString& OutError)
{
	OutError.clear();
	auto Temp = Path;
	Temp += FPaths::FromUtf8("." + FGuid::NewGuid().ToString() + ".tmp");
	try
	{
		Validate(Data);
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes);
		{
			FJsonArchiveOutputFormatter Formatter(Writer);
			FStructuredArchive Archive(Formatter);
			Serialize(Archive.Open().EnterRecord(), Data);
		}
		if (Writer.IsError())
			throw std::runtime_error("Cannot serialize asset");
		std::ofstream File(Temp, std::ios::binary | std::ios::trunc);
		File.write(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num());
		File.close();
		if (!File || !MoveFileExW(Temp.c_str(), Path.c_str(), MOVEFILE_REPLACE_EXISTING))
			throw std::runtime_error("Cannot write or replace asset file");
		return true;
	}
	catch (const std::exception& Exception)
	{
		std::error_code Ignore;
		std::filesystem::remove(Temp, Ignore);
		OutError = FPaths::ToUtf8(Path) + ": " + Exception.what();
		return false;
	}
}

FString SkeletalMeshAssetFile::SectionSignature(const TArray<FVertex>& Vertices, const TArray<FSkinWeight>& Weights,
                                                const TArray<uint32>& Indices, const TArray<FBoneInfo>& Bones,
                                                const FSkeletalMeshSection& Section)
{
	if (Section.StartIndex > static_cast<uint32>(Indices.Num()) ||
	    Section.IndexCount > static_cast<uint32>(Indices.Num()) - Section.StartIndex)
		return {};

	uint64 Hash = 14695981039346656037ull;
	const auto Add = [&Hash](uint32 Value)
	{
		for (int32 Shift = 0; Shift < 32; Shift += 8)
		{
			Hash ^= (Value >> Shift) & 0xff;
			Hash *= 1099511628211ull;
		}
	};
	const auto AddFloat = [&Add](float Value) { Add(std::bit_cast<uint32>(Value)); };
	Add(Vertices.Num());
	Add(Section.IndexCount);
	TArray<uint8> UsedBones;
	UsedBones.Init(0, Bones.Num());
	for (uint32 Offset = 0; Offset < Section.IndexCount; ++Offset)
	{
		const uint32 Vertex = Indices[Section.StartIndex + Offset];
		if (!Vertices.IsValidIndex(Vertex) || !Weights.IsValidIndex(Vertex))
			return {};
		Add(Vertex);
		const auto& Position = Vertices[Vertex].Position;
		AddFloat(Position.X);
		AddFloat(Position.Y);
		AddFloat(Position.Z);
		const auto& Skin = Weights[Vertex];
		for (int32 Influence = 0; Influence < MaxBoneInfluences; ++Influence)
		{
			Add(Skin.BoneIndex[Influence]);
			AddFloat(Skin.Weight[Influence]);
			if (Skin.Weight[Influence] <= 0)
				continue;
			for (int32 Index = Skin.BoneIndex[Influence]; Index >= 0; Index = Bones[Index].ParentIndex)
			{
				if (!Bones.IsValidIndex(Index))
					return {};
				if (UsedBones[Index])
					break;
				UsedBones[Index] = 1;
			}
		}
	}
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		if (!UsedBones[Index])
			continue;
		const auto& Bone = Bones[Index];
		Add(Index);
		Add(Bone.ParentIndex);
		Add(static_cast<uint32>(Bone.Name.size()));
		for (unsigned char Character : Bone.Name)
			Add(Character);
		const auto& Pose = Bone.LocalBindPose;
		for (float Value : {Pose.Location.X, Pose.Location.Y, Pose.Location.Z,
		                    Pose.Rotation.X, Pose.Rotation.Y, Pose.Rotation.Z, Pose.Rotation.W,
		                    Pose.Scale.X, Pose.Scale.Y, Pose.Scale.Z})
			AddFloat(Value);
		for (const auto& Row : Bone.InverseBindPose.M)
			for (float Value : Row)
				AddFloat(Value);
	}
	return std::format("{:016x}", Hash);
}

FString SkeletalMeshAssetFile::SectionSignature(const FSkeletalMeshData& Data, int32 RenderLOD, int32 SectionIndex)
{
	if (RenderLOD != 0)
		return {};
	if (Data.Sections.IsEmpty() && SectionIndex == 0)
		return SectionSignature(Data.BindVertices, Data.SkinWeight, Data.Indices, Data.Bones,
		                        {0, static_cast<uint32>(Data.Indices.Num()), 0});
	if (!Data.Sections.IsValidIndex(SectionIndex))
		return {};
	return SectionSignature(Data.BindVertices, Data.SkinWeight, Data.Indices, Data.Bones, Data.Sections[SectionIndex]);
}
