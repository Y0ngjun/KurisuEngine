#include "EnginePCH.h"
#include "Components/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Character.h"

ACharacter::ACharacter()
{
	CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>("CapsuleComponent");
	SetRootComponent(CapsuleComponent);
	// UE와 같은 Pawn 프리셋: Pawn 채널, 쿼리+물리, 대부분 Block하고 Visibility·Camera는 무시한다.
	// 시뮬레이션하지 않는 키네마틱 바디라 이동 중 시뮬레이션 바디를 밀고, Overlap 응답인 도형과는 Overlap 이벤트가 난다.
	CapsuleComponent->SetCollisionProfileName(FName("Pawn"));

	SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>("SkeletalMeshComponent");
	SkeletalMeshComponent->SetupAttachment(CapsuleComponent);

	CharacterMovementComponent = CreateDefaultSubobject<UCharacterMovementComponent>("CharacterMovementComponent");
}

void ACharacter::AddControllerYawInput(float Value)
{
	if (!GetRootComponent())
		return;
	FRotator Rotation = GetActorRotation();
	Rotation.Yaw += Value;
	GetRootComponent()->SetRelativeRotation(Rotation);
}

void ACharacter::AddControllerPitchInput(float Value)
{
	if (!GetRootComponent())
		return;
	FRotator Rotation = GetActorRotation();
	Rotation.Pitch = FMath::Clamp(Rotation.Pitch + Value, -89.0f, 89.0f);
	GetRootComponent()->SetRelativeRotation(Rotation);
}

FVector ACharacter::GetVelocity() const
{
	return CharacterMovementComponent->Velocity;
}

void ACharacter::Jump()
{
	if (CanJump())
		CharacterMovementComponent->DoJump();
}

bool ACharacter::CanJump() const
{
	return CharacterMovementComponent && CharacterMovementComponent->IsMovingOnGround();
}

bool ACharacter::IsFalling() const
{
	return CharacterMovementComponent && CharacterMovementComponent->IsFalling();
}
