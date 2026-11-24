#include "EnginePCH.h"
#include "LuaBindings.h"
#include "LuaBindingsInternal.h"

#include "GameFramework/Actor.h"
#include "Script/LuaActorRef.h"
#include "Script/LuaWidgetRef.h"
#include "Script/CoroutineScheduler.h"
#include "Engine/World.h"
#include "Engine/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "UI/UserWidget.h"
#include "UI/Button.h"
#include "UI/TextBlock.h"
#include "UI/ProgressBar.h"
#include "Script/LuaScriptManager.h"
#include "Asset/AssetManager.h"
#include "Audio/SoundWave.h"

namespace
{
	struct FLuaClickBinding
	{
		AActor* Owner;
		FLuaWidgetRef Button;
	};
	TArray<FLuaClickBinding> ClickBindings;
	TArray<FLuaActorRef> PendingDestroies;
}

namespace LuaBindingsDetail
{
	AActor* OwnerFromEnv(sol::this_environment& ThisEnv)
	{
		if (!ThisEnv)
			return nullptr;

		sol::environment& Env = ThisEnv;
		sol::optional<FLuaActorRef> Ref = Env["obj"];
		return Ref
		           ? Ref->Get()
		           : nullptr;
	}

	UWorld* WorldFromEnv(sol::this_environment& ThisEnv, const char* FuncName)
	{
		AActor* Owner = OwnerFromEnv(ThisEnv);
		UWorld* World = Owner ? Owner->GetWorld() : nullptr;
		if (!World)
			throw sol::error(FString(FuncName) + " must be called from an actor script.");
		return World;
	}

	void AddPendingDestroy(const FLuaActorRef& Ref)
	{
		PendingDestroies.Add(Ref);
	}

	void TrackClickBinding(AActor* Owner, const FLuaWidgetRef& Button)
	{
		ClickBindings.Add({Owner, Button});
	}

	void RegisterAudio(sol::state& Lua)
	{
		Lua.set_function("PlaySound2D", [](sol::this_environment ThisEnv, const FString& Path,
		                                   sol::optional<float> Volume, sol::optional<float> Pitch)
		                 {
				if (USoundWave* Sound = UAssetManager::Get().LoadSound(Path))
					UGameplayStatics::PlaySound2D(WorldFromEnv(ThisEnv, "PlaySound2D"), Sound,
						Volume.value_or(1.0f), Pitch.value_or(1.0f)); });

		Lua.set_function("PlaySoundAtLocation", [](sol::this_environment ThisEnv, const FString& Path,
		                                           const FVector& Location, sol::optional<float> Volume, sol::optional<float> Pitch)
		                 {
				if (USoundWave* Sound = UAssetManager::Get().LoadSound(Path))
					UGameplayStatics::PlaySoundAtLocation(WorldFromEnv(ThisEnv, "PlaySoundAtLocation"), Sound,
						Location, Volume.value_or(1.0f), Pitch.value_or(1.0f)); });
	}

	void RegisterGameplay(sol::state& Lua)
	{
		// 실제 전환은 다음 Tick 시작. 호출한 뒤의 코드도 이번 프레임에는 그대로 실행된다.
		Lua.set_function("OpenLevel", [](sol::this_environment ThisEnv, const FString& LevelName)
		                 { UGameplayStatics::OpenLevel(WorldFromEnv(ThisEnv, "OpenLevel"), LevelName); });
		Lua.set_function("SetGameValue", [](sol::this_environment ThisEnv, const FString& Key, double Value)
		                 {
			if (UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldFromEnv(ThisEnv, "SetGameValue")))
				GameInstance->SetValue(Key, Value); });
		Lua.set_function("GetGameValue", [](sol::this_environment ThisEnv, const FString& Key, sol::optional<double> Default)
		                 {
			const double Fallback = Default.value_or(0.0);
			UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldFromEnv(ThisEnv, "GetGameValue"));
			return GameInstance ? GameInstance->GetValue(Key, Fallback) : Fallback; });

		// Require(경로): 다른 Lua 파일을 불러와 그 파일이 return한 값(보통 테이블)을 돌려준다.
		// 스크립트끼리 데이터(프리셋 표 등)를 나눠 쓰는 용도다. 경로는 ScriptPath와 같은 방식(Content/...)이다.
		// 같은 파일은 한 번만 실행해서 값을 공유하고, 파일이 바뀌면(핫 리로드) 다시 읽는다.
		Lua.set_function("Require", [](sol::this_state State, const std::string& Path) -> sol::object
		                 {
			sol::state_view View(State);
			// nil을 sol::table로 바로 받으면 타입 오류(sol::error)가 나므로 optional로 받는다.
			sol::table Cache;
			if (sol::optional<sol::table> Existing = View["__ModuleCache"])
				Cache = *Existing;
			else
			{
				Cache = View.create_table();
				View["__ModuleCache"] = Cache;
			}

			const fs::path File = FLuaScriptManager::ToPath(Path);
			std::error_code Ec;
			const long long Stamp = static_cast<long long>(fs::last_write_time(File, Ec).time_since_epoch().count());
			if (Ec)
				throw sol::error("Require: cannot open " + Path);

			if (sol::optional<sol::table> Entry = Cache[Path])
				if (Entry->get<long long>("Stamp") == Stamp)
					return Entry->get<sol::object>("Value");

			std::ifstream Stream(File, std::ios::binary);
			if (!Stream)
				throw sol::error("Require: cannot read " + Path);
			const std::string Code((std::istreambuf_iterator<char>(Stream)), std::istreambuf_iterator<char>());

			// 모듈은 자기만의 환경에서 실행한다 (전역은 그대로 보인다).
			sol::environment Env(View, sol::create, View.globals());
			sol::protected_function_result Result = View.safe_script(Code, Env, sol::script_pass_on_error, "@" + Path);
			if (!Result.valid())
			{
				sol::error Err = Result;
				throw sol::error("Require: " + Path + " failed\n" + Err.what());
			}
			sol::object Value = Result.get<sol::object>();
			sol::table NewEntry = View.create_table();
			NewEntry["Stamp"] = Stamp;
			NewEntry["Value"] = Value;
			Cache[Path] = NewEntry;
			return Value; });
	}

	void RegisterWidget(sol::state& Lua)
	{
		Lua.new_usertype<FLuaWidgetRef>("Widget", sol::no_constructor, "IsValid", [](const FLuaWidgetRef& R)
		                                { return R.Get() != nullptr; }, "Name", sol::property([](const FLuaWidgetRef& R)
		                                                                                      { return DerefWidget<UWidget>(R).WidgetName; }),
		                                "Visible", sol::property([](const FLuaWidgetRef& R)
		                                                         { return DerefWidget<UWidget>(R).IsVisible(); }, [](const FLuaWidgetRef& R, bool b)
		                                                         { DerefWidget<UWidget>(R).Visibility = b ? 0 : 1; }),
		                                "Find", [](const FLuaWidgetRef& R, const std::string& Name) -> sol::optional<FLuaWidgetRef>
		                                {
				UWidget* Found = DerefWidget<UUserWidget>(R).FindWidget(Name);
				return Found ? sol::optional(FLuaWidgetRef::Make(Found)) : sol::nullopt; }, "AddToViewport", [](const FLuaWidgetRef& R, sol::optional<int> Z)
		                                { DerefWidget<UUserWidget>(R).AddToViewport(Z.value_or(0)); }, "RemoveFromViewport", [](const FLuaWidgetRef& R)
		                                { DerefWidget<UUserWidget>(R).RemoveFromViewport(); }, "Destroy", [](const FLuaWidgetRef& R)
		                                { DerefWidget<UUserWidget>(R).Destroy(); }, "SetText", [](const FLuaWidgetRef& R, const std::string& T)
		                                { DerefWidget<UTextBlock>(R).SetText(T); }, "BindClick", [](sol::this_environment Env, const FLuaWidgetRef& R, sol::protected_function Fn)
		                                {
				AActor* Owner = OwnerFromEnv(Env);
				if (!Owner) throw sol::error("BindClick needs an Actor script context.");
				UButton& Button = DerefWidget<UButton>(R);
				const FLuaActorRef OwnerRef = FLuaActorRef::Make(Owner);
				Button.OnClicked = [OwnerRef, Fn]
					{
						if (!OwnerRef.Get()) return;              // 액터가 이미 지워졌다
						sol::protected_function_result Result = Fn();
						if (!Result.valid())
						{
							sol::error Err = Result;
							KRS_LOG(Error, "[Lua] OnClicked: {}", Err.what());
						}
					};
				TrackClickBinding(Owner, R); });

		Lua.set_function("CreateWidget", [](sol::this_environment Env, const std::string& Path) -> sol::optional<FLuaWidgetRef>
		                 {
				AActor* Owner = OwnerFromEnv(Env);
				UUserWidget* W = Owner ? ::CreateWidget(Owner->GetWorld(), Path) : nullptr;
				return W ? sol::optional(FLuaWidgetRef::Make(W)) : sol::nullopt; });

		// 진행 바(UProgressBar)를 0~1로 채운다.
		sol::usertype<FLuaWidgetRef> WidgetType = Lua["Widget"];
		WidgetType["SetPercent"] = [](const FLuaWidgetRef& R, float Percent)
		{ DerefWidget<UProgressBar>(R).SetPercent(Percent); };
		// 글자(TextBlock)의 색, 또는 진행 바의 채움 색을 바꾼다. 값은 0~1.
		WidgetType["SetColor"] = [](const FLuaWidgetRef& R, float Red, float Green, float Blue, sol::optional<float> Alpha)
		{
			UWidget* W = R.Get();
			const FLinearColor Color(Red, Green, Blue, Alpha.value_or(1.0f));
			if (auto* Text = Cast<UTextBlock>(W))
				Text->ColorAndOpacity = Color;
			else if (auto* Bar = Cast<UProgressBar>(W))
				Bar->SetFillColorAndOpacity(Color);
			else
				throw sol::error("SetColor works on a TextBlock or a ProgressBar.");
		};
	}

	void RegisterCoroutine(sol::state& Lua)
	{
		Lua.set_function("StartCoroutine",
		                 [](sol::this_environment ThisEnv, sol::function Fn)
		                 {
			                 UWorld* World = WorldFromEnv(ThisEnv, "StartCoroutine");
			                 World->GetCoroutineScheduler().Start(OwnerFromEnv(ThisEnv), Fn);
		                 });

		// C++ 함수가 반환값을 yield 값으로 남기고 멈춘다. 코루틴 밖에서 부르면 Lua 에러
		Lua.set_function("wait", sol::yielding(
		                             [](float Seconds)
		                             {
			                             return std::make_tuple("time", Seconds);
		                             }));
		Lua.set_function("wait_frame", sol::yielding(
		                                   []()
		                                   {
			                                   return;
		                                   }));
		Lua.set_function("wait_until", sol::yielding(
		                                   [](sol::function Cond)
		                                   {
			                                   return std::make_tuple("until", Cond);
		                                   }));
		Lua.set_function("wait_until_move_done", sol::yielding(
		                                             []()
		                                             {
			                                             return "move";
		                                             }));

		Lua.set_function("move_to",
		                 [](sol::this_environment ThisEnv, const FVector& Target, float Speed)
		                 {
			                 UWorld* World = WorldFromEnv(ThisEnv, "move_to");
			                 World->GetCoroutineScheduler().MoveTo(OwnerFromEnv(ThisEnv), Target, Speed);
		                 });
	}
}

namespace FLuaBindings
{
	void Register(sol::state& Lua)
	{
		using namespace LuaBindingsDetail;
		// TODO(B): wait, wait_until, wait_until_move_done, StartCoroutine
		RegisterMath(Lua);
		RegisterActor(Lua);
		RegisterComponent(Lua);
		RegisterInput(Lua);
		RegisterCoroutine(Lua);
		RegisterWidget(Lua);
		RegisterGameplay(Lua);
		RegisterAudio(Lua);
		RegisterCameraAction(Lua);
		RegisterAnimation(Lua);
		RegisterPhysics(Lua);
		RegisterVehicle(Lua);
	}

	void FlushPendingDestroy(UWorld* World)
	{
		TArray<FLuaActorRef> ToDestroy = std::move(PendingDestroies);
		PendingDestroies = TArray<FLuaActorRef>();

		for (const FLuaActorRef& Ref : ToDestroy)
		{
			if (Ref.World != World)
			{
				continue;
			}
			if (AActor* Actor = Ref.Get()) // 살아 있는 액터만 Destroy
			{
				Actor->Destroy();
			}
		}
	}

	void ReleaseCallbacks(AActor* Owner)
	{
		for (int32 i = ClickBindings.Num() - 1; i >= 0; --i)
		{
			if (ClickBindings[i].Owner != Owner)
				continue;
			if (UButton* B = Cast<UButton>(ClickBindings[i].Button.Get()))
				B->OnClicked = nullptr;
			ClickBindings.RemoveAt(i);
		}

		LuaBindingsDetail::ReleaseAnimGraphBindings(Owner);
		LuaBindingsDetail::ReleasePhysicsBindings(Owner);
	}
}
