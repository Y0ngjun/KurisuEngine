#include "EnginePCH.h"
#include "GameFramework/DefaultPawn.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Asset/AssetManager.h"

ADefaultPawn::ADefaultPawn()
{
	SetActorEnableCollision(false);
	CameraComponent = CreateDefaultSubobject<UCameraComponent>("CameraComponent");
	StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("StaticMeshComponent");
	SetRootComponent(StaticMeshComponent);
	StaticMeshComponent->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Sphere"));
	// UE의 DefaultPawn처럼 게임 화면(PIE 카메라)에서는 구를 숨긴다.
	// 에디터 뷰는 게임 뷰가 아니므로 F8로 빠져나오면(SIE) 구가 보인다.
	StaticMeshComponent->SetHiddenInGame(true);
	CameraComponent->SetupAttachment(StaticMeshComponent);
	// 자유 비행 시 시점 자체가 회전 중심이 되도록 카메라를 Pawn 원점에 둔다.
	CameraComponent->SetRelativeLocation(FVector::ZeroVector);
	CameraComponent->SetRelativeRotation(FRotator::Identitiy);
	CameraComponent->SetExternalInputManaged(true);
}

void ADefaultPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const FVector Input = ConsumeMovementInputVector();
	if (GetRootComponent() && Input.SizeSquared() > 0.0f)
		GetRootComponent()->SetRelativeLocation(GetActorLocation() + Input * (MoveSpeed * DeltaTime));
}

void ADefaultPawn::AddControllerYawInput(float Value)
{
	if (!GetRootComponent())
		return;
	FRotator Rotation = GetActorRotation();
	Rotation.Yaw += Value;
	GetRootComponent()->SetRelativeRotation(Rotation);
}

void ADefaultPawn::AddControllerPitchInput(float Value)
{
	if (!GetRootComponent())
		return;
	FRotator Rotation = GetActorRotation();
	Rotation.Pitch = FMath::Clamp(Rotation.Pitch + Value, -89.0f, 89.0f);
	GetRootComponent()->SetRelativeRotation(Rotation);
}
