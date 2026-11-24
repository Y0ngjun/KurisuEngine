#pragma once

#include "../GameFramework/Actor.h"
#include "UObject/Class.h"

#include "CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CameraActor.generated.h"

// class UCameraComponent;

UCLASS()
class ACameraActor : public AActor
{
	GENERATED_BODY()

public:
	// 카메라와 에디터 전용 원본 Unreal 카메라 메시를 생성한다.
	ACameraActor();

	UCameraComponent* GetCameraComponent() const { return CameraComponent; };
	UStaticMeshComponent* GetCameraMeshComponent() const { return CameraMeshComponent; }
	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

private:
	UCameraComponent* CameraComponent;
	UStaticMeshComponent* CameraMeshComponent = nullptr;
};
