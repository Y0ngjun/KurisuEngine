#include "EnginePCH.h"
#include "PhysicsAssetEditor.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "Collision/Ray.h"
#include "Core/Misc/Paths.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "PhysicsAssetEditorTab.h"
#include "PhysicsBodyRenderer.h"
#include "Render/SceneView.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Physics/PhysicsAsset.h"
#include "Render/LineBatcher.h"
#include "Render/SkeletalMesh.h"
#include "UObject/UObjectGlobals.h"
#include "imgui.h"

namespace
{
	// 본 월드 행렬에서 스케일을 뺀 것 = 바디 프레임 (실행 바디와 같은 규칙). 행 i = 로컬 i축.
	FMatrix BoneFrameWithoutScale(const USkeletalMeshComponent& Component, int32 BoneIndex)
	{
		FMatrix Frame = Component.GetBoneWorldMatrix(BoneIndex);
		for (int32 Row = 0; Row < 3; ++Row)
		{
			const FVector Axis = FVector(Frame.M[Row][0], Frame.M[Row][1], Frame.M[Row][2]).GetSafeNormal();
			Frame.M[Row][0] = Axis.X;
			Frame.M[Row][1] = Axis.Y;
			Frame.M[Row][2] = Axis.Z;
		}
		return Frame;
	}

	FVector FrameOrigin(const FMatrix& Frame)
	{
		return FVector(Frame.M[3][0], Frame.M[3][1], Frame.M[3][2]);
	}

	// 광선(Origin + Direction * t, Direction 단위벡터)과 형상의 첫 교차 거리 t. 안 맞으면 음수.
	float RaySphere(const FVector& Origin, const FVector& Direction, const FVector& Center, float Radius)
	{
		const FVector ToOrigin = Origin - Center;
		const float B = ToOrigin.Dot(Direction);
		const float C = ToOrigin.Dot(ToOrigin) - Radius * Radius;
		const float Discriminant = B * B - C;
		if (Discriminant < 0.0f)
			return -1.0f;
		const float Root = std::sqrt(Discriminant);
		const float Near = -B - Root;
		return Near >= 0.0f ? Near : (-B + Root >= 0.0f ? 0.0f : -1.0f);	// 안에서 쏘면 0
	}

	// 로컬 공간의 축 정렬 박스 (-HalfExtent ~ HalfExtent). 슬랩 방식.
	float RayBox(const FVector& Origin, const FVector& Direction, const FVector& HalfExtent)
	{
		float Enter = 0.0f;
		float Exit = FLT_MAX;
		const float O[3] = {Origin.X, Origin.Y, Origin.Z};
		const float D[3] = {Direction.X, Direction.Y, Direction.Z};
		const float H[3] = {HalfExtent.X, HalfExtent.Y, HalfExtent.Z};
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (std::abs(D[Axis]) < 1e-8f)
			{
				if (O[Axis] < -H[Axis] || O[Axis] > H[Axis])
					return -1.0f;
				continue;
			}
			float T0 = (-H[Axis] - O[Axis]) / D[Axis];
			float T1 = (H[Axis] - O[Axis]) / D[Axis];
			if (T0 > T1)
				std::swap(T0, T1);
			Enter = std::max(Enter, T0);
			Exit = std::min(Exit, T1);
			if (Enter > Exit)
				return -1.0f;
		}
		return Enter;
	}

	// 로컬 공간의 캡슐: 축 = Z, 원기둥 반길이 HalfLength, 반지름 Radius. 원기둥 옆면 + 양 끝 반구.
	float RayCapsule(const FVector& Origin, const FVector& Direction, float HalfLength, float Radius)
	{
		float Best = -1.0f;
		const auto Consider = [&Best](float T)
		{
			if (T >= 0.0f && (Best < 0.0f || T < Best))
				Best = T;
		};

		const float A = Direction.X * Direction.X + Direction.Y * Direction.Y;
		if (A > 1e-8f)
		{
			const float B = Origin.X * Direction.X + Origin.Y * Direction.Y;
			const float C = Origin.X * Origin.X + Origin.Y * Origin.Y - Radius * Radius;
			const float Discriminant = B * B - A * C;
			if (Discriminant >= 0.0f)
			{
				const float T = (-B - std::sqrt(Discriminant)) / A;
				const float Z = Origin.Z + Direction.Z * T;
				if (Z >= -HalfLength && Z <= HalfLength)
					Consider(C <= 0.0f && std::abs(Origin.Z) <= HalfLength ? 0.0f : T);
			}
		}
		Consider(RaySphere(Origin, Direction, FVector(0.0f, 0.0f, HalfLength), Radius));
		Consider(RaySphere(Origin, Direction, FVector(0.0f, 0.0f, -HalfLength), Radius));
		return Best;
	}

	// 관절 프레임의 세 축 (X 빨강 = Twist축, Y 초록, Z 파랑). PIE 관절 디버그와 같은 색.
	void DrawConstraintFrame(FLineBatcher& Lines, const FMatrix& Frame, float Length)
	{
		static const FVector4 AxisColors[3] = {{1.0f, 0.15f, 0.15f, 1.0f}, {0.2f, 1.0f, 0.2f, 1.0f}, {0.25f, 0.45f, 1.0f, 1.0f}};
		const FVector Origin = FrameOrigin(Frame);
		for (int32 Row = 0; Row < 3; ++Row)
		{
			const FVector Axis = FVector(Frame.M[Row][0], Frame.M[Row][1], Frame.M[Row][2]).GetSafeNormal();
			Lines.AddLine(Origin, Origin + Axis * Length, AxisColors[Row]);
		}
	}
}

FPhysicsAssetEditor::FPhysicsAssetEditor(UEditorEngine& Engine, FEditorManager& Manager, const FString& Path)
	: FSkeletalPreviewEditor(Engine, Manager, Path)
{
}

FPhysicsAssetEditor::~FPhysicsAssetEditor()
{
	delete PhysicsAsset;
	PhysicsAsset = nullptr;
}

bool FPhysicsAssetEditor::IsPhysicsAssetPath(const FString& Path)
{
	FString Extension = FPaths::FromUtf8(Path).extension().string();
	std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
	return Extension == ".physicsasset";
}

FString FPhysicsAssetEditor::ResolvePreviewMeshPath() const
{
	if (PhysicsAsset && !PhysicsAsset->PreviewMeshPath.empty())
		return PhysicsAsset->PreviewMeshPath;

	// 과도기 경로 Content/Physics/PhysicsAssets/<메시 경로>.physicsasset 이면 메시 경로를 되찾을 수 있다.
	const FString FilePath = FAssetUtils::ResolveAssetFilePath(GetAssetPath());
	if (FilePath.empty())
		return {};
	const std::filesystem::path File = FPaths::FromUtf8(FilePath);
	const std::filesystem::path Root = FPaths::ProjectContentDir() / "Physics" / "PhysicsAssets";
	std::error_code Error;
	std::filesystem::path Relative = std::filesystem::relative(File, Root, Error);
	if (Error || Relative.empty() || *Relative.begin() == "..")
		return {};
	Relative.replace_extension();	// <메시 경로>.physicsasset → <메시 경로>
	return FPaths::ToUtf8(std::filesystem::path(Relative.generic_wstring()));
}

bool FPhysicsAssetEditor::Save()
{
	if (!PhysicsAsset)
		return false;
	BodyGizmo.EndDrag();
	const FString FilePath = FAssetUtils::ResolveAssetFilePath(GetAssetPath());
	const std::filesystem::path File = FPaths::FromUtf8(FilePath);
	if (FilePath.empty() || !PhysicsAsset->SaveToFile(File))
	{
		KRS_LOG(Error, "[Physics] Failed to save physics asset: {}", GetAssetPath());
		return false;
	}
	KRS_LOG(Info, "[Physics] Physics asset saved: {}", GetAssetPath());

	// 에디터는 파일에서 따로 읽은 사본을 고친다. 저장한 파일이 메시에 연결된 에셋(과도기 경로)이면
	// 메시가 들고 있는 에셋도 파일에서 다시 읽어, 게임에서 쓰는 래그돌이 바로 새 값을 쓰게 한다.
	if (Mesh && FPaths::HasProject() && !Mesh->GetPath().empty())
	{
		std::filesystem::path Assigned = FPaths::ProjectContentDir() / "Physics" / "PhysicsAssets" / FPaths::FromUtf8(Mesh->GetPath());
		Assigned += ".physicsasset";
		std::error_code Error;
		if (std::filesystem::equivalent(File, Assigned, Error) && !Error)
			Mesh->RevertPhysicsAsset();
	}
	return true;
}

bool FPhysicsAssetEditor::InitializeResources()
{
	// 1. 물리 에셋 읽기
	const FString FilePath = FAssetUtils::ResolveAssetFilePath(GetAssetPath());
	PhysicsAsset = NewObject<UPhysicsAsset>();
	if (!PhysicsAsset || FilePath.empty() || !PhysicsAsset->LoadFromFile(FPaths::FromUtf8(FilePath)))
	{
		KRS_LOG(Warning, "[Physics] Failed to open physics asset: {}", GetAssetPath());
		return false;
	}

	// 2. 미리보기 메시 읽기
	PreviewMeshPath = ResolvePreviewMeshPath();
	if (PreviewMeshPath.empty())
	{
		KRS_LOG(Warning, "[Physics] Physics asset has no preview mesh. Recreate it from a skeletal mesh: {}", GetAssetPath());
		return false;
	}
	const FString MeshFilePath = FAssetUtils::ResolveAssetFilePath(PreviewMeshPath);
	FString Error;
	Mesh = UAssetManager::LoadSkeletalMesh(MeshFilePath.empty() ? PreviewMeshPath : MeshFilePath, Error);
	if (!Mesh)
	{
		KRS_LOG(Warning, "[Physics] Physics asset preview mesh failed to load: {} ({})", PreviewMeshPath, Error);
		return false;
	}

	// 3. 프리뷰 씬에 메시 컴포넌트를 띄운다
	if (!InitializePreview())
		return false;

	// 4. 이 에디터의 주인공은 바디이므로 본은 기본으로 끈다 (뷰포트 메뉴에서 다시 켤 수 있다).
	SetBoneDrawMode(EBoneDrawMode::None);

	// 5. 바디 기즈모가 따라갈 빈 컴포넌트. 축은 바디 형상 기준(Local)으로 보여 준다.
	BodyGizmoTarget = Cast<USceneComponent>(PreviewScene.AddComponent(USceneComponent::StaticClass()));
	if (!BodyGizmoTarget)
		return false;
	BodyGizmoTarget->DetachFromParent();
	BodyGizmo.SetSpace(EGizmoSpace::Local);
	BodyGizmo.SetScreenScale(0.6f);	// 바디는 작으므로 레벨 기즈모보다 작게
	return true;
}

void FPhysicsAssetEditor::OnRenderOverlay(const FSceneView& View)
{
	if (!PhysicsAsset || !MeshComponent)
		return;
	if (!BodyRenderer)
		BodyRenderer = MakeUnique<FPhysicsBodyRenderer>();
	if (!BodyRenderer->Begin(View.ViewProjectionMatrix, View.ViewLocation))
		return;

	// 선과 같은 색, 면은 옅게. 무언가 선택돼 있으면 나머지는 더 옅게.
	const bool bAnySelected = SelectedBodyIndex >= 0 || SelectedConstraintIndex >= 0;
	const FVector4 BodyColor = bAnySelected ? FVector4(0.3f, 0.8f, 1.0f, 0.12f) : FVector4(0.3f, 0.8f, 1.0f, 0.3f);
	const FVector4 SelectedBodyColor(1.0f, 0.85f, 0.2f, 0.45f);
	const FVector Scale = MeshComponent->GetWorldScale3D();
	for (int32 BodyIndex = 0; BodyIndex < PhysicsAsset->SkeletalBodySetups.Num(); ++BodyIndex)
	{
		const USkeletalBodySetup* Setup = PhysicsAsset->SkeletalBodySetups[BodyIndex];
		if (!Setup)
			continue;
		const int32 BoneIndex = MeshComponent->FindBoneIndex(Setup->BoneName.ToString());
		if (BoneIndex < 0)
			continue;
		BodyRenderer->DrawBody(*Setup, BoneFrameWithoutScale(*MeshComponent, BoneIndex), Scale,
		                       BodyIndex == SelectedBodyIndex ? SelectedBodyColor : BodyColor);
	}
}

void FPhysicsAssetEditor::OnRenderLine(FLineBatcher& LineBatcher)
{
	FSkeletalPreviewEditor::OnRenderLine(LineBatcher);
	if (!PhysicsAsset || !MeshComponent)
		return;

	// 바디: 스케일을 뺀 본 월드 행렬 위에 형상을 그린다. 형상 크기는 컴포넌트 스케일로 키운다 (실행 바디와 같은 규칙).
	const FVector Scale = MeshComponent->GetWorldScale3D();
	// 무언가 선택돼 있으면 나머지는 흐리게, 선택한 바디는 노랗게 그린다.
	const bool bAnySelected = SelectedBodyIndex >= 0 || SelectedConstraintIndex >= 0;
	const FVector4 BodyColor = bAnySelected ? FVector4(0.2f, 0.45f, 0.6f, 1.0f) : FVector4(0.3f, 0.8f, 1.0f, 1.0f);
	const FVector4 SelectedBodyColor(1.0f, 0.85f, 0.2f, 1.0f);
	for (int32 BodyIndex = 0; BodyIndex < PhysicsAsset->SkeletalBodySetups.Num(); ++BodyIndex)
	{
		USkeletalBodySetup* Setup = PhysicsAsset->SkeletalBodySetups[BodyIndex];
		if (!Setup)
			continue;
		const int32 BoneIndex = MeshComponent->FindBoneIndex(Setup->BoneName.ToString());
		if (BoneIndex < 0)
			continue;

		Setup->DrawDebug(LineBatcher, BoneFrameWithoutScale(*MeshComponent, BoneIndex), Scale,
		                 BodyIndex == SelectedBodyIndex ? SelectedBodyColor : BodyColor);
	}

	// 관절: 각 본 공간에 저장된 관절 프레임(GetRefFrame)을 본 월드 프레임에 얹는다.
	// Frame1 = 자식 본(Bone1) 쪽, Frame2 = 부모 본(Bone2) 쪽. 정상이면 두 프레임이 같은 자리에 겹친다.
	const FVector4 ConstraintColor(0.0f, 0.0f, 0.0f, 1.0f);	// 관절 표시(연결선·선택 구)는 검은색으로 통일
	for (int32 ConstraintIndex = 0; ConstraintIndex < PhysicsAsset->ConstraintSetup.Num(); ++ConstraintIndex)
	{
		const UPhysicsConstraintTemplate* Template = PhysicsAsset->ConstraintSetup[ConstraintIndex];
		if (!Template)
			continue;
		const bool bSelected = ConstraintIndex == SelectedConstraintIndex;
		const FConstraintInstance& Constraint = Template->DefaultInstance;
		const int32 ChildBone = MeshComponent->FindBoneIndex(Constraint.ConstraintBone1.ToString());
		const int32 ParentBone = MeshComponent->FindBoneIndex(Constraint.ConstraintBone2.ToString());
		if (ChildBone < 0 || ParentBone < 0)
			continue;

		FTransform Ref1 = Constraint.GetRefFrame(EConstraintFrame::Frame1);
		FTransform Ref2 = Constraint.GetRefFrame(EConstraintFrame::Frame2);
		Ref1.Location = Ref1.Location * Scale.X;	// 실행 관절도 위치에만 컴포넌트 스케일을 곱한다
		Ref2.Location = Ref2.Location * Scale.X;
		const FMatrix Frame1 = Ref1.GetLocalMatrix() * BoneFrameWithoutScale(*MeshComponent, ChildBone);
		const FMatrix Frame2 = Ref2.GetLocalMatrix() * BoneFrameWithoutScale(*MeshComponent, ParentBone);

		// 선택한 관절은 축을 길게 그리고 위치에 구를 그려 눈에 띄게 한다.
		DrawConstraintFrame(LineBatcher, Frame1, bSelected ? 20.0f : 8.0f);
		DrawConstraintFrame(LineBatcher, Frame2, bSelected ? 14.0f : 5.0f);
		LineBatcher.AddLine(FrameOrigin(Frame1), FrameOrigin(Frame2), ConstraintColor);
		if (bSelected)
			LineBatcher.AddSphere(FrameOrigin(Frame1), 3.0f, ConstraintColor);
	}
}

void FPhysicsAssetEditor::OnBoneSelected()
{
	BodyGizmo.EndDrag();
	SelectedBodyIndex = -1;
	SelectedConstraintIndex = -1;
}

int32 FPhysicsAssetEditor::GetBodyBoneIndex(int32 BodyIndex) const
{
	if (!PhysicsAsset || !Mesh || BodyIndex < 0 || BodyIndex >= PhysicsAsset->SkeletalBodySetups.Num() || !PhysicsAsset->SkeletalBodySetups[BodyIndex])
		return -1;
	return Mesh->FindBoneIndex(PhysicsAsset->SkeletalBodySetups[BodyIndex]->BoneName.ToString());
}

int32 FPhysicsAssetEditor::GetConstraintBoneIndex(int32 ConstraintIndex) const
{
	if (!PhysicsAsset || !Mesh || ConstraintIndex < 0 || ConstraintIndex >= PhysicsAsset->ConstraintSetup.Num() || !PhysicsAsset->ConstraintSetup[ConstraintIndex])
		return -1;
	// 관절은 자식 본(Bone1) 원점에 있다.
	return Mesh->FindBoneIndex(PhysicsAsset->ConstraintSetup[ConstraintIndex]->DefaultInstance.ConstraintBone1.ToString());
}

void FPhysicsAssetEditor::SelectBody(int32 BodyIndex)
{
	// 본 선택을 먼저 바꾸고(OnBoneSelected가 선택을 비운다) 그다음 바디를 고른다.
	SelectBone(GetBodyBoneIndex(BodyIndex));
	SelectedBodyIndex = GetBodyBoneIndex(BodyIndex) >= 0 ? BodyIndex : -1;
}

void FPhysicsAssetEditor::SelectConstraint(int32 ConstraintIndex)
{
	SelectBone(GetConstraintBoneIndex(ConstraintIndex));
	SelectedConstraintIndex = GetConstraintBoneIndex(ConstraintIndex) >= 0 ? ConstraintIndex : -1;
}

void FPhysicsAssetEditor::ShutdownResources()
{
	BodyGizmo.EndDrag();
	BodyGizmo.SetTarget(nullptr);
	BodyGizmoTarget = nullptr;
	FSkeletalPreviewEditor::ShutdownResources();
}

void FPhysicsAssetEditor::TickAfterWorld(float DeltaTime)
{
	UpdateGizmoModeKeys();
	// 기즈모가 마우스를 쓰지 않았을 때만 바디를 피킹하고, 마지막에 기즈모를 선택한 바디로 옮긴다.
	if (!UpdateBodyGizmo())
		UpdateBodyPicking();
	SyncBodyGizmo();
}

void FPhysicsAssetEditor::SetGizmoMode(EGizmoMode Mode)
{
	BodyGizmo.EndDrag();
	BodyGizmo.SetMode(Mode);
	SyncBodyGizmo();
}

void FPhysicsAssetEditor::UpdateGizmoModeKeys()
{
	// 스켈레탈 메시 에디터처럼 W/E/R. 우클릭 비행 중(카메라 이동 W/E)이나 글자 입력 중에는 바꾸지 않는다.
	const ImGuiIO& IO = ImGui::GetIO();
	if (!PointerInput.bFocused || !PointerInput.bHovered || CameraInput.bCaptured || IO.WantTextInput ||
	    ImGui::IsMouseDown(ImGuiMouseButton_Right) || BodyGizmo.IsUsing())
		return;
	if (ImGui::IsKeyPressed(ImGuiKey_W, false))
		SetGizmoMode(EGizmoMode::Location);
	else if (ImGui::IsKeyPressed(ImGuiKey_E, false))
		SetGizmoMode(EGizmoMode::Rotation);
	else if (ImGui::IsKeyPressed(ImGuiKey_R, false))
		SetGizmoMode(EGizmoMode::Scale);
}

bool FPhysicsAssetEditor::UpdateBodyGizmo()
{
	USkeletalBodySetup* Setup = (PhysicsAsset && SelectedBodyIndex >= 0 && SelectedBodyIndex < PhysicsAsset->SkeletalBodySetups.Num())
	                                ? PhysicsAsset->SkeletalBodySetups[SelectedBodyIndex]
	                                : nullptr;
	if (!CanProcessViewportInput() || !BodyGizmo.GetTarget() || !Setup)
	{
		BodyGizmo.EndDrag();
		return false;
	}

	FKAggregateGeom& Geom = Setup->AggGeom;
	const bool bWasUsing = BodyGizmo.IsUsing();

	// Esc: 드래그 시작 값으로 되돌린다.
	if (PointerInput.bCancel)
	{
		if (bWasUsing && DragStart.BodyIndex == SelectedBodyIndex)
		{
			if (Geom.SphereElems.Num() > 0)
			{
				Geom.SphereElems[0].Center = DragStart.Center;
				Geom.SphereElems[0].Radius = DragStart.Radius;
			}
			else if (Geom.SphylElems.Num() > 0)
			{
				Geom.SphylElems[0].Center = DragStart.Center;
				Geom.SphylElems[0].Rotation = DragStart.Rotation;
				Geom.SphylElems[0].Radius = DragStart.Radius;
				Geom.SphylElems[0].Length = DragStart.Length;
			}
			else if (Geom.BoxElems.Num() > 0)
			{
				Geom.BoxElems[0].Center = DragStart.Center;
				Geom.BoxElems[0].Rotation = DragStart.Rotation;
				Geom.BoxElems[0].X = DragStart.BoxSize.X;
				Geom.BoxElems[0].Y = DragStart.BoxSize.Y;
				Geom.BoxElems[0].Z = DragStart.BoxSize.Z;
			}
			else if (Geom.ConvexElems.Num() > 0)
				Geom.ConvexElems[0].Transform = DragStart.ConvexTransform;
		}
		BodyGizmo.EndDrag();
		return false;
	}

	if ((!PointerInput.bHovered && !bWasUsing) || CameraInput.bCaptured)
	{
		BodyGizmo.EndDrag();
		return false;
	}

	SyncBodyGizmo();
	const int32 Width = static_cast<int32>(ViewportClient->GetWidth());
	const int32 Height = static_cast<int32>(ViewportClient->GetHeight());
	const FVector2 MousePosition(PointerInput.MouseUV.X * Width, PointerInput.MouseUV.Y * Height);
	const FRay MouseRay = ViewportClient->DeprojectScreenToWorld(MousePosition);

	// 클릭한 프레임에만 시작하고, 버튼을 놓는 프레임의 마지막 마우스 위치도 반영한다.
	const bool bUpdateDrag = bWasUsing || (PointerInput.bMousePressed && PointerInput.bMouseDown);
	BodyGizmo.Update(MouseRay, MousePosition, ViewportClient->GetViewProjectionMatrix(),
	                 Width, Height, bUpdateDrag, ViewportClient->GetViewLocation(), !ViewportClient->IsPerspective());

	if (!bWasUsing && BodyGizmo.IsUsing())
	{
		// 드래그 시작 값 저장. 본 프레임은 드래그 동안 고정한다 (미리보기 포즈는 움직이지 않는다).
		const int32 BoneIndex = MeshComponent->FindBoneIndex(Setup->BoneName.ToString());
		DragStart = FShapeDragStart();
		DragStart.BodyIndex = SelectedBodyIndex;
		DragStart.BoneFrameInverse = BoneFrameWithoutScale(*MeshComponent, BoneIndex).Inverse();
		if (Geom.SphereElems.Num() > 0)
		{
			DragStart.Center = Geom.SphereElems[0].Center;
			DragStart.Radius = Geom.SphereElems[0].Radius;
		}
		else if (Geom.SphylElems.Num() > 0)
		{
			DragStart.Center = Geom.SphylElems[0].Center;
			DragStart.Rotation = Geom.SphylElems[0].Rotation;
			DragStart.Radius = Geom.SphylElems[0].Radius;
			DragStart.Length = Geom.SphylElems[0].Length;
		}
		else if (Geom.BoxElems.Num() > 0)
		{
			DragStart.Center = Geom.BoxElems[0].Center;
			DragStart.Rotation = Geom.BoxElems[0].Rotation;
			DragStart.BoxSize = FVector(Geom.BoxElems[0].X, Geom.BoxElems[0].Y, Geom.BoxElems[0].Z);
		}
		else if (Geom.ConvexElems.Num() > 0)
			DragStart.ConvexTransform = Geom.ConvexElems[0].Transform;
	}

	if (BodyGizmo.IsUsing())
	{
		ApplyBodyGizmo();
		if (!PointerInput.bMouseDown)
		{
			BodyGizmo.EndDrag();
			// 놓을 때 한 번만 에셋을 Dirty로 만들고 실행 바디를 다시 만든다 (드래그 중에는 그리기만 바뀐다).
			PhysicsAsset->InvalidatePhysicsData();
		}
	}

	return bWasUsing || BodyGizmo.IsUsing() || BodyGizmo.GetHoveredAxis() >= 0;
}

void FPhysicsAssetEditor::ApplyBodyGizmo()
{
	if (!PhysicsAsset || DragStart.BodyIndex != SelectedBodyIndex || SelectedBodyIndex < 0 ||
	    SelectedBodyIndex >= PhysicsAsset->SkeletalBodySetups.Num() || !PhysicsAsset->SkeletalBodySetups[SelectedBodyIndex])
		return;
	FKAggregateGeom& Geom = PhysicsAsset->SkeletalBodySetups[SelectedBodyIndex]->AggGeom;

	// 기즈모 대상의 월드 Transform → 본 프레임 기준 형상 로컬 (그리기 규칙의 역)
	const FMatrix TargetWorld = FTransform(BodyGizmoTarget->GetWorldRotation(), BodyGizmoTarget->GetWorldLocation()).GetLocalMatrix();
	const FMatrix ShapeLocal = TargetWorld * DragStart.BoneFrameInverse;
	const FVector NewCenter = FrameOrigin(ShapeLocal);
	const FRotator NewRotation = MatrixToRotator(ShapeLocal);
	// 크기 모드: 기즈모 대상의 축별 배율(시작 1). 한 번에 한 축만 바뀌므로 곱으로 합쳐도 그 축 배율이 된다.
	const FVector S = BodyGizmoTarget->GetRelativeScale3D();
	constexpr float MinSize = 0.01f;	// AggregateGeom의 ClampMin과 같다

	const EGizmoMode Mode = BodyGizmo.GetMode();
	if (Geom.SphereElems.Num() > 0)
	{
		FKSphereElem& Sphere = Geom.SphereElems[0];
		if (Mode == EGizmoMode::Location)
			Sphere.Center = NewCenter;
		else if (Mode == EGizmoMode::Scale)
			Sphere.Radius = FMath::Max(DragStart.Radius * S.X * S.Y * S.Z, MinSize);
		// 구는 회전이 없다
	}
	else if (Geom.SphylElems.Num() > 0)
	{
		FKSphylElem& Capsule = Geom.SphylElems[0];
		if (Mode == EGizmoMode::Location)
			Capsule.Center = NewCenter;
		else if (Mode == EGizmoMode::Rotation)
			Capsule.Rotation = NewRotation;
		else if (Mode == EGizmoMode::Scale)
		{
			// 캡슐 축은 형상 로컬 Z: X/Y는 반지름, Z는 원기둥 길이
			Capsule.Radius = FMath::Max(DragStart.Radius * S.X * S.Y, MinSize);
			Capsule.Length = FMath::Max(DragStart.Length * S.Z, 0.0f);
		}
	}
	else if (Geom.BoxElems.Num() > 0)
	{
		FKBoxElem& Box = Geom.BoxElems[0];
		if (Mode == EGizmoMode::Location)
			Box.Center = NewCenter;
		else if (Mode == EGizmoMode::Rotation)
			Box.Rotation = NewRotation;
		else if (Mode == EGizmoMode::Scale)
		{
			Box.X = FMath::Max(DragStart.BoxSize.X * S.X, MinSize);
			Box.Y = FMath::Max(DragStart.BoxSize.Y * S.Y, MinSize);
			Box.Z = FMath::Max(DragStart.BoxSize.Z * S.Z, MinSize);
		}
	}
	else if (Geom.ConvexElems.Num() > 0)
	{
		// 컨벡스는 꼭짓점을 그대로 두고 Transform의 위치·회전만 바꾼다
		FTransform& Transform = Geom.ConvexElems[0].Transform;
		if (Mode == EGizmoMode::Location)
			Transform.Location = NewCenter;
		else if (Mode == EGizmoMode::Rotation)
			Transform.Rotation = NewRotation.Quaternion();
	}
}

void FPhysicsAssetEditor::UpdateBodyPicking()
{
	// 본 피킹(UpdateBonePicking)과 같은 조건: 뷰포트 위에서 좌클릭이 시작된 프레임만
	if (!CanProcessViewportInput() || !Mesh || !MeshComponent || !PhysicsAsset ||
	    !PointerInput.bHovered || !PointerInput.bMousePressed || PointerInput.bCancel ||
	    CameraInput.bCaptured || ViewportWidth == 0 || ViewportHeight == 0)
	{
		return;
	}

	const FVector2 MousePosition(PointerInput.MouseUV.X * ViewportWidth, PointerInput.MouseUV.Y * ViewportHeight);
	const int32 Body = PickBody(MousePosition);
	if (Body >= 0)
		SelectBody(Body);
	else if (IsBoneVisualizerVisible())
		UpdateBonePicking();	// 바디가 안 맞으면 본을 고른다 (본도 안 맞으면 선택 해제)
	else
		SelectBone(-1);			// 빈 곳 클릭 = 선택 해제
}

void FPhysicsAssetEditor::SyncBodyGizmo()
{
	const USkeletalBodySetup* Setup = (PhysicsAsset && SelectedBodyIndex >= 0 && SelectedBodyIndex < PhysicsAsset->SkeletalBodySetups.Num())
	                                      ? PhysicsAsset->SkeletalBodySetups[SelectedBodyIndex]
	                                      : nullptr;
	const int32 BoneIndex = Setup && MeshComponent ? MeshComponent->FindBoneIndex(Setup->BoneName.ToString()) : -1;
	if (!BodyGizmoTarget || BoneIndex < 0)
	{
		BodyGizmo.SetTarget(nullptr);
		return;
	}
	// 드래그 중에는 기즈모가 대상 Transform을 쥐고 있으므로 덮어쓰지 않는다.
	if (BodyGizmo.IsUsing())
		return;

	// 형상 프레임은 그리기(DrawDebug, DrawBody)와 같은 규칙: 형상 로컬(Rotation, Center) × 스케일 뺀 본 월드.
	// 형상이 여러 개면 첫 형상(구 → 캡슐 → 박스 → 컨벡스 순)에 둔다. 형상이 없으면 본 위치에 둔다.
	const FMatrix BoneFrame = BoneFrameWithoutScale(*MeshComponent, BoneIndex);
	const FKAggregateGeom& Geom = Setup->AggGeom;
	FMatrix ShapeFrame = BoneFrame;
	if (Geom.SphereElems.Num() > 0)
		ShapeFrame = FMatrix::MakeTranslation(Geom.SphereElems[0].Center) * BoneFrame;
	else if (Geom.SphylElems.Num() > 0)
		ShapeFrame = FTransform(Geom.SphylElems[0].Rotation, Geom.SphylElems[0].Center).GetLocalMatrix() * BoneFrame;
	else if (Geom.BoxElems.Num() > 0)
		ShapeFrame = FTransform(Geom.BoxElems[0].Rotation, Geom.BoxElems[0].Center).GetLocalMatrix() * BoneFrame;
	else if (Geom.ConvexElems.Num() > 0)
		ShapeFrame = Geom.ConvexElems[0].Transform.GetLocalMatrix() * BoneFrame;

	BodyGizmoTarget->SetTransform(FTransform(MatrixToRotator(ShapeFrame), FrameOrigin(ShapeFrame)));
	BodyGizmo.SetTarget(BodyGizmoTarget);
}

int32 FPhysicsAssetEditor::PickBody(const FVector2& MousePosition) const
{
	// 레벨 에디터 피킹과 같은 마우스 광선 (원근·직교 모두 처리)
	const FRay Ray = ViewportClient->DeprojectScreenToWorld(MousePosition);
	const FVector RayStart = Ray.Origin;
	const FVector RayDirection = Ray.Direction.GetSafeNormal();
	if (RayDirection.SizeSquared() < 0.5f)
		return -1;

	// 그리기(UBodySetup::DrawDebug)와 같은 규칙으로 형상을 놓고 가장 가까운 것을 고른다.
	const FVector Scale = MeshComponent->GetWorldScale3D();
	int32 BestBody = -1;
	float BestDistance = FLT_MAX;
	for (int32 BodyIndex = 0; BodyIndex < PhysicsAsset->SkeletalBodySetups.Num(); ++BodyIndex)
	{
		const USkeletalBodySetup* Setup = PhysicsAsset->SkeletalBodySetups[BodyIndex];
		if (!Setup)
			continue;
		const int32 BoneIndex = MeshComponent->FindBoneIndex(Setup->BoneName.ToString());
		if (BoneIndex < 0)
			continue;
		const FMatrix BoneFrame = BoneFrameWithoutScale(*MeshComponent, BoneIndex);

		const auto Consider = [&](float Distance)
		{
			if (Distance >= 0.0f && Distance < BestDistance)
			{
				BestDistance = Distance;
				BestBody = BodyIndex;
			}
		};
		// 형상 로컬 공간으로 광선을 옮긴다. 프레임은 스케일이 없으므로 거리가 그대로 유지된다.
		const auto ToLocal = [&](const FMatrix& Local, FVector& OutOrigin, FVector& OutDirection)
		{
			const FMatrix Inverse = Local.Inverse();
			OutOrigin = Inverse.TransformPosition(RayStart);
			OutDirection = Inverse.TransformVector(RayDirection).GetSafeNormal();
		};

		const FKAggregateGeom& Geom = Setup->AggGeom;
		for (const FKSphereElem& Sphere : Geom.SphereElems)
			if (Sphere.Radius > 0)
				Consider(RaySphere(RayStart, RayDirection, BoneFrame.TransformPosition(Sphere.Center), Sphere.Radius * Scale.X));
		for (const FKBoxElem& Box : Geom.BoxElems)
		{
			if (Box.X <= 0 || Box.Y <= 0 || Box.Z <= 0)
				continue;
			FVector Origin, Direction;
			ToLocal(FTransform(Box.Rotation, Box.Center).GetLocalMatrix() * BoneFrame, Origin, Direction);
			Consider(RayBox(Origin, Direction, FVector(Box.X * 0.5f, Box.Y * 0.5f, Box.Z * 0.5f)));
		}
		for (const FKSphylElem& Capsule : Geom.SphylElems)
		{
			if (Capsule.Radius <= 0 || Capsule.Length < 0)
				continue;
			FVector Origin, Direction;
			ToLocal(FTransform(Capsule.Rotation, Capsule.Center).GetLocalMatrix() * BoneFrame, Origin, Direction);
			Consider(RayCapsule(Origin, Direction, Capsule.Length * 0.5f * Scale.X, Capsule.Radius * Scale.X));
		}
	}
	return BestBody;
}

TUniquePtr<IEditorTab> FPhysicsAssetEditor::CreateTab()
{
	return MakeUnique<FPhysicsAssetEditorTab>(*this);
}

bool FPhysicsAssetEditor::IsDirty() const
{
	return PhysicsAsset && PhysicsAsset->IsDirty();
}
