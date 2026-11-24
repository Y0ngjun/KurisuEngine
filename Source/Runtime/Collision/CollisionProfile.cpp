#include "EnginePCH.h"
#include "Collision/CollisionProfile.h"
#include "Physics/BodyInstance.h"

namespace
{
	FCollisionResponseTemplate Make(const char* Name, ECollisionEnabled Enabled, ECollisionChannel ObjectType, ECollisionResponse All, const char* Help,
	                                std::initializer_list<std::pair<ECollisionChannel, ECollisionResponse>> Exceptions = {})
	{
		FCollisionResponseTemplate Template;
		Template.Name = FName(Name);
		Template.CollisionEnabled = Enabled;
		Template.ObjectType = ObjectType;
		Template.ResponseToChannels.SetAllChannels(All);
		for (const auto& [Channel, Response] : Exceptions)
			Template.ResponseToChannels.SetResponse(Channel, Response);
		Template.HelpMessage = Help;
		return Template;
	}

	TArray<FCollisionResponseTemplate> BuildProfiles()
	{
		using E = ECollisionEnabled;
		TArray<FCollisionResponseTemplate> Profiles;
		// UE BaseEngine.ini의 기본 프리셋과 같은 이름·의도
		Profiles.Add(Make("NoCollision", E::NoCollision, ECC_WorldStatic, ECR_Ignore, "충돌 없음"));
		Profiles.Add(Make("BlockAll", E::QueryAndPhysics, ECC_WorldStatic, ECR_Block, "모두 막는 정적 물체 (바닥·벽)"));
		Profiles.Add(Make("OverlapAll", E::QueryOnly, ECC_WorldStatic, ECR_Overlap, "모두와 겹치기만 하는 정적 물체"));
		Profiles.Add(Make("BlockAllDynamic", E::QueryAndPhysics, ECC_WorldDynamic, ECR_Block, "모두 막는 움직이는 물체"));
		Profiles.Add(Make("OverlapAllDynamic", E::QueryOnly, ECC_WorldDynamic, ECR_Overlap, "모두와 겹치기만 하는 움직이는 물체 (도형 기본값)"));
		Profiles.Add(Make("IgnoreOnlyPawn", E::QueryAndPhysics, ECC_WorldDynamic, ECR_Block, "Pawn·Vehicle만 통과",
		                  {{ECC_Pawn, ECR_Ignore}, {ECC_Vehicle, ECR_Ignore}}));
		Profiles.Add(Make("OverlapOnlyPawn", E::QueryAndPhysics, ECC_WorldDynamic, ECR_Ignore, "Pawn·Camera·Vehicle과만 겹침",
		                  {{ECC_Pawn, ECR_Overlap}, {ECC_Camera, ECR_Overlap}, {ECC_Vehicle, ECR_Overlap}}));
		Profiles.Add(Make("Pawn", E::QueryAndPhysics, ECC_Pawn, ECR_Block, "캐릭터 캡슐 (Visibility·Camera 무시)",
		                  {{ECC_Visibility, ECR_Ignore}, {ECC_Camera, ECR_Ignore}}));
		Profiles.Add(Make("Spectator", E::QueryOnly, ECC_Pawn, ECR_Ignore, "WorldStatic만 막는 관전자", {{ECC_WorldStatic, ECR_Block}}));
		Profiles.Add(Make("CharacterMesh", E::QueryOnly, ECC_Pawn, ECR_Block, "캐릭터 메시 (쿼리만, Pawn·Vehicle 무시)",
		                  {{ECC_Pawn, ECR_Ignore}, {ECC_Vehicle, ECR_Ignore}}));
		Profiles.Add(Make("PhysicsActor", E::QueryAndPhysics, ECC_PhysicsBody, ECR_Block, "시뮬레이션하는 물체"));
		Profiles.Add(Make("Destructible", E::QueryAndPhysics, ECC_Destructible, ECR_Block, "부서지는 물체"));
		Profiles.Add(Make("InvisibleWall", E::QueryAndPhysics, ECC_WorldStatic, ECR_Block, "보이지 않는 정적 벽 (Visibility 무시)",
		                  {{ECC_Visibility, ECR_Ignore}}));
		Profiles.Add(Make("InvisibleWallDynamic", E::QueryAndPhysics, ECC_WorldDynamic, ECR_Block, "보이지 않는 움직이는 벽 (Visibility 무시)",
		                  {{ECC_Visibility, ECR_Ignore}}));
		Profiles.Add(Make("Trigger", E::QueryOnly, ECC_WorldDynamic, ECR_Overlap, "트리거 볼륨 (Visibility 무시)", {{ECC_Visibility, ECR_Ignore}}));
		Profiles.Add(Make("Ragdoll", E::QueryAndPhysics, ECC_PhysicsBody, ECR_Block, "래그돌 (Pawn·Visibility 무시)",
		                  {{ECC_Pawn, ECR_Ignore}, {ECC_Visibility, ECR_Ignore}}));
		Profiles.Add(Make("Vehicle", E::QueryAndPhysics, ECC_Vehicle, ECR_Block, "자동차"));
		Profiles.Add(Make("UI", E::QueryOnly, ECC_WorldDynamic, ECR_Overlap, "UI 요소 (Visibility만 막음)", {{ECC_Visibility, ECR_Block}}));
		return Profiles;
	}
}

const FName& UCollisionProfile::CustomCollisionProfileName()
{
	static const FName Custom("Custom");
	return Custom;
}

const TArray<FCollisionResponseTemplate>& UCollisionProfile::GetProfiles()
{
	static const TArray<FCollisionResponseTemplate> Profiles = BuildProfiles();
	return Profiles;
}

const FCollisionResponseTemplate* UCollisionProfile::FindProfile(const FName& ProfileName)
{
	for (const FCollisionResponseTemplate& Template : GetProfiles())
		if (Template.Name == ProfileName)
			return &Template;
	return nullptr;
}

const char* UCollisionProfile::GetChannelDisplayName(ECollisionChannel Channel)
{
	switch (Channel)
	{
	case ECC_WorldStatic: return "WorldStatic";
	case ECC_WorldDynamic: return "WorldDynamic";
	case ECC_Pawn: return "Pawn";
	case ECC_Visibility: return "Visibility";
	case ECC_Camera: return "Camera";
	case ECC_PhysicsBody: return "PhysicsBody";
	case ECC_Vehicle: return "Vehicle";
	case ECC_Destructible: return "Destructible";
	case ECC_GameTraceChannel1: return "GameTrace1";
	case ECC_GameTraceChannel2: return "GameTrace2";
	case ECC_GameTraceChannel3: return "GameTrace3";
	case ECC_GameTraceChannel4: return "GameTrace4";
	default: return "?";
	}
}
