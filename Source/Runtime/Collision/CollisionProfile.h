#pragma once

#include "Collision/CollisionTypes.h"
#include "Core/NameTypes.h"

enum class ECollisionEnabled : uint8;

// UE FCollisionResponseTemplate: 이름 붙은 충돌 설정 묶음 (Project Settings > Collision > Preset 한 줄)
struct FCollisionResponseTemplate
{
	FName Name;
	ECollisionEnabled CollisionEnabled;
	ECollisionChannel ObjectType = ECC_WorldStatic;
	FCollisionResponseContainer ResponseToChannels;
	FString HelpMessage;
};

// UE UCollisionProfile. 엔진 기본 프리셋 목록. 바디는 프리셋 이름(CollisionProfileName)을 저장하고,
// 그 이름이 Custom이 아니면 활성화·Object Type·채널 응답을 프리셋에서 가져온다.
namespace UCollisionProfile
{
	// 프리셋을 쓰지 않고 값을 직접 정한 상태 (UE CustomCollisionProfileName)
	const FName& CustomCollisionProfileName();
	const TArray<FCollisionResponseTemplate>& GetProfiles();
	const FCollisionResponseTemplate* FindProfile(const FName& ProfileName);
	// 편집 UI에 보이는 채널 이름
	const char* GetChannelDisplayName(ECollisionChannel Channel);
}
