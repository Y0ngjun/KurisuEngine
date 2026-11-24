#pragma once

#include "GameFramework/Pawn.h"
#include "WheeledVehicle.generated.h"

class UBoxComponent;
class USceneComponent;
class UStaticMeshComponent;
class UWheeledVehicleMovementComponent;
class USpringArmComponent;
class UCameraComponent;

// UE4 AWheeledVehicle 대응 (PhysX 시절). 바퀴 달린 차량 Pawn의 뼈대.
//
// 구성 (UE4는 스켈레탈 메시 하나가 섀시이자 모양이고 바퀴는 본이다. 지금은 아래처럼 나눈다):
//   Chassis (박스 충돌, 루트): 물리 섀시. 스케일이 1이라 바퀴 자세를 그대로 쓸 수 있다.
//   Mesh + Cabin, Roof, Grille, Headlight_*, Taillight_*, Bumper_* (스태틱 메시): 상자로 만든 빨간 시각용 차체. 충돌 없음.
//   WheelRoot_* + Wheel_* : 바퀴 4개. Root가 물리 결과의 자세를 따르고, 검은 타이어 메시가 그 아래에서 눕고 크기를 맞춘다.
//     WheelHub_*, WheelSpoke_* 는 타이어의 자식이라 크기와 구르는 회전을 같이 따른다.
//   SpringArm + Camera: 추적 카메라
// 움직임은 UWheeledVehicleMovementComponent가 맡는다.
//
// 엔진은 메커니즘만 준다. 조작 키, HUD, 차 종류(프리셋) 같은 게임 규칙은 여기 없고
// 게임 쪽 Lua 스크립트(Content/Scripts/Vehicle)가 SetThrottle 같은 Lua API로 정한다.
UCLASS()
class AWheeledVehicle : public APawn
{
	GENERATED_BODY()
public:
	AWheeledVehicle();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	// UE4 AWheeledVehicle::GetMesh / GetVehicleMovement. GetMesh는 시각용 차체다.
	UStaticMeshComponent* GetMesh() const { return Mesh; }
	UBoxComponent* GetChassis() const { return Chassis; }
	UWheeledVehicleMovementComponent* GetVehicleMovementComponent() const { return VehicleMovement; }

	virtual FVector GetVelocity() const override;

	// 차체 도색(아래 차체와 지붕)의 색을 바꾼다. 유리, 라이트, 바퀴는 그대로다.
	void SetBodyColor(const FLinearColor& Color);

	static constexpr int32 NumWheels = 4;

private:
	// 설정(WheelSetups)의 바퀴 위치와 크기를 화면 바퀴에 맞춘다. 물리 결과가 오기 전의 정지 자세다.
	void ApplyWheelRestPose();
	// 물리 결과(바퀴 상태)의 자세를 화면 바퀴에 적용한다.
	void UpdateWheelVisuals();

	// 화면 바퀴를 맞춘 시점의 설정 번호. 설정이 통째로 바뀌면 달라져서 다시 맞춘다.
	uint32 AppliedSetupRevision = 0;

	UBoxComponent* Chassis = nullptr;
	UStaticMeshComponent* Mesh = nullptr;
	UStaticMeshComponent* Roof = nullptr;
	UWheeledVehicleMovementComponent* VehicleMovement = nullptr;
	// 바퀴 4개: 앞왼쪽, 앞오른쪽, 뒤왼쪽, 뒤오른쪽
	USceneComponent* WheelRoots[NumWheels] = {};
	UStaticMeshComponent* WheelMeshes[NumWheels] = {};
	// 추적 카메라 (UE 차량 템플릿의 SpringArm + Camera 구성)
	USpringArmComponent* SpringArm = nullptr;
	UCameraComponent* Camera = nullptr;
};
