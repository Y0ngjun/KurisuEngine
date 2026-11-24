#include "EnginePCH.h"
#include "PhysicsAssetCreator.h"

#include <memory>
#include <commdlg.h>

#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "Core/Misc/Paths.h"
#include "Core/Misc/Guid.h"
#include "Physics/PhysicsAsset.h"
#include "Render/SkeletalMesh.h"
#include "imgui.h"

namespace
{
	constexpr const char* DialogTitle = "New Physics Asset";

	// 콘텐츠 드로어는 아직 로드하지 않은 메시도 보여 주므로 필요하면 여기서 읽는다.
	USkeletalMesh* LoadSkeletalMesh(const FString& Path)
	{
		URenderAsset* Asset = UAssetManager::FindLoadedAsset(Path);
		if (!Asset)
		{
			FString Error;
			Asset = UAssetManager::LoadSkeletalMesh(Path, Error);
			if (!Asset)
				KRS_LOG(Warning, "[Physics] Failed to load skeletal mesh {}: {}", Path, Error);
		}
		return Asset && Asset->IsA<USkeletalMesh>() ? static_cast<USkeletalMesh*>(Asset) : nullptr;
	}

	// USkeletalMesh::SavePhysicsAsset이 쓰는 과도기 경로: Content/Physics/PhysicsAssets/<메시 경로>.physicsasset
	FString AssignedPhysicsAssetPath(const USkeletalMesh& Mesh)
	{
		const auto Key = FPaths::FromUtf8(Mesh.GetPath());
		if (!FPaths::HasProject() || Key.empty() || Key.is_absolute() || Key.has_root_name())
			return {};
		auto Path = FPaths::ProjectContentDir() / "Physics" / "PhysicsAssets" / Key;
		Path += ".physicsasset";
		return FPaths::ToUtf8(Path);
	}

	bool IsPhysicsAssetFile(const std::filesystem::path& Path)
	{
		return Path.extension() == ".physicsasset";
	}
}

void FPhysicsAssetCreator::DrawMenuItems(const FString& InMeshPath, const std::filesystem::path& Directory)
{
	if (!ImGui::BeginMenu("Create"))
		return;

	ImGui::SeparatorText("Physics Asset");
	if (ImGui::BeginMenu("Physics Asset"))
	{
		const bool bCreate = ImGui::MenuItem("Create");
		const bool bCreateAndAssign = ImGui::MenuItem("Create and Assign");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Create a new physics asset and assign it to this mesh");
		if (bCreate || bCreateAndAssign)
		{
			MeshPath = InMeshPath;
			CreateDirectory = Directory;
			bAssign = bCreateAndAssign;
			Params = FPhysAssetCreateParams();
			bOpenDialog = true;
		}
		ImGui::EndMenu();
	}
	ImGui::EndMenu();
}

void FPhysicsAssetCreator::ProcessCreateRequest()
{
	// 우클릭 팝업이 닫힌 뒤 같은 ID 스택에서 열어야 모달이 뜬다.
	if (bOpenDialog)
	{
		ImGui::OpenPopup(DialogTitle);
		bOpenDialog = false;
	}
	DrawOptionsDialog();
}

void FPhysicsAssetCreator::DrawOptionsDialog()
{
	ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_Appearing);
	if (!ImGui::BeginPopupModal(DialogTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		return;

	ImGui::TextDisabled("%s", MeshPath.c_str());
	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Body Creation", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// 겹친 바디 충돌 끄기(bDisableCollisionsOfOverlappingBodies)는 옵션으로 두지 않고 항상 켠다 (안 끄면 래그돌이 시작하자마자 튄다).
		ImGui::DragFloat("Min Bone Size", &Params.MinBoneSize, 0.5f, 0.0f, 1000.0f, "%.1f");
		// UE EPhysAssetFitGeomType 순서 그대로 (Convex는 추후 추가)
		static const char* GeomTypeNames[] = {"Box", "Capsule", "Sphere"};
		int GeomType = static_cast<int>(Params.GeomType);
		if (ImGui::Combo("Primitive Type", &GeomType, GeomTypeNames, IM_ARRAYSIZE(GeomTypeNames)))
			Params.GeomType = static_cast<EPhysAssetFitGeomType>(GeomType);
		ImGui::Checkbox("Auto Orient to Bone", &Params.bAutoOrientToBone);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Align each capsule with the bone direction (bone to child bone)");
	}
	if (ImGui::CollapsingHeader("Constraint Creation", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Checkbox("Create Constraints", &Params.bCreateConstraints);
		ImGui::BeginDisabled(!Params.bCreateConstraints);
		// UE와 같이 각도는 받지 않고 모드만 고른다. 각도는 관절 기본값(45°)이고 에디터에서 관절마다 조정한다.
		static const char* ModeNames[] = {"Free", "Limited", "Locked"};
		int Mode = static_cast<int>(Params.AngularConstraintMode);
		if (ImGui::Combo("Angular Constraint Mode", &Mode, ModeNames, IM_ARRAYSIZE(ModeNames)))
			Params.AngularConstraintMode = static_cast<EAngularConstraintMotion>(Mode);
		ImGui::EndDisabled();
	}

	ImGui::Spacing();
	ImGui::Separator();
	if (ImGui::Button("Create Asset", ImVec2(120.0f, 0.0f)))
	{
		CreateAsset();
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
		ImGui::CloseCurrentPopup();

	ImGui::EndPopup();
}

bool FPhysicsAssetCreator::CreateAsset()
{
	USkeletalMesh* Mesh = LoadSkeletalMesh(MeshPath);
	if (!Mesh)
	{
		MessageBoxW(GetActiveWindow(), L"Cannot load the skeletal mesh. See Output Log.", L"Create Physics Asset", MB_OK | MB_ICONERROR);
		return false;
	}

	if (bAssign && Mesh->IsSkeletalMeshAsset())
	{
		if (Mesh->AssetNeedsReload())
		{
			MessageBoxW(GetActiveWindow(), L"Restart the editor before assigning to the reimported mesh.", L"Create Physics Asset", MB_OK | MB_ICONERROR);
			return false;
		}
		auto Target = FPaths::FromUtf8(AssignedPhysicsAssetPath(*Mesh));
		if (std::filesystem::exists(Target))
			Target.replace_extension("." + FGuid::NewGuid().ToString() + ".physicsasset");
		const std::unique_ptr<UPhysicsAsset> Asset(NewObject<UPhysicsAsset>());
		if (!FPhysicsAssetUtils::CreateFromSkeletalMesh(*Asset, *Mesh, Params) || !Asset->SaveToFile(Target))
		{
			MessageBoxW(GetActiveWindow(), L"Failed to create or save the physics asset.", L"Create Physics Asset", MB_OK | MB_ICONERROR);
			return false;
		}

		const FString TargetPath = FPaths::ToUtf8(Target);
		FString Error;
		if (!Mesh->AssignPhysicsAssetPath(TargetPath, Error) || !Mesh->SaveSkeletalMeshAsset(Error))
		{
			KRS_LOG(Error, "[Physics] Failed to save skeletal mesh assignment: {}", Error);
			MessageBoxW(GetActiveWindow(), L"The physics asset was saved. Open the Skeletal Mesh Editor to complete the assignment.", L"Create Physics Asset", MB_OK | MB_ICONERROR);
			return false;
		}
		if (OnCreated)
			OnCreated(FAssetUtils::GetAssetKey(TargetPath));
		return true;
	}

	// Legacy meshes use their conventional Physics Asset path.
	if (bAssign)
	{
		UPhysicsAsset* Asset = Mesh->GetPhysicsAsset();
		if (!Asset || !FPhysicsAssetUtils::CreateFromSkeletalMesh(*Asset, *Mesh, Params))
		{
			MessageBoxW(GetActiveWindow(), L"Failed to create the physics asset. See Output Log.", L"Create Physics Asset", MB_OK | MB_ICONERROR);
			return false;
		}
		Asset->InvalidatePhysicsData();	// 이 에셋을 쓰는 실행 중 컴포넌트의 바디를 다시 만든다
		if (!Mesh->SavePhysicsAsset())
		{
			MessageBoxW(GetActiveWindow(), L"Failed to save the physics asset.", L"Create Physics Asset", MB_OK | MB_ICONERROR);
			return false;
		}
		KRS_LOG(Info, "[Physics] Physics asset created and assigned: {} ({} bodies, {} constraints)",
		        MeshPath, Asset->SkeletalBodySetups.Num(), Asset->ConstraintSetup.Num());
		if (OnCreated)
			OnCreated(FAssetUtils::GetAssetKey(AssignedPhysicsAssetPath(*Mesh)));
		return true;
	}

	// Create: 저장 위치를 골라 별도 파일로만 만든다.
	std::filesystem::path DefaultName = FPaths::FromUtf8(MeshPath).stem();
	DefaultName += L"_PhysicsAsset.physicsasset";
	wchar_t FilePath[MAX_PATH] = {};
	wcsncpy_s(FilePath, DefaultName.c_str(), _TRUNCATE);
	const std::wstring InitialDirectory = std::filesystem::absolute(CreateDirectory).wstring();

	OPENFILENAMEW Dialog = {};
	Dialog.lStructSize = sizeof(Dialog);
	Dialog.hwndOwner = GetActiveWindow();
	Dialog.lpstrFile = FilePath;
	Dialog.nMaxFile = static_cast<DWORD>(std::size(FilePath));
	Dialog.lpstrFilter = L"Physics Asset Files (*.physicsasset)\0*.physicsasset\0";
	Dialog.lpstrDefExt = L"physicsasset";
	Dialog.lpstrInitialDir = InitialDirectory.c_str();
	Dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT;
	if (!GetSaveFileNameW(&Dialog))
		return false;

	const std::filesystem::path Target(FilePath);
	if (!IsPhysicsAssetFile(Target))
	{
		MessageBoxW(Dialog.hwndOwner, L"Use the .physicsasset extension.", L"Create Physics Asset", MB_OK | MB_ICONERROR);
		return false;
	}

	auto Asset = std::unique_ptr<UPhysicsAsset>(NewObject<UPhysicsAsset>());
	if (!Asset || !FPhysicsAssetUtils::CreateFromSkeletalMesh(*Asset, *Mesh, Params) || !Asset->SaveToFile(Target))
	{
		MessageBoxW(Dialog.hwndOwner, L"Failed to create the physics asset. See Output Log.", L"Create Physics Asset", MB_OK | MB_ICONERROR);
		return false;
	}
	KRS_LOG(Info, "[Physics] Physics asset created: {} ({} bodies, {} constraints)",
	        Target.generic_string(), Asset->SkeletalBodySetups.Num(), Asset->ConstraintSetup.Num());
	if (OnCreated)
		OnCreated(FAssetUtils::GetAssetKey(Target.generic_string()));
	return true;
}
