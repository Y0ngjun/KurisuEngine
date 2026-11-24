#include "EnginePCH.h"
#include "ParticleSystemActor.h"
#include "Asset/AssetManager.h"

AParticleSystemActor::AParticleSystemActor()
{
	ParticleSystemComponent = CreateDefaultSubobject<UParticleSystemComponent>("UParticleSystemComponent");
	SetRootComponent(ParticleSystemComponent);

	// 엔진 기본 파티클 (EngineResources/Particles). 프로젝트 Content에 의존하지 않는다
	UParticleSystem* DefaultTemplate = UAssetManager::LoadParticleSystem("Engine/Particles/DefaultParticle.particle");
	ParticleSystemComponent->InitializeSystem(DefaultTemplate);

	BillboardComponent = CreateDefaultSubobject<UBillboardComponent>("UBillboardComponent");
	BillboardComponent->SetEditorIcon(true);
	BillboardComponent->SetIconSize(200.0f);
	BillboardComponent->SetHiddenInGame(true);
	BillboardComponent->SetupAttachment(ParticleSystemComponent);
	BillboardComponent->SetHiddenInDetails(true);

	UMaterial* Base = UAssetManager::GetAssetByPath<UMaterial>("BillboardMaterial");
	UTexture2D* Icon = UAssetManager::Get().LoadTexture("Engine/Icons/Emitter.png");
	if (Base && Icon)
	{
		UMaterial* IconMat = UMaterial::CreateInstance(Base);
		IconMat->Textures[0] = Icon;
		BillboardComponent->SetMaterial(0, IconMat);
	}
}
