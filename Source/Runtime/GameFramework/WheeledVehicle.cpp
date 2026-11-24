#include "EnginePCH.h"
#include "GameFramework/WheeledVehicle.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WheeledVehicleMovementComponent.h"
#include "Asset/AssetManager.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Render/Material.h"


namespace
{
	// 엔진의 Cylinder 메시는 반지름 100cm, 높이 100cm이고 축이 Z다. 바퀴 축(Y)에 맞추려면 눕혀야 한다.
	constexpr float CylinderMeshRadius = 100.0f;
	constexpr float CylinderMeshHeight = 100.0f;

	const char* const WheelRootNames[] = {"WheelRoot_FL", "WheelRoot_FR", "WheelRoot_RL", "WheelRoot_RR"};
	const char* const WheelMeshNames[] = {"Wheel_FL", "Wheel_FR", "Wheel_RL", "Wheel_RR"};
	const char* const WheelHubNames[] = {"WheelHub_FL", "WheelHub_FR", "WheelHub_RL", "WheelHub_RR"};
	const char* const WheelSpokeNames[] = {"WheelSpoke_FL", "WheelSpoke_FR", "WheelSpoke_RL", "WheelSpoke_RR"};

	// 기본 재질의 인스턴스를 만들어 색만 바꾼다 (Lua SetColor와 같은 방식). 기본 재질이 아직 없으면 nullptr = 메시 기본값.
	UMaterial* MakeColorMaterial(float R, float G, float B, float Shininess = 32.0f)
	{
		UMaterial* Material = UMaterial::CreateInstance(UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial"));
		if (Material)
		{
			Material->BaseColor = FVector4(R, G, B, 1.0f);
			Material->Shininess = Shininess;
		}
		return Material;
	}

	// 시각용 부품 하나: Cube(100cm)를 Size(cm) 크기로 늘려 Center(섀시 로컬, cm)에 둔다. 충돌은 없다.
	UStaticMeshComponent* SetupBodyPart(UStaticMeshComponent* Part, USceneComponent* Parent, const FVector& Center, const FVector& Size, UMaterial* Material)
	{
		Part->SetupAttachment(Parent);
		Part->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Cube"));
		Part->SetRelativeLocation(Center);
		Part->SetRelativeScale3D(Size / 100.0f);
		Part->SetCollisionProfileName(FName("NoCollision"));
		Part->SetMaterial(0, Material);
		return Part;
	}
}

void AWheeledVehicle::SetBodyColor(const FLinearColor& Color)
{
	UMaterial* PaintMaterial = MakeColorMaterial(Color.R, Color.G, Color.B, 64.0f);
	for (UStaticMeshComponent* Part : {Mesh, Roof})
		if (Part)
			Part->SetMaterial(0, PaintMaterial);
}

AWheeledVehicle::AWheeledVehicle()
{
	// 섀시: 박스 충돌이 루트다. 반 크기 (225, 90, 40)cm = 길이 450, 폭 180, 높이 80.
	// 움직임 컴포넌트는 루트를 UpdatedComponent로 잡고, 루트의 스케일이 1이라 바퀴 자세를 그대로 쓸 수 있다.
	Chassis = CreateDefaultSubobject<UBoxComponent>("Chassis");
	SetRootComponent(Chassis);
	Chassis->BoxExtent = FVector(225.0f, 90.0f, 40.0f);
	// UE의 Vehicle 프리셋(Vehicle 채널). 시뮬레이션하는 바디라 땅과 충돌한다.
	Chassis->SetCollisionProfileName(FName("Vehicle"));
	Chassis->SetSimulatePhysics(true);
	// UShapeComponent는 겹침 감지용 기본값이라 중력이 꺼져 있다. 차 섀시는 떨어져야 하므로 켠다.
	Chassis->SetEnableGravity(true);

	// 시각용 차체 (충돌 없음). 상자 여러 개로 빨간 승용차 모양을 만든다. 좌표는 섀시 로컬 cm, +X가 앞이다.
	// 섀시 충돌 상자(450 x 180 x 80, 중심 0)와 같은 길이를 쓰고, 폭은 조금 좁혀 바퀴가 옆으로 드러나게 한다.
	UMaterial* PaintMaterial = MakeColorMaterial(0.78f, 0.05f, 0.04f, 64.0f);
	UMaterial* GlassMaterial = MakeColorMaterial(0.07f, 0.09f, 0.13f, 128.0f);
	UMaterial* TrimMaterial = MakeColorMaterial(0.08f, 0.08f, 0.09f, 8.0f);
	UMaterial* HeadlightMaterial = MakeColorMaterial(1.0f, 0.95f, 0.75f, 128.0f);
	UMaterial* TaillightMaterial = MakeColorMaterial(1.0f, 0.12f, 0.08f, 64.0f);
	UMaterial* TireMaterial = MakeColorMaterial(0.04f, 0.04f, 0.04f, 4.0f);
	UMaterial* HubMaterial = MakeColorMaterial(0.72f, 0.73f, 0.76f, 96.0f);
	UMaterial* SpokeMaterial = MakeColorMaterial(0.25f, 0.26f, 0.28f, 32.0f);

	// 아래 차체 (GetMesh가 돌려주는 메시)
	Mesh = SetupBodyPart(CreateDefaultSubobject<UStaticMeshComponent>("Mesh"), Chassis,
	                     FVector(0.0f, 0.0f, -20.0f), FVector(450.0f, 170.0f, 45.0f), PaintMaterial);
	// 유리 캐빈과 지붕. 캐빈은 가운데보다 조금 뒤에 둔다.
	SetupBodyPart(CreateDefaultSubobject<UStaticMeshComponent>("Cabin"), Chassis,
	              FVector(-20.0f, 0.0f, 22.5f), FVector(220.0f, 150.0f, 40.0f), GlassMaterial);
	Roof = SetupBodyPart(CreateDefaultSubobject<UStaticMeshComponent>("Roof"), Chassis,
	              FVector(-25.0f, 0.0f, 46.5f), FVector(200.0f, 156.0f, 8.0f), PaintMaterial);
	// 앞: 그릴, 헤드라이트 / 뒤: 테일라이트 / 앞뒤 범퍼
	SetupBodyPart(CreateDefaultSubobject<UStaticMeshComponent>("Grille"), Chassis,
	              FVector(226.0f, 0.0f, -22.0f), FVector(4.0f, 70.0f, 12.0f), TrimMaterial);
	SetupBodyPart(CreateDefaultSubobject<UStaticMeshComponent>("Headlight_L"), Chassis,
	              FVector(226.0f, -55.0f, -6.0f), FVector(4.0f, 30.0f, 12.0f), HeadlightMaterial);
	SetupBodyPart(CreateDefaultSubobject<UStaticMeshComponent>("Headlight_R"), Chassis,
	              FVector(226.0f, 55.0f, -6.0f), FVector(4.0f, 30.0f, 12.0f), HeadlightMaterial);
	SetupBodyPart(CreateDefaultSubobject<UStaticMeshComponent>("Taillight_L"), Chassis,
	              FVector(-226.0f, -55.0f, -6.0f), FVector(4.0f, 30.0f, 10.0f), TaillightMaterial);
	SetupBodyPart(CreateDefaultSubobject<UStaticMeshComponent>("Taillight_R"), Chassis,
	              FVector(-226.0f, 55.0f, -6.0f), FVector(4.0f, 30.0f, 10.0f), TaillightMaterial);
	SetupBodyPart(CreateDefaultSubobject<UStaticMeshComponent>("Bumper_F"), Chassis,
	              FVector(228.0f, 0.0f, -38.0f), FVector(12.0f, 172.0f, 10.0f), TrimMaterial);
	SetupBodyPart(CreateDefaultSubobject<UStaticMeshComponent>("Bumper_R"), Chassis,
	              FVector(-228.0f, 0.0f, -38.0f), FVector(12.0f, 172.0f, 10.0f), TrimMaterial);

	VehicleMovement = CreateDefaultSubobject<UWheeledVehicleMovementComponent>("VehicleMovement");

	// 바퀴 4개. Root는 물리 결과의 자세(서스펜션 이동, 조향, 구르는 회전)를 받는다.
	// 그 아래 메시는 원통을 바퀴 축(Y)으로 눕히고 반지름과 폭에 맞게 키운다. 눕히는 일을 계층이 맡게 해서
	// 회전 합성 순서를 직접 계산하지 않는다.
	for (int32 Index = 0; Index < NumWheels; ++Index)
	{
		WheelRoots[Index] = CreateDefaultSubobject<USceneComponent>(WheelRootNames[Index]);
		WheelRoots[Index]->SetupAttachment(Chassis);

		WheelMeshes[Index] = CreateDefaultSubobject<UStaticMeshComponent>(WheelMeshNames[Index]);
		WheelMeshes[Index]->SetupAttachment(WheelRoots[Index]);
		WheelMeshes[Index]->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Cylinder"));
		WheelMeshes[Index]->SetRelativeRotation(FRotator(0.0f, 0.0f, 90.0f));
		WheelMeshes[Index]->SetCollisionProfileName(FName("NoCollision"));
		WheelMeshes[Index]->SetMaterial(0, TireMaterial);

		// 휠 허브와 스포크는 타이어 메시의 자식이라 크기(반지름, 폭)와 구르는 회전을 그대로 따라간다.
		// 타이어 로컬 단위(반지름 100, 폭 100, 축 Z)로 둔다. 양옆으로 살짝 튀어나오게 해서 바깥에서 보이게 한다.
		UStaticMeshComponent* Hub = CreateDefaultSubobject<UStaticMeshComponent>(WheelHubNames[Index]);
		Hub->SetupAttachment(WheelMeshes[Index]);
		Hub->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Cylinder"));
		Hub->SetRelativeScale3D(FVector(0.6f, 0.6f, 1.08f));
		Hub->SetCollisionProfileName(FName("NoCollision"));
		Hub->SetMaterial(0, HubMaterial);
		// 바퀴가 도는 게 보이도록 허브를 가로지르는 막대
		SetupBodyPart(CreateDefaultSubobject<UStaticMeshComponent>(WheelSpokeNames[Index]), WheelMeshes[Index],
		              FVector::ZeroVector, FVector(160.0f, 22.0f, 112.0f), SpokeMaterial);
	}

	// 추적 카메라. 스프링 암은 월드 좌표로 계산하고 자기 액터는 충돌에서 무시한다.
	// 암은 섀시의 Yaw만 따라가고(기울어져도 시점은 수평) 살짝 내려다본다.
	SpringArm = CreateDefaultSubobject<USpringArmComponent>("SpringArm");
	SpringArm->SetupAttachment(Chassis);
	// 뒤쪽 위에서 비스듬히 내려다본다. 차는 화면 아래쪽에, 앞 도로는 화면 가운데에 온다.
	// 이 엔진의 Pitch는 +가 아래를 본다 (FRotator::LookDirection: Pitch = atan2(-Z, 수평)). UE와 부호가 반대다.
	SpringArm->TargetArmLength = 700.0f;
	SpringArm->TargetOffset = FVector(0.0f, 0.0f, 120.0f);
	SpringArm->SetRelativeRotation(FRotator(22.0f, 0.0f, 0.0f));
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bInheritPitch = false;
	SpringArm->bInheritYaw = true;
	SpringArm->bInheritRoll = false;
	SpringArm->bEnableCameraRotationLag = true;
	SpringArm->CameraRotationLagSpeed = 4.0f;
	SpringArm->bDrawDebug = false;

	Camera = CreateDefaultSubobject<UCameraComponent>("Camera");
	Camera->SetupAttachment(SpringArm);
	Camera->SetExternalInputManaged(true);
	Camera->bUsePawnControlRotation = false;

	ApplyWheelRestPose();
}

void AWheeledVehicle::BeginPlay()
{
	Super::BeginPlay();
	// 에디터에서 바꾼 WheelSetups(위치, 반지름, 폭)를 플레이가 시작될 때 화면 바퀴에 반영한다.
	ApplyWheelRestPose();
	AppliedSetupRevision = VehicleMovement ? VehicleMovement->GetSetupRevision() : 0;
}

void AWheeledVehicle::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// 설정이 통째로 바뀌어(NotifySetupChanged) 바퀴 크기와 위치가 달라졌으면 화면 바퀴를 다시 맞춘다.
	if (VehicleMovement && AppliedSetupRevision != VehicleMovement->GetSetupRevision())
	{
		ApplyWheelRestPose();
		AppliedSetupRevision = VehicleMovement->GetSetupRevision();
	}
	UpdateWheelVisuals();
}

void AWheeledVehicle::ApplyWheelRestPose()
{
	if (!VehicleMovement)
		return;
	const TArray<FWheelSetup>& Setups = VehicleMovement->WheelSetups;
	for (int32 Index = 0; Index < NumWheels && Index < Setups.Num(); ++Index)
	{
		const FWheelSetup& Setup = Setups[Index];
		if (WheelRoots[Index])
			WheelRoots[Index]->SetRelativeLocation(Setup.AdditionalOffset);
		if (WheelMeshes[Index])
		{
			// 원통 메시의 반지름과 높이(폭)를 바퀴 설정 크기에 맞춘다.
			const float RadiusScale = Setup.Wheel.ShapeRadius / CylinderMeshRadius;
			const float WidthScale = Setup.Wheel.ShapeWidth / CylinderMeshHeight;
			WheelMeshes[Index]->SetRelativeScale3D(FVector(RadiusScale, RadiusScale, WidthScale));
		}
	}
}

void AWheeledVehicle::UpdateWheelVisuals()
{
	if (!VehicleMovement)
		return;
	const TArray<FVehicleWheelState>& States = VehicleMovement->GetWheelStates();
	// 차량 갱신 결과가 아직 없으면(플레이 시작 직후 등) 정지 자세 그대로 둔다.
	if (States.Num() < NumWheels)
		return;
	for (int32 Index = 0; Index < NumWheels; ++Index)
	{
		if (WheelRoots[Index])
			WheelRoots[Index]->SetTransform(States[Index].LocalPose);
	}
}

FVector AWheeledVehicle::GetVelocity() const
{
	return Chassis ? Chassis->GetPhysicsLinearVelocity() : FVector::ZeroVector;
}
