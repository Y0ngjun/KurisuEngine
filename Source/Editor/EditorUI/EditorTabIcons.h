#pragma once

#include <imgui.h>
#include <string_view>
#include <unordered_map>
#include <string>
#include "Asset/AssetManager.h"
#include "Render/Texture2D.h"

namespace EditorTabIcons
{
	inline ImTextureID GetTabIcon(const char* Label, void* /*UserData*/)
	{
		if (!Label)
			return (ImTextureID)0;

		const char* LabelEnd = ImGui::FindRenderedTextEnd(Label);
		const std::string_view Name(Label, LabelEnd - Label);

		const char* IconAssetPath = nullptr;
		if (Name == "Viewports")
			IconAssetPath = "Engine/Icons/Tab/Tab_Viewports.png";
		else if (Name == "Outliner")
			IconAssetPath = "Engine/Icons/Tab/Tab_Outliner.png";
		else if (Name == "Details")
			IconAssetPath = "Engine/Icons/Tab/Tab_Details.png";
		else if (std::string_view(Label).ends_with("###LevelEditor"))
			IconAssetPath = "Engine/Icons/Scene.png";

		if (!IconAssetPath)
			return (ImTextureID)0;

		static std::unordered_map<std::string, ImTextureID> Cache;
		auto It = Cache.find(IconAssetPath);
		if (It != Cache.end() && It->second != (ImTextureID)0)
			return It->second;

		if (UTexture2D* Texture = UAssetManager::GetAssetByPath<UTexture2D>(IconAssetPath))
		{
			if (Texture->GetResource() && Texture->GetResource()->GetSRV())
			{
				ImTextureID SRV = (ImTextureID)Texture->GetResource()->GetSRV();
				Cache[IconAssetPath] = SRV;
				return SRV;
			}
		}

		return (ImTextureID)0;
	}
}
