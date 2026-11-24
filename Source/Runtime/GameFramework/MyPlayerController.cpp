#include "EnginePCH.h"
#include "GameFramework/MyPlayerController.h"
#include "GameFramework/Actor/MyCharacter.h"
#include "GameFramework/WheeledVehicle.h"
#include "Components/WheeledVehicleMovementComponent.h"

void AMyPlayerController::InputKey(int32 Key, bool bDown)
{
	// Super가 bKeyDown을 갱신하므로, 눌림 순간 판정은 호출 전에 이전 상태를 잡아 둔다
	const bool bJustPressed = bDown && !IsKeyDown(Key);
	const bool bJustReleased = !bDown && IsKeyDown(Key);
	Super::InputKey(Key, bDown);

	// 차량: Space 핸드브레이크 (누르는 동안). WASD는 AddMovementInput으로 이동 컴포넌트가 받는다.
	if (Key == static_cast<int32>(EKeyCode::Space) && (bJustPressed || bJustReleased))
	{
		if (AWheeledVehicle* Vehicle = Cast<AWheeledVehicle>(GetPawn()))
		{
			if (UWheeledVehicleMovementComponent* VehicleMovement = Vehicle->GetVehicleMovementComponent())
				VehicleMovement->SetHandbrakeInput(bDown);
		}
	}

	if (!bJustPressed)
		return;

	if (Key == static_cast<int32>(EKeyCode::LButton))
	{
		if (AMyCharacter* MyCharacter = Cast<AMyCharacter>(GetPawn()))
		{
			MyCharacter->Attack();
		}
	}

	if (Key == static_cast<int32>(EKeyCode::Space))
	{
		// UE 템플릿처럼 모든 ACharacter가 점프한다. Jump는 virtual이라 AMyCharacter의 override(애님 IsJumping)도 그대로 탄다.
		if (ACharacter* Character = Cast<ACharacter>(GetPawn()))
		{
			Character->Jump();
		}
	}

	// 차량: R 차 되돌리기 (뒤집혔을 때)
	if (Key == static_cast<int32>(EKeyCode::R))
	{
		if (AWheeledVehicle* Vehicle = Cast<AWheeledVehicle>(GetPawn()))
		{
			if (UWheeledVehicleMovementComponent* VehicleMovement = Vehicle->GetVehicleMovementComponent())
				VehicleMovement->ResetVehicle();
		}
	}
}
