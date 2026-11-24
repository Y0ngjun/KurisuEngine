#include "EnginePCH.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"

#include "../Components/ActorComponent.h"
#include "Asset/AssetManager.h"
#include "Components/BillboardComponent.h"
#include "Serialization/StructuredArchive.h"

// Unreal의 MatineeCam_SM을 OBJ로 내보낸 메시를 카메라의 로컬 +X 방향에 붙인다.
ACameraActor::ACameraActor()
{
	CameraComponent = CreateDefaultSubobject<UCameraComponent>("CameraComponent");
	SetRootComponent(CameraComponent);

	// 월드의 메인 카메라도 이 클래스지만 RegisterAllComponents를 거치지 않아 씬에 그려지지 않는다.
	CameraMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("CameraMeshComponent");
	CameraMeshComponent->SetEditorIcon(true);
	CameraMeshComponent->SetHiddenInGame(true);
	CameraMeshComponent->SetupAttachment(CameraComponent);
	CameraMeshComponent->SetHiddenInDetails(true);
	CameraMeshComponent->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Engine/Models/Camera/MatineeCam_SM.obj"));
	CameraMeshComponent->SetCastShadow(false);
	CameraMeshComponent->SetReceiveShadow(false);
	CameraMeshComponent->bReceivesDecals = false;
	CameraMeshComponent->bGenerateOverlapEvents = false;
}

void ACameraActor::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	if (!Record.GetUnderlyingArchive().IsLoading())
		return;

	// 기존 씬에 저장된 기본 카메라 빌보드는 새 메시와 겹치지 않도록 제거한다.
	TArray<UActorComponent*> LoadedComponents = GetComponents();
	for (UActorComponent* Component : LoadedComponents)
	{
		UBillboardComponent* Billboard = Cast<UBillboardComponent>(Component);
		if (!Billboard || Billboard->GetAttachParent() != CameraComponent)
			continue;
		UMaterial* Material = Billboard->GetMaterial(0);
		const bool bCameraIcon = Material && !Material->Textures.IsEmpty() && Material->Textures[0] &&
		                         Material->Textures[0]->GetPath().ends_with("Icons/Camera.png");
		if (Component->GetDefaultSubobjectName() == FName("UBillboardComponent") || bCameraIcon)
			DestroyComponent(Billboard);
	}
}
