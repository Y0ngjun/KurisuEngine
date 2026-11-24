#pragma once

#include "GameFramework/Actor.h"
#include "Input/InputSystem.h"
#include "PlayerController.generated.h"

class APawn;
class APlayerCameraManager;
class UCurveFloat;

UCLASS()
class APlayerController : public AActor
{
	GENERATED_BODY()
public:
	APlayerController();
	void Tick(float DeltaTime) override;
	void ProcessPlayerInput(float DeltaTime);
	void Possess(APawn* InPawn);
	void UnPossess();
	APawn* GetPawn() const { return PossessedPawn; }
	virtual void InputKey(int32 Key, bool bDown);
	bool IsKeyDown(int32 Key) const { return Key >= 0 && Key < 256 && bKeyDown[Key]; }
	void InputAxis(EGameInputAxis Axis, float Delta);
	void ClearInputState();

	// UE APlayerController와 같다: true를 넘길 때마다 쌓이고 false로 하나씩 푼다. 쌓여 있으면 WASD 이동 입력을 Pawn에 넘기지 않는다.
	// (카운트다운처럼 게임 규칙이 잠시 조작을 막을 때 쓴다. 키 상태 자체는 계속 갱신된다.)
	void SetIgnoreMoveInput(bool bNewMoveInput);
	void ResetIgnoreMoveInput() { IgnoreMoveInput = 0; }
	bool IsMoveInputIgnored() const { return IgnoreMoveInput > 0; }

	FRotator ControlRotation;
	// PCM을 만들고 주인을 알려준다.
	void SpawnPlayerCameraManager();
	// Wolrd Tick으로부터 전달
	void UpdateCameraManager(float DeltaTime);
	APlayerCameraManager* GetCameraManager() const { return PlayerCameraManager; }

private:
	APlayerCameraManager* PlayerCameraManager = nullptr;
	APawn* PossessedPawn = nullptr;
	bool bKeyDown[256]{};
	float MouseAxisX = 0.0f;
	float MouseAxisY = 0.0f;
	float LookSensitivity = 0.1f;
	uint8 IgnoreMoveInput = 0;
};
