#include "EnginePCH.h"
#include "Editor/Viewports/ViewportMath.h"
#include "Physics/PhysicsDebugDraw.h"
#include "Editor/Viewports/LevelEditorViewport.h"
#include "Editor/KurisuEd/Editor.h"
#include "Launch/LaunchEngineLoop.h"
#include "Render/Renderer.h"
#include "Render/SceneRenderer.h"
#include "Components/ShapeComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Physics/PhysicsMath.h"
#include "Physics/BodySetup.h"
#include <PxPhysicsAPI.h>
#include "Components/DecalComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/ParticleSystemComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Actor/DecalActor.h"
#include "GameFramework/Actor/PointLightActor.h"
#include "GameFramework/Actor/SpotLightActor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "Asset/AssetManager.h"

FLevelEditorViewportClient* GCurrentLevelEditingViewportClient = nullptr;

namespace
{
	void DrawMeshPhysicsShape(UWorld* World, FLineBatcher* Lines, const UStaticMeshComponent* Mesh)
	{
		if (!Mesh || Mesh->GetWorld() != World || !Mesh->IsRegistered() || Mesh->IsEditorIcon() || !Mesh->GetStaticMesh())
			return;
		const FVector4 Color(0.15f, 1.0f, 0.45f, 1.0f);
		if (auto* Actor = Mesh->GetBodyInstance().GetActor())
		{
			// 플레이 중에는 설정을 근사하지 않고 실제 Shape의 치수와 월드 Pose를 읽는다.
			for (physx::PxU32 Index = 0; Index < Actor->getNbShapes(); ++Index)
			{
				physx::PxShape* Shape = nullptr;
				if (!Actor->getShapes(&Shape, 1, Index) || !Shape)
					continue;
				const auto Pose = Actor->getGlobalPose() * Shape->getLocalPose();
				const physx::PxGeometryHolder Geometry(Shape->getGeometry());
				if (Geometry.getType() == physx::PxGeometryType::eBOX)
				{
					const FVector Extent = PhysicsMath::FromPx(Geometry.box().halfExtents);
					Lines->AddOrientedBox({-Extent, Extent}, PhysicsMath::FromPxPose(Pose).GetLocalMatrix(), Color);
				}
				else if (Geometry.getType() == physx::PxGeometryType::eSPHERE)
					Lines->AddSphere(PhysicsMath::FromPx(Pose.p), Geometry.sphere().radius, Color, 48);
				else if (Geometry.getType() == physx::PxGeometryType::eCAPSULE)
				{
					// PhysX 캡슐은 Pose의 X축 방향으로 뻗는다.
					const float Radius = Geometry.capsule().radius;
					const physx::PxVec3 Axis = Pose.q.rotate(physx::PxVec3(1, 0, 0)) * Geometry.capsule().halfHeight;
					const FVector A = PhysicsMath::FromPx(Pose.p + Axis);
					const FVector B = PhysicsMath::FromPx(Pose.p - Axis);
					Lines->AddSphere(A, Radius, Color, 24);
					Lines->AddSphere(B, Radius, Color, 24);
					const FVector Side1 = PhysicsMath::FromPx(Pose.q.rotate(physx::PxVec3(0, 1, 0))) * Radius;
					const FVector Side2 = PhysicsMath::FromPx(Pose.q.rotate(physx::PxVec3(0, 0, 1))) * Radius;
					for (const FVector& Side : {Side1, Side1 * -1.0f, Side2, Side2 * -1.0f})
						Lines->AddLine(A + Side, B + Side, Color);
				}
				else if (Geometry.getType() == physx::PxGeometryType::eCONVEXMESH && Geometry.convexMesh().convexMesh)
				{
					// 스케일은 정점에 미리 구워 두었으므로 Pose만 적용한다.
					DrawConvexMeshDebug(*Lines, *Geometry.convexMesh().convexMesh, PhysicsMath::FromPxPose(Pose).GetLocalMatrix(), Color);
				}
			}
			return;
		}

		// 편집 중에는 바디가 없으므로 생성에 쓰는 설정으로 미리 본다. 물리를 끈 상태도 설정 가능하다.
		if (UBodySetup* Setup = Mesh->GetBodySetup())
			Setup->DrawDebug(*Lines, Mesh->GetWorldMatrix(), Mesh->GetWorldScale3D(), Color);
	}

	void DrawSelectedMeshPhysics(UWorld* World, FLineBatcher* Lines, const FGizmo* Gizmo, const FOutline* Outline)
	{
		USceneComponent* Target = Gizmo ? Gizmo->GetTarget() : nullptr;
		if (!World || !Lines || !Target || Target->GetWorld() != World)
			return;
		const bool bActorSelected = Target->GetOwner() && Target->GetOwner()->GetRootComponent() == Target;
		if (bActorSelected && Outline && Outline->GetTargets().Num() > 1)
		{
			for (UPrimitiveComponent* Selected : Outline->GetTargets())
				DrawMeshPhysicsShape(World, Lines, Cast<UStaticMeshComponent>(Selected));
		}
		else if (const auto* Mesh = Cast<UStaticMeshComponent>(Target))
			DrawMeshPhysicsShape(World, Lines, Mesh);
		else if (bActorSelected && Outline)
			for (UPrimitiveComponent* Selected : Outline->GetTargets())
				DrawMeshPhysicsShape(World, Lines, Cast<UStaticMeshComponent>(Selected));
	}
}

FLevelEditorViewportClient::FLevelEditorViewportClient()
{
	// 그리드 셰이더와 버퍼를 준비한다.
	DrawHelper.Init(GEditor ? GEditor->GetEngineLoop().GetRenderer() : nullptr);

	// 깊이 시각화와 UUID 글자를 그리는 도구를 준비한다. 두 Init의 렌더러 인자는 쓰이지 않는다.
	SceneDepthRenderer.Init(nullptr);
	CascadeFloorRenderer.Init();
	TextRenderer.Init();
	SystemFont = UAssetManager::GetAssetByPath<UFont>("Engine/Fonts/Pretendard.json");
}

// 사라지는 뷰포트를 전역이 계속 가리키지 않게 한다. UE도 소멸자에서 같은 일을 한다.
FLevelEditorViewportClient::~FLevelEditorViewportClient()
{
	if (GCurrentLevelEditingViewportClient == this)
	{
		GCurrentLevelEditingViewportClient = nullptr;
	}
}

// 이번 프레임의 표시 설정을 채운 뒤 기반 클래스의 Draw로 씬을 그린다.
// 에디터 설정 창의 값과 이 뷰포트의 뷰 모드를 EngineShowFlags에 옮긴다.
// UE의 FLevelEditorViewportClient::Draw(FViewport*, FCanvas*)도 준비를 한 뒤 FEditorViewportClient::Draw를 부른다.
void FLevelEditorViewportClient::Draw(FRenderer* Renderer)
{
	if (const FEditorSettings* EditorSettings = GEditor ? GEditor->GetEditorSettings() : nullptr)
	{
		const FEditorSettings& S = *EditorSettings;

		// 모든 뷰포트에 똑같이 적용되는 에디터 설정
		EngineShowFlags.bEnableFXAA = S.bEnableFXAA;
		EngineShowFlags.bCacheShadows = S.bCacheShadows;
		EngineShowFlags.PcfMode = S.PcfMode;
		EngineShowFlags.LightingModel = S.LightingModel;
		EngineShowFlags.LightCullingMode = S.LightCullingMode;
		EngineShowFlags.DirectionalShadowMapSize = S.GetDirectionalShadowMapSize();
		EngineShowFlags.PointShadowMapSize = S.GetPointShadowMapSize();
		EngineShowFlags.SpotShadowMapSize = S.GetSpotShadowMapSize();
		EngineShowFlags.bGammaCorrection = S.bGammaCorrection;
		EngineShowFlags.DisplayGamma = S.DisplayGamma;
		EngineShowFlags.bShowVignette = S.bShowVignette;
		EngineShowFlags.bShowCameraOverlay = S.bShowCameraOverlay;
		EngineShowFlags.bShowSkeletalMesh = S.bShowSkeletalMesh;
		EngineShowFlags.bShowParticles = S.bShowParticles;
		EngineShowFlags.bDrawPrimitives = S.bDrawPrimitives;
		EngineShowFlags.bDrawDecals = S.bDrawDecals;
		EngineShowFlags.bDecalCulling = S.bDecalCulling;
		EngineShowFlags.bDecalNarrowCulling = S.bDecalNarrowCulling;
		EngineShowFlags.bDecalBVHCulling = S.bDecalBVHCulling;
		EngineShowFlags.bDecalOBBTree = S.bDecalOBBTree;

		// 이 뷰포트의 뷰 모드에서 나오는 설정
		EngineShowFlags.bEnableFog = ViewportMode == EViewportMode::Solid || ViewportMode == EViewportMode::Unlit;
		// Unlit은 Solid와 같은 경로로 그리되 이 뷰포트만 라이팅 모델을 Unlit으로 바꾼다.
		if (ViewportMode == EViewportMode::Unlit)
			EngineShowFlags.LightingModel = ELightingModel::Unlit;
		EngineShowFlags.bShowSceneDepth = IsSceneDepth();
		EngineShowFlags.bShowCascade = IsCascade();
		EngineShowFlags.bShowWorldNormal = IsWorldNormal();
		EngineShowFlags.bShowLightCulling = IsLightCulling();
		EngineShowFlags.SceneDepthRange = MaxRange;
	}

	FEditorViewportClient::Draw(Renderer);
}

// 레벨 에디터 뷰포트의 씬 위에 선, 그리드, 외곽선, 기즈모를 올린다. 씬은 기반 클래스의 Draw가 그린다.
// 여기서는 그릴 시점에 불릴 함수를 등록만 하고, 등록한 함수는 FSceneRenderer::Render 안에서 불린다.
void FLevelEditorViewportClient::Draw(const FSceneView* View, FScenePassCallbacks& Callbacks)
{
	if (!GEditor || !GEditor->GetEditorSettings())
		return;
	const FSceneView& SceneView = *View;

	// 디버그 선은 이 뷰포트가 보는 월드의 LineBatcher로 그린다.
	// 무엇이 선택됐는지(Gizmo, Outline)와 에디터 설정은 에디터 전체가 공유하므로 엔진(GEditor)에서 읽는다.
	const FEditorSettings& Settings = *GEditor->GetEditorSettings();
	FLineBatcher* LineBatcher = GetWorld()->LineBatcher.get();
	FGizmo* Gizmo = GEditor->GetGizmo();
	FOutline* Outline = GEditor->GetOutline();

	// 뷰 모드가 Scene Depth, Cascade일 때 깊이를 색으로 바꿔 보여 준다.
	FTexture2D* ColorTarget = ViewTargets.SceneColor.get();
	FTexture2D* DepthTarget = ViewTargets.Depth.get();
	Callbacks.DepthVisualization = [this, &SceneView, DepthTarget, ColorTarget]
	{
		SceneDepthRenderer.OnRender(SceneView, DepthTarget, ColorTarget, MaxRange);
	};
	if (IsCascade())
		Callbacks.CascadeVisualization = [this, &SceneView, DepthTarget, ColorTarget]
		{
			CascadeFloorRenderer.OnRender(SceneView, RenderData, DepthTarget, ColorTarget);
		};

	const FViewportSettings ViewportSetting{0, 0, ViewTargets.SceneColor->GetWidth(), ViewTargets.SceneColor->GetHeight(), 0.0f, 1.0f};
	const bool bIsActiveViewport = (GCurrentLevelEditingViewportClient == this);

	const bool bDrawSelectedLight = bIsActiveViewport && Gizmo->GetTarget() &&
	                                (Cast<ASpotLightActor>(Gizmo->GetTarget()->GetOwner()) ||
	                                 Cast<APointLightActor>(Gizmo->GetTarget()->GetOwner()) ||
	                                 Cast<ADecalActor>(Gizmo->GetTarget()->GetOwner()));

	const bool bDrawCascadeBounds = Settings.bDrawCascadeBounds;
	{
		Callbacks.Overlays.EditorLines = [this, &SceneView, &Settings, LineBatcher, Gizmo, Outline, bDrawSelectedLight, bDrawCascadeBounds]
		{
			LineBatcher->BeginFrame();
			if (bDrawCascadeBounds)
			{
				FSceneRenderData& Data = RenderData;
				const FVector4 Colors[4] = {
				    FVector4(1.f, 0.2f, 0.2f, 1.f),
				    FVector4(1.f, 0.8f, 0.2f, 1.f),
				    FVector4(0.2f, 1.f, 0.4f, 1.f),
				    FVector4(0.2f, 0.6f, 1.f, 1.f)};
				const FBox ClipBox{FVector(-1.f, -1.f, 0.f), FVector(1.f, 1.f, 1.f)};
				// 광원의 첫 그림자 뷰(VisibleLightShadowViews)부터 cascade 순서로 들어 있다. 그림자가 없는 방향광은 건너뛴다.
				const uint32 DirectionalOffset = Data.GetDirectionalLightOffset();
				for (uint32 LightIndex = 0; LightIndex < Data.GetDirectionalLightCount(); ++LightIndex)
				{
					const uint32 VisibleIndex = DirectionalOffset + LightIndex;
					if (VisibleIndex >= static_cast<uint32>(Data.VisibleLightShadowViews.Num()))
						continue;
					const uint32 FirstCascade = Data.VisibleLightShadowViews[VisibleIndex];
					if (FirstCascade == FSceneRenderData::InvalidShadowView)
						continue;
					for (uint32 CascadeIndex = 0; CascadeIndex < Data.CascadeShadowCount; ++CascadeIndex)
					{
						const uint32 ShadowIndex = FirstCascade + CascadeIndex;
						if (ShadowIndex >= static_cast<uint32>(Data.LightShadows.Num()))
							break;
						LineBatcher->AddOrientedBox(ClipBox, Data.LightShadows[ShadowIndex].ViewProjection.Inverse(), Colors[CascadeIndex]);
					}
				}
			}
			if (Settings.bDrawBatchLine && Settings.bDrawBoundingBox)
			{
				LineBatcher->BuildVertexBuffer();
			}

			if (bDrawSelectedLight)
			{
				if (ASpotLightActor* Light = Cast<ASpotLightActor>(Gizmo->GetTarget()->GetOwner()))
					Light->GetSpotLightComponent()->DrawDebug(LineBatcher);
				else if (APointLightActor* Light = Cast<APointLightActor>(Gizmo->GetTarget()->GetOwner()))
					Light->GetPointLightComponent()->DrawDebug(LineBatcher);
				else if (ADecalActor* Decal = Cast<ADecalActor>(Gizmo->GetTarget()->GetOwner()))
					Decal->GetDecalComponent()->DrawDebug(LineBatcher);
			}

			const AActor* SelectedActor =
			    Gizmo->GetTarget() ? Gizmo->GetTarget()->GetOwner() : nullptr;

			// 이 뷰포트가 보고 있는 월드의 것을 그린다. PIE와 SIE 중에는 PIE 월드다.
			DrawCollisionShapes(GetWorld(), LineBatcher, SelectedActor);
			DrawSpringArms(GetWorld(), LineBatcher, SelectedActor);
			if (EngineShowFlags.bShowParticles)
			{
				DrawParticleSystems(GetWorld(), LineBatcher);
				DrawParticleLODDistances(GetWorld(), LineBatcher, SelectedActor);
			}

			LineBatcher->OnRender(SceneView.ViewProjectionMatrix);
			// 선택한 물리 형상은 별도 배치로 깊이 검사를 끄고 그려 메시 표면에 묻히지 않게 한다.
			LineBatcher->BeginFrame();
			DrawSelectedMeshPhysics(GetWorld(), LineBatcher, Gizmo, Outline);
			// 실행 중 물리 바디·관절. 스킨 메시 안쪽 바디(래그돌)도 보이도록 깊이 검사 없이 그린다.
			if (Settings.bShowPhysicsBodies || Settings.bShowPhysicsConstraints)
				DrawPhysicsDebug(GetWorld(), LineBatcher, Settings.bShowPhysicsBodies, Settings.bShowPhysicsConstraints);
			LineBatcher->SetDepthTest(false);
			LineBatcher->OnRender(SceneView.ViewProjectionMatrix);
			LineBatcher->SetDepthTest(true);
		};
	}
	if (Settings.bDrawBatchLine)
	{
		Callbacks.Overlays.EditorGrid = [this, &SceneView, &Settings, ViewportSetting]
		{
			EGridPlane GridPlane = EGridPlane::XY;
			switch (ViewportType)
			{
			case ELevelViewportType::Front:
			case ELevelViewportType::Back:
				GridPlane = EGridPlane::YZ;
				break;
			case ELevelViewportType::Right:
			case ELevelViewportType::Left:
				GridPlane = EGridPlane::XZ;
				break;
			default:
				break;
			}
			DrawHelper.OnRenderGrid(SceneView.ViewProjectionMatrix, SceneView.ViewLocation,
			                        SceneView.ViewForward, GridPlane,
			                        EditorViewportMath::GridCellSize(Settings.GridSpacing),
			                        SceneView.bIsPerspective, ViewportSetting);
		};
	}
	if (bIsActiveViewport && Outline->GetTarget())
		Callbacks.Overlays.EditorOutline = [&SceneView, Outline, ViewportSetting]
		{
			Outline->Render(&SceneView, ViewportSetting);
		};
	if (bIsActiveViewport && Gizmo->GetTarget())
		Callbacks.Overlays.EditorGizmo = [&SceneView, Gizmo]
		{
			Gizmo->Render(&SceneView);
		};
	Callbacks.Overlays.bClearEditorDepth = true;
	if (Settings.bShowUUID)
		Callbacks.Overlays.EditorUUID = [this, &SceneView]
		{
			RenderActorUUIDs(SceneView);
		};
}

// 이 뷰포트가 보는 월드의 액터마다, 경계 상자 위에 카메라를 향한 UUID 글자를 그린다.
void FLevelEditorViewportClient::RenderActorUUIDs(const FSceneView& SceneView)
{
	const bool bOrtho = !SceneView.bIsPerspective;
	UWorld* TargetWorld = GetWorld();
	if (!TargetWorld || !SystemFont)
		return;

	for (AActor* Actor : TargetWorld->GetPersistentLevel()->GetActors())
	{
		if (!Actor)
			continue;
		USceneComponent* Root = Actor->GetRootComponent();

		if (!Root)
			continue;

		FBox Box = Root->CalcBounds();
		FVector UUIDLoc((Box.Min.X + Box.Max.X) * 0.5f,
		                (Box.Min.Y + Box.Max.Y) * 0.5f, Box.Max.Z + 0.5f);

		FString Text = "UUID : " + std::to_string(Actor->GetUUID());
		TextRenderer.BuildTextMesh(Text, 0.5f, *SystemFont);

		FVector Facing = bOrtho ? (SceneView.ViewForward * -1.0f)
		                        : (SceneView.ViewLocation - UUIDLoc).Normalized();
		FVector Up = FVector(0.0f, 0.0f, 1.0f);
		FVector Right = FVector::Cross(Up, Facing).Normalized();
		FVector RealUp = FVector::Cross(Facing, Right).Normalized();
		FMatrix BillboardWorld = FMatrix::Identity;
		BillboardWorld.M[0][0] = Facing.X;
		BillboardWorld.M[0][1] = Facing.Y;
		BillboardWorld.M[0][2] = Facing.Z;
		BillboardWorld.M[1][0] = Right.X;
		BillboardWorld.M[1][1] = Right.Y;
		BillboardWorld.M[1][2] = Right.Z;
		BillboardWorld.M[2][0] = RealUp.X;
		BillboardWorld.M[2][1] = RealUp.Y;
		BillboardWorld.M[2][2] = RealUp.Z;
		BillboardWorld.M[3][0] = UUIDLoc.X;
		BillboardWorld.M[3][1] = UUIDLoc.Y;
		BillboardWorld.M[3][2] = UUIDLoc.Z;

		TextRenderer.OnRender(Text, BillboardWorld, 0.5f, *SystemFont,
		                      SceneView.ViewProjectionMatrix);
	}
}
