// Shift Happens - Game Off 2026

#include "Game/ShiftPlayerController.h"

#include "Audio/ShiftAudio.h"
#include "Audio/ShiftAudioMeter.h"
#include "AudioDevice.h"
#include "Behavior/ShiftPolicies.h"
#include "Behavior/StateTree/ShiftBehavior.h"
#include "Board/ShiftBoard.h"
#include "Board/ShiftPalette.h"
#include "Camera/CameraTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/StringConv.h"
#include "Containers/UnrealString.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/EngineTypes.h"
#include "Engine/GameViewportClient.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Framework/Application/SlateApplication.h"
#include "Game/ShiftContent.h"
#include "Game/ShiftResults.h"
#include "ShiftGlass.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "GenericPlatform/GenericWindow.h"
#include "HAL/FileManager.h"
#include "HAL/Platform.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "InputCoreTypes.h"
#include "Jams/ShiftJamPack.h"
#include "Logging/LogMacros.h"
#include "Math/Color.h"
#include "Math/MathFwd.h"
#include "Math/RandomStream.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/Build.h"
#include "Misc/CommandLine.h"
#include "Misc/CString.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ShiftAchievements.h"
#include "ShiftBuild.h"
#include "ShiftForecast.h"
#include "ShiftHappens.h"
#include "ShiftLesson.h"
#include "ShiftRecording.h"
#include "ShiftRun.h"
#include "Sim/ShiftBuildRules.h"
#include "Sim/ShiftGraph.h"
#include "Sim/ShiftParams.h"
#include "Sim/ShiftSim.h"
#include <string>
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"
#include "Templates/UnrealTemplate.h"
#include "TimerManager.h"
#include "Trailer/ShiftTrailer.h"
#include "UI/ShiftHudModel.h"
#include "UI/SShiftHud.h"
#include "UnrealClient.h"
#include "UObject/UObjectGlobals.h"
#include <vector>
#include "Widgets/DeclarativeSyntaxSupport.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Editor/EditorEngine.h"
#endif

namespace
{
	FLinearColor TealColor() { return FLinearColor::FromSRGBColor(FColor(0x1F, 0xB6, 0xA6)); }
	FLinearColor AmberColor() { return FLinearColor::FromSRGBColor(FColor(0xF2, 0xB1, 0x34)); }
	FLinearColor PurpleColor() { return FLinearColor::FromSRGBColor(FColor(0x7B, 0x5C, 0xF0)); }
	FLinearColor RedColor() { return FLinearColor::FromSRGBColor(FColor(0xFF, 0x4D, 0x5A)); }
	FLinearColor OrangeColor() { return FLinearColor::FromSRGBColor(FColor(0xE8, 0x68, 0x1C)); }
	FLinearColor DeepRedColor() { return FLinearColor::FromSRGBColor(FColor(0xC8, 0x2A, 0x3C)); }

	FString Ordinal(int32 N)
	{
		const int32 Mod100 = N % 100;
		const TCHAR* Suffix = TEXT("th");
		if (Mod100 < 11 || Mod100 > 13)
		{
			switch (N % 10)
			{
			case 1: Suffix = TEXT("st"); break;
			case 2: Suffix = TEXT("nd"); break;
			case 3: Suffix = TEXT("rd"); break;
			default: break;
			}
		}
		return FString::Printf(TEXT("%d%s"), N, Suffix);
	}

	FString Replace(FString Text, const TCHAR* Key, int32 Value)
	{
		Text.ReplaceInline(Key, *FString::FromInt(Value));
		return Text;
	}

	const TCHAR* JamSubtitle(int32 Jam)
	{
		switch (Jam)
		{
		case 0: return TEXT("A cosy 36-voter jam. The rival has 3K followers. Learn the ropes, then try the night shift.");
		case 1: return TEXT("52 voters, streamers behind gates and a Mod Eye that sweeps. The rival has 48K followers.");
		default: return TEXT("72 voters, three celebrities and a Mega Campaign in the last days. The rival has 1.2M followers.");
		}
	}

	const TCHAR* RivalFollowers(int32 Jam)
	{
		switch (Jam)
		{
		case 0: return TEXT("3K");
		case 1: return TEXT("48K");
		default: return TEXT("1.2M");
		}
	}

	// Tutorial cards, in Data/Tutorial.csv trigger names; the bit index is the card's index here.
	enum ETut : int32
	{
		TutJamStart,
		TutFirstHover,
		TutFirstPlug,
		TutFirstConvictionRing,
		TutPostsLow,
		TutFirstReinforce,
		TutFirstGateSeen,
		TutFirstGateOpened,
		TutShiftUnlocked,
		TutFirstHeat,
		TutFirstModEye,
		TutFirstStrike,
		TutBlameAvailable,
		TutKeysHint,
		// appended after the first release, so the seen-bits saved in the player's settings keep their meaning
		TutFirstLostVoter,
		TutFirstSockpuppet,
		TutFinalAudit,
		TutRally,
		TutThreat,
		TutEyeAway,
		TutFake,
		TutPush,
		// appended in round 5: the Blame is a last chance now (the old BlameAvailable card, bit 12, is retired and never shown)
		TutBlameRule,
		// appended in round 8: the other entries fight too
		TutFirstLead,
		TutPackHit,
		// appended in round 9: a runaway leader loses voters to the runner-up
		TutSympathy,
		// appended in round 11: a colour you took a voter from comes for it
		TutGrudge,
		TutCount
	};

	const TCHAR* const TutTriggers[TutCount] =
	{
		TEXT("JamStart"), TEXT("FirstHover"), TEXT("FirstPlug"), TEXT("FirstConvictionRing"), TEXT("PostsLow"), TEXT("FirstReinforce"),
		TEXT("FirstGateSeen"), TEXT("FirstGateOpened"), TEXT("ShiftUnlocked"), TEXT("FirstHeat"), TEXT("FirstModEye"), TEXT("FirstStrike"),
		TEXT("BlameAvailable"), TEXT("KeysHint"), TEXT("FirstLostVoter"), TEXT("FirstSockpuppet"), TEXT("FinalAudit"), TEXT("RallyHint"), TEXT("ThreatRing"), TEXT("EyeAway"), TEXT("FirstFake"), TEXT("FinalPush"), TEXT("BlameRule"), TEXT("FirstLead"), TEXT("PackHit"), TEXT("Sympathy"), TEXT("Grudge"),
	};

	enum ETutEvent : uint32
	{
		EvPlug = 1u << 0,
		EvReinforce = 1u << 1,
		EvGated = 1u << 2,
		EvStrike = 1u << 3,
		EvLost = 1u << 4,
		EvSock = 1u << 5,
		EvTelegraph = 1u << 6,
		EvEyeAway = 1u << 7,
		EvStrikeLast = 1u << 8,
		EvPackHit = 1u << 9,
	};
}

AShiftPlayerController::AShiftPlayerController()
{
	bShowMouseCursor = true;
	PrimaryActorTick.bCanEverTick = true;
}

uint32 AShiftPlayerController::NewSeed() const
{
	return bFixedSeed ? Seed : static_cast<uint32>((FDateTime::UtcNow().GetTicks() / 10000) & 0x7FFFFFFF);
}

void AShiftPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (ShiftGlass::IsActive())
	{
		// the see-through window: the engine's scene alpha is inverse opacity, the desktop compositor wants opacity (the colour is already premultiplied)
		// Set in code, not with console commands (the ShowFlag command is not in Shipping) and not only from the ini: a Shipping build must not depend on a saved Engine.ini.
		auto SetCvar = [](const TCHAR* Name, float Value)
			{
				if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
				{
					Var->Set(Value, ECVF_SetByCode);
				}
			};
		SetCvar(TEXT("r.PostProcessing.PropagateAlpha"), 1.0f);
		SetCvar(TEXT("r.TransparentViewportClear"), 1.0f);
		// The engine's last-pass alpha invert draws at the wrong place when the view is letterboxed (it ignores the output view rect); the upscale pass does the same invert
		// correctly, and it runs when the scene is rendered a hair smaller than the window.
		SetCvar(TEXT("r.ScreenPercentage"), 99.0f);
		UE_LOG(LogShift, Display, TEXT("Glass: the game inverts the scene alpha for the desktop compositor"));
	}

	const TCHAR* Cmd = FCommandLine::Get();
	bool bQaRun = false;

	// -ShiftReplay=<file>: replays a recording (the jam, seed and build it was played with, its clicks where they were made); -ShiftRecord records a QA run too.
	bForceRecord = FParse::Param(Cmd, TEXT("ShiftRecord"));
	FString ReplayFile;
	if (FParse::Value(Cmd, TEXT("ShiftReplay="), ReplayFile, false))
	{
		FString Error;
		if (FShiftRecording::Load(ReplayFile, ReplaySource, Error))
		{
			bReplayMode = true;
			bQaRun = true;
			JamIndex = ReplaySource.Jam;
			Seed = ReplaySource.Seed;
			bFixedSeed = true;
			Build.Quality = ReplaySource.Quality;
			Build.Thumbnail = ReplaySource.Thumbnail;
			Build.Network = ReplaySource.Network;
			UE_LOG(LogShift, Display, TEXT("Replay: %s (jam %d, seed %u, build %d/%d/%d, %d clicks, recorded result %s, tag %s)"), *ReplayFile, ReplaySource.Jam, ReplaySource.Seed, ReplaySource.Quality, ReplaySource.Thumbnail, ReplaySource.Network, ReplaySource.Events.Num(), *ReplaySource.Result, *ReplaySource.Tag);
		}
		else
		{
			UE_LOG(LogShift, Error, TEXT("Replay: %s"), *Error);
		}
	}

	int32 SeedArg = 7;
	if (FParse::Value(Cmd, TEXT("ShiftSeed="), SeedArg))
	{
		Seed = static_cast<uint32>(SeedArg);
		bFixedSeed = true;
		bQaRun = true;
	}
	if (FParse::Value(Cmd, TEXT("ShiftJam="), JamIndex))
	{
		bQaRun = true;
	}
	JamIndex = FMath::Clamp(JamIndex, 0, 2);
	FParse::Value(Cmd, TEXT("ShiftNight="), NightOverride);

	// -ShiftLesson=<chapter>[:<step>] starts a guided lesson directly (a QA run: nothing is saved); -ShiftLessonAuto performs every step by itself and exits when it is done
	int32 LessonFlagChapter = 0;
	int32 LessonFlagStep = 1;
	FString LessonFlag;
	if (FParse::Value(Cmd, TEXT("ShiftLesson="), LessonFlag, false))
	{
		FString Chap, Stp;
		if (!LessonFlag.Split(TEXT(":"), &Chap, &Stp))
		{
			Chap = LessonFlag;
			Stp = TEXT("1");
		}
		LessonFlagChapter = FMath::Clamp(FCString::Atoi(*Chap), 1, FShiftLesson::ChapterCount());
		LessonFlagStep = FMath::Max(1, FCString::Atoi(*Stp));
		bQaRun = true;
	}
	bLessonAuto = FParse::Param(Cmd, TEXT("ShiftLessonAuto"));
	if (bLessonAuto && NightOverride < 0.0f)
	{
		NightOverride = 0.0f;
	}
	bQaRun |= FParse::Param(Cmd, TEXT("ShiftBot")) || FString(Cmd).Contains(TEXT("ShiftBot=")) || FString(Cmd).Contains(TEXT("ShiftClicks="))
		|| FString(Cmd).Contains(TEXT("ShiftWarp=")) || FString(Cmd).Contains(TEXT("ShiftSpeed=")) || FParse::Param(Cmd, TEXT("ShiftTrailer"))
		|| FString(Cmd).Contains(TEXT("ShiftTrailer="));
	if (!bFixedSeed)
	{
		Seed = bQaRun ? 7u : NewSeed();
	}

	FString BuildText;
	if (FParse::Value(Cmd, TEXT("ShiftBuild="), BuildText, false))
	{
		TArray<FString> Parts;
		BuildText.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() == 3)
		{
			Build.Quality = FMath::Clamp(FCString::Atoi(*Parts[0]), 0, 8);
			Build.Thumbnail = FMath::Clamp(FCString::Atoi(*Parts[1]), 0, 8);
			Build.Network = FMath::Clamp(FCString::Atoi(*Parts[2]), 0, 8);
		}
	}

	bPseudoCursor = FParse::Param(Cmd, TEXT("ShiftPseudoCursor"));
	bSystemCursor = FParse::Param(Cmd, TEXT("ShiftSystemCursor"));

	FString CursorText;
	// bShouldStopOnSeparator = false: otherwise the value would stop at the comma.
	if (FParse::Value(Cmd, TEXT("ShiftCursor="), CursorText, false))
	{
		TArray<FString> Parts;
		CursorText.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() == 2)
		{
			FakeCursorDesign = FVector2D(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]));
			bFakeCursor = true;
		}
	}

	// -ShiftClicks="T,X,Y[,n];T,X,Y": scripted board clicks in design pixels at game-time T seconds (n = night/sockpuppet click).
	FString ClickText;
	if (FParse::Value(Cmd, TEXT("ShiftClicks="), ClickText, false))
	{
		TArray<FString> Items;
		ClickText.ParseIntoArray(Items, TEXT(";"));
		for (const FString& Item : Items)
		{
			TArray<FString> F;
			Item.ParseIntoArray(F, TEXT(","));
			if (F.Num() >= 3)
			{
				ScriptedClicks.Add({ FCString::Atof(*F[0]), FVector2D(FCString::Atof(*F[1]), FCString::Atof(*F[2])), F.Num() >= 4 && F[3] == TEXT("n") });
			}
		}
		ScriptedClicks.Sort([](const FScriptedClick& A, const FScriptedClick& B) { return A.Time < B.Time; });
		UE_LOG(LogShift, Log, TEXT("%d scripted clicks queued"), ScriptedClicks.Num());
	}

	// -ShiftUi="T,Id[,V];...": scripted UI actions (button ids from EShiftUi, slider value 0..1) at game-time T seconds.
	FString UiText;
	if (FParse::Value(Cmd, TEXT("ShiftUi="), UiText, false))
	{
		TArray<FString> Items;
		UiText.ParseIntoArray(Items, TEXT(";"));
		for (const FString& Item : Items)
		{
			TArray<FString> F;
			Item.ParseIntoArray(F, TEXT(","));
			if (F.Num() >= 2)
			{
				ScriptedUi.Add({ FCString::Atof(*F[0]), FCString::Atoi(*F[1]), F.Num() >= 3 ? FCString::Atof(*F[2]) : 0.0f });
			}
		}
		ScriptedUi.Sort([](const FScriptedUi& A, const FScriptedUi& B) { return A.Time < B.Time; });
		UE_LOG(LogShift, Log, TEXT("%d scripted UI actions queued"), ScriptedUi.Num());
	}

	// -ShiftMouse="T,X,Y,m|d|u;...": mouse events injected into the HUD widget (design pixels), to test hit regions and slider dragging.
	FString MouseText;
	if (FParse::Value(Cmd, TEXT("ShiftMouse="), MouseText, false))
	{
		TArray<FString> Items;
		MouseText.ParseIntoArray(Items, TEXT(";"));
		for (const FString& Item : Items)
		{
			TArray<FString> F;
			Item.ParseIntoArray(F, TEXT(","));
			if (F.Num() >= 4)
			{
				FScriptedMouse Event;
				Event.Time = FCString::Atof(*F[0]);
				Event.Design = FVector2f(FCString::Atof(*F[1]), FCString::Atof(*F[2]));
				Event.Kind = F[3] == TEXT("d") ? 1 : (F[3] == TEXT("u") ? 2 : (F[3] == TEXT("rd") ? 3 : (F[3] == TEXT("ru") ? 4 : (F[3] == TEXT("rm") ? 5 : 0))));   // m move, d / u left down / up, rd / ru right down / up, rm move with the right button held
				ScriptedMouse.Add(Event);
			}
		}
		ScriptedMouse.Sort([](const FScriptedMouse& A, const FScriptedMouse& B) { return A.Time < B.Time; });
		UE_LOG(LogShift, Log, TEXT("%d scripted mouse events queued"), ScriptedMouse.Num());
	}

	bCoachEnabled = FParse::Param(Cmd, TEXT("ShiftTutorial")) || (!bQaRun && !FParse::Param(Cmd, TEXT("ShiftNoTutorial")));
	bCoachPersist = !FParse::Param(Cmd, TEXT("ShiftTutorial"));
	CoachSeenMask = bCoachPersist ? ShiftSave::TutorialSeenMask() : 0;

	FParse::Value(Cmd, TEXT("ShiftMasterVolume="), QaMasterVolume);
	FParse::Value(Cmd, TEXT("ShiftAudioMeter="), QaAudioMeterSpec, false);
	bAutoPauseOnFocusLoss = !bQaRun && !FParse::Param(Cmd, TEXT("ShiftNoAutoPause"));
	bQaRunFlag = bQaRun;
	bEffectsOn = ShiftSave::IsEffectsOn();
	bMusicOn = ShiftSave::IsMusicOn();
	bForceNoSfx = FParse::Param(Cmd, TEXT("ShiftNoSfx"));
	bForceNoMusic = FParse::Param(Cmd, TEXT("ShiftNoMusic"));
	bLessonNudge = FParse::Param(FCommandLine::Get(), TEXT("ShiftNudge")) || (!bQaRun && (ShiftSave::LessonsDone() & 1) == 0);
	EdgeMode = ShiftSave::EdgeMode();
	bMarkers = ShiftSave::GetFlag(TEXT("Markers")) || FParse::Param(FCommandLine::Get(), TEXT("ShiftMarkers"));
	bLargeText = ShiftSave::GetFlag(TEXT("LargeText")) || FParse::Param(FCommandLine::Get(), TEXT("ShiftLargeText"));
	bReduceMotion = ShiftSave::GetFlag(TEXT("ReduceMotion")) || FParse::Param(FCommandLine::Get(), TEXT("ShiftCalm"));
	bGlassSaved = ShiftSave::GlassWindow();
	GlassOpacityPct = ShiftGlass::CommandLineOpacity() >= 0.0f ? FMath::RoundToInt(ShiftGlass::CommandLineOpacity() * 100.0f) : ShiftSave::GlassOpacity();
	LeaderboardMode = ShiftSave::LeaderboardMode();
	ApplySoundSettings();

	FShiftContent::EnsureLoaded();
	RefreshJamCards();

	FString ScreenArg;
	EShiftScreen Start = bQaRun ? EShiftScreen::Play : EShiftScreen::Title;
	if (FParse::Value(Cmd, TEXT("ShiftScreen="), ScreenArg))
	{
		if (ScreenArg.Equals(TEXT("title"), ESearchCase::IgnoreCase)) { Start = EShiftScreen::Title; }
		else if (ScreenArg.Equals(TEXT("jams"), ESearchCase::IgnoreCase)) { Start = EShiftScreen::JamSelect; }
		else if (ScreenArg.Equals(TEXT("build"), ESearchCase::IgnoreCase)) { Start = EShiftScreen::Build; }
		else if (ScreenArg.Equals(TEXT("play"), ESearchCase::IgnoreCase)) { Start = EShiftScreen::Play; }
	}

	if (LessonFlagChapter > 0)
	{
		StartLesson(LessonFlagChapter, LessonFlagStep);
	}
	else if (Start == EShiftScreen::Play)
	{
		Screen = EShiftScreen::Play;
		StartRun(JamIndex, Seed, false);
	}
	else
	{
		EnterScreen(Start);
	}

	if (!QaAudioMeterSpec.IsEmpty())
	{
		// Audio QA: plays the MetaSound sources one by one with the component volume near zero and logs their in-graph meters (no recording).
		AudioMeter = NewObject<UShiftAudioMeter>(this);
		AudioMeter->Begin(GetWorld(), QaAudioMeterSpec);
	}
	else if (!FParse::Param(Cmd, TEXT("ShiftNoAudio")))
	{
		Audio = NewObject<UShiftAudio>(this);
		Audio->Init(GetWorld());
		// QA / capture: -ShiftSfxOff=Name,Name never plays those effects (names as in EShiftSfx: ModWarn, Heartbeat, Hover, ...); -ShiftTension=0..1 pins the music's tension layer
		FString SfxOffText;
		if (FParse::Value(Cmd, TEXT("ShiftSfxOff="), SfxOffText, false))
		{
			TArray<FString> Names;
			SfxOffText.ParseIntoArray(Names, TEXT(","));
			uint32 OffMask = 0;
			for (const FString& Name : Names)
			{
				for (int32 i = 0; i < static_cast<int32>(EShiftSfx::Count); ++i)
				{
					if (Name.TrimStartAndEnd().Equals(ShiftSfxName(static_cast<EShiftSfx>(i)), ESearchCase::IgnoreCase))
					{
						OffMask |= 1u << i;
					}
				}
			}
			Audio->SetSfxOffMask(OffMask);
		}
		float TensionPinValue = -1.0f;
		if (FParse::Value(Cmd, TEXT("ShiftTension="), TensionPinValue))
		{
			Audio->SetTensionPin(TensionPinValue);
		}
		ApplySoundSettings();
		if (FParse::Param(FCommandLine::Get(), TEXT("ShiftMixTap")))
		{
			Audio->BeginMixTap();
		}
	}

	if (!FParse::Param(Cmd, TEXT("ShiftNoUI")) && GEngine && GEngine->GameViewport)
	{
		Hud = SNew(SShiftHud)
			.OnClick(FOnShiftHudClick::CreateUObject(this, &AShiftPlayerController::HandleHudClick))
			.OnUi(FOnShiftHudUi::CreateUObject(this, &AShiftPlayerController::HandleUi))
			.OnKey(FOnShiftHudKey::CreateUObject(this, &AShiftPlayerController::HandleHudKey))
			.OnPan(FOnShiftHudPan::CreateUObject(this, &AShiftPlayerController::HandleHudPan));
		GEngine->GameViewport->AddViewportWidgetContent(Hud.ToSharedRef(), 10);

		FInputModeGameAndUI Mode;
		Mode.SetWidgetToFocus(Hud);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		SetInputMode(Mode);
	}

	ScheduleScreenshotFromCommandLine();

#if !UE_BUILD_SHIPPING
	if (FParse::Param(Cmd, TEXT("ShiftTrailer")) || FString(Cmd).Contains(TEXT("ShiftTrailer=")))   // plain -ShiftTrailer, or -ShiftTrailer=<cue file>
	{
		StartTrailer();   // the scripted trailer run (not in Shipping builds) (Trailer/ShiftTrailer.h)
	}
#endif
}

void AShiftPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Hud.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Hud.ToSharedRef());
	}
	Hud.Reset();
	Run.Reset();
	ForecastService.Cancel();
	AudioMeter = nullptr;
	if (Audio)
	{
		Audio->Shutdown();
		Audio = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// flow

void AShiftPlayerController::StartRun(int32 InJamIndex, uint32 InSeed, bool bInAttract)
{
	SaveRecording(TEXT("abandoned"));   // the jam that is being replaced, if it was left unfinished
	bAttract = bInAttract;
	const bool bLesson = bLessonRun && !bInAttract && Lesson.IsValid();   // a guided lesson: the practice board of ShiftLesson.h
	const int32 RunJam = FMath::Clamp(InJamIndex, 0, 2);
	if (!bAttract)
	{
		JamIndex = RunJam;
		Seed = InSeed;
	}

	Run = MakeShared<FShiftRun>();
	FShiftLessonSetup LessonSetup;
	if (bLesson)
	{
		FShiftLesson::Configure(Lesson->Chapter(), LessonSetup);
	}
	FString SetArg;
	if (!bAttract && FParse::Value(FCommandLine::Get(), TEXT("ShiftSet="), SetArg, false))
	{
		// QA: -ShiftSet=Name=Value,Name=Value overrides a sim Param, jam field or policy field by name, like the balance commandlet's -Set (for example BotBlame=0, BlameMaxFake=0.01)
		TMap<FString, double> Overrides;
		TArray<FString> Parts;
		SetArg.ParseIntoArray(Parts, TEXT(","));
		for (const FString& Part : Parts)
		{
			FString Key, Value;
			if (Part.Split(TEXT("="), &Key, &Value))
			{
				Overrides.Add(Key.TrimStartAndEnd(), FCString::Atod(*Value));
			}
		}
		Run->SetOverrides(Overrides);
	}
	if (bLesson)
	{
		Run->SetOverrides(LessonSetup.Overrides);   // the lesson's world, whatever -ShiftSet said
	}
	if (!Run->Start(RunJam, InSeed, bAttract ? FShiftBuild() : (bLesson ? LessonSetup.Build : Build), /*bInModsEnabled*/ bLesson ? LessonSetup.bMods : true))
	{
		Run.Reset();
		return;
	}
	CosmeticRng.Initialize(static_cast<int32>(InSeed * 7919u + 13u));
	if (!bLesson && !FParse::Param(FCommandLine::Get(), TEXT("ShiftNoStateTree")))
	{
		Run->SetDriver(FShiftBehavior::Create(*Run));   // null (native policies) when the StateTree assets are missing
	}
	bRecordingSaved = false;
	RecordingPath.Empty();
	if (!bAttract)
	{
		if (bReplayMode)
		{
			Run->SetReplay(ReplaySource);
		}
		else if (!bLesson && (!bQaRunFlag || bForceRecord))
		{
			Run->StartRecording();   // every jam a person plays is recorded: a few kilobytes that replay it
		}
	}

	float Warp = 0.0f;
	float Speed = 1.0f;
	if (bAttract)
	{
		Run->SetTimeScale(4.0);
	}
	else
	{
		FString BotName;
		bool bHaveBot = false;
		bool bHaveSpeed = false;
		bool bHaveWarp = false;
#if !UE_BUILD_SHIPPING
		if (bRunOverride)
		{
			// the scripted trailer run (Trailer/ShiftTrailer.h) names the bot, the speed and the warp of each run itself
			BotName = RunOverrideBot;
			Speed = RunOverrideSpeed;
			Warp = RunOverrideWarp;
			bHaveBot = !BotName.IsEmpty();
			bHaveSpeed = Speed != 1.0f;
			bHaveWarp = Warp > 0.0f;
		}
		else
#endif
		{
			bHaveBot = FParse::Value(FCommandLine::Get(), TEXT("ShiftBot="), BotName);
			bHaveSpeed = FParse::Value(FCommandLine::Get(), TEXT("ShiftSpeed="), Speed);
			bHaveWarp = FParse::Value(FCommandLine::Get(), TEXT("ShiftWarp="), Warp);
		}
		if (bHaveBot)
		{
			ShiftSim::BotKind Kind = ShiftSim::BotKind::Idle;
			if (!ShiftSim::BotKindFromName(std::string(TCHAR_TO_UTF8(*BotName)), &Kind))
			{
				Kind = ShiftSim::BotKind::Idle;   // an unknown name plays nothing
			}
			Run->SetBot(Kind);
		}
		if (bHaveSpeed)
		{
			Run->SetTimeScale(Speed);
		}
		if (bHaveWarp)
		{
			for (float T = 0.0f; T < Warp && !Run->IsOver(); T += 0.2f)
			{
				Run->Tick(0.2);
			}
		}
	}
	EventScratch.Reset();
	Run->DrainEvents(EventScratch);
	EventScratch.Reset();

	const ShiftSim::Sim& S = Run->GetSim();
	InitialShares.SetNumZeroed(S.NumEntries());
	for (int32 E = 0; E < S.NumEntries(); ++E)
	{
		InitialShares[E] = static_cast<float>(S.Share(E));
	}

	FShiftContent::BuildIdentities(S, InSeed, NodeHandles, NodeBios);
	if (bLesson)
	{
		Lesson->Attach(*Run);
	}

	if (Board)
	{
		Board->Destroy();
		Board = nullptr;
	}
	Board = GetWorld()->SpawnActor<AShiftBoard>(FVector::ZeroVector, FRotator::ZeroRotator);
	Board->Build(S);
	{
		// QA: where the big voters sit, in the design pixels -ShiftClicks takes
		FString Where;
		for (const int Celeb : S.GetGraph().celebrities)
		{
			const ShiftSim::GraphNode& N = S.GetGraph().nodes[static_cast<size_t>(Celeb)];
			Where += FString::Printf(TEXT(" celebrity #%d (%.0f,%.0f)"), Celeb, N.x, N.y);
		}
		for (const int Hub : S.GetGraph().hubOfCommunity)
		{
			const ShiftSim::GraphNode& N = S.GetGraph().nodes[static_cast<size_t>(Hub)];
			Where += FString::Printf(TEXT(" hub #%d (%.0f,%.0f)"), Hub, N.x, N.y);
		}
		UE_LOG(LogShift, Log, TEXT("Layout:%s"), *Where);
		FString All;
		for (int32 Node = 0; Node < S.NodeCount(); ++Node)
		{
			const ShiftSim::GraphNode& N = S.GetGraph().nodes[static_cast<size_t>(Node)];
			All += FString::Printf(TEXT(" %d%c(%.0f,%.0f)"), Node, N.role == ShiftSim::NodeRole::Celebrity ? TEXT('C') : (N.role == ShiftSim::NodeRole::LocalHub ? TEXT('H') : (N.role == ShiftSim::NodeRole::Bridge ? TEXT('B') : TEXT('m'))), N.x, N.y);
		}
		UE_LOG(LogShift, Log, TEXT("Layout nodes:%s"), *All);
	}

	bResultsBuilt = false;
	ShareHistory.Reset();
	ShareSampleTime = -1.0;
	ResetAlerts();
	bPaused = false;
	bNightLatch = false;
	NightBlend = 0.0f;
	Feed.Reset();
	HoverNode = -1;
	LastLeader = S.Leader();
	LastHeadlineDay = -1;
	RunClock = 0.0f;
	bPlayerActed = false;
	TutEventMask = 0;
	GatedSeenNodes.Reset();
	LostRecently.Reset();
	LostCooldown = 0.0f;
	ChatterQueue.Reset();
	ChatterGap = 0.0f;
	ResetView();
	Ach.Reset();
	AchSaved = FParse::Param(FCommandLine::Get(), TEXT("ShiftAchTest")) ? 0u : ShiftSave::Achievements();
	AchNewThisRun.Reset();
	ExtrasPage = 0;
	IdleClock = 0.0f;
	GateChatted.Reset();
	bAuditWarned = false;
	RallyFxNode = -1;
	RallyFxAge = 99.0f;
	bHowTo = false;
	bSettingsOpen = false;
	LessonAutoTimer = 0.0f;
	CoachQueue.Reset();
	CoachQueuedMask = 0;
	CoachCurrent = -1;
	CoachAlpha = 0.0f;
	CoachCooldown = 0.0f;

	UE_LOG(LogShift, Log, TEXT("Run started: jam=%d (%hs) seed=%u attract=%d nodes=%d edges=%d rival=%.1f%% you=%.1f%% bot=%d speed=%.1f warp=%.0fs build=%d/%d/%d"),
		RunJam, S.Jam().name.c_str(), InSeed, bAttract ? 1 : 0, S.NodeCount(), Board->EdgeCount(), S.Share(S.RivalId()) * 100.0,
		S.Share(ShiftSim::kPlayerId) * 100.0, Run->HasBot() ? 1 : 0, Speed, Warp, Build.Quality, Build.Thumbnail, Build.Network);
}

void AShiftPlayerController::StartLesson(int32 Chapter, int32 FirstStep)
{
	ForecastService.Cancel();
	Lesson = MakeUnique<FShiftLesson>();
	Lesson->Expand = [this](const FString& Text) { return ExpandTutorialText(Text); };
	Lesson->Begin(Chapter, FirstStep);
	bLessonRun = true;
	bLessonDone = false;
	LessonChapter = Lesson->Chapter();
	LessonAutoWall = 0.0f;
	LessonAutoDone = 0.0f;
	bHowTo = false;
	FShiftLessonSetup Setup;
	FShiftLesson::Configure(LessonChapter, Setup);
	Screen = EShiftScreen::Play;
	StartRun(Setup.Jam, Setup.Seed, false);
	if (!Run.IsValid())
	{
		LeaveLesson(EShiftScreen::Title);
	}
}

void AShiftPlayerController::LeaveLesson(EShiftScreen NewScreen)
{
	bLessonRun = false;
	Lesson.Reset();
	bHowTo = false;
	bPaused = false;
	EnterScreen(NewScreen);
}

void AShiftPlayerController::TickLesson(float DeltaTime)
{
	if (!bLessonRun || !Lesson.IsValid() || !Run.IsValid() || Screen != EShiftScreen::Play)
	{
		return;
	}
	if (!Lesson->NightAllowed())
	{
		if (bNightLatch)
		{
			UE_LOG(LogShift, Log, TEXT("Lesson: the night toggle ended with its step"));
		}
		bNightLatch = false;   // a toggled night shift ends with the step that allowed it, it does not come back with the next one
	}
	Lesson->Tick(*Run, DeltaTime, EventScratch, HoverNode);
	const FString Notice = Lesson->ConsumeNotice();
	if (!Notice.IsEmpty())
	{
		PlaySfx(EShiftSfx::Locked);
		ShowToast(Notice, AmberColor(), 3.0f);
	}
	if (Lesson->IsComplete() && !bLessonDone)
	{
		bLessonDone = true;
		PlaySfx(EShiftSfx::Win);
		if (!bQaRunFlag)
		{
			ShiftSave::SetLessonsDone(ShiftSave::LessonsDone() | (1 << (LessonChapter - 1)));
			bLessonNudge = (ShiftSave::LessonsDone() & 1) == 0;
			// the pop-up cards this lesson covered need not say it again
			const int32 Covered = LessonChapter == 1
				? 0xFF | (1 << TutRally) | (1 << TutThreat)
				: (1 << TutShiftUnlocked) | (1 << TutFirstHeat) | (1 << TutFirstModEye) | (1 << TutFirstSockpuppet);
			CoachSeenMask |= Covered;
			if (bCoachPersist)
			{
				ShiftSave::SetTutorialSeenMask(CoachSeenMask);
			}
		}
	}

	if (!bLessonAuto)
	{
		return;
	}
	// QA autoplay: what a player would do, through the same paths a click or a key takes
	LessonAutoWall += DeltaTime;
	if (Lesson->IsComplete())
	{
		LessonAutoDone += DeltaTime;
		if (LessonAutoDone > 0.8f)
		{
			UE_LOG(LogShift, Display, TEXT("Lesson %d autoplay: complete"), LessonChapter);
			FPlatformMisc::RequestExitWithStatus(false, 0);
		}
		return;
	}
	if (LessonAutoWall > 900.0f)
	{
		UE_LOG(LogShift, Error, TEXT("Lesson %d autoplay: timed out at step %d"), LessonChapter, Lesson->View().Step);
		FPlatformMisc::RequestExitWithStatus(false, 4);
		return;
	}
	// a reader takes a moment; a player in the middle of a step acts every half second of the jam's own time, whatever the fast-forward is
	const double SimNow = Run->GetSim().Time();
	if (Lesson->IsFrozen())
	{
		LessonAutoTimer += DeltaTime;
		LessonAutoSim = SimNow;
		if (LessonAutoTimer < 0.4f)
		{
			return;
		}
		LessonAutoTimer = 0.0f;
	}
	else
	{
		if (SimNow - LessonAutoSim < 0.5)
		{
			return;
		}
		LessonAutoSim = SimNow;
	}
	const FShiftLessonAuto Act = Lesson->Auto(Run->GetSim());
	switch (Act.Kind)
	{
	case FShiftLessonAuto::EKind::Continue:
		HandleUi(EShiftUi::LessonContinue, 0.0f);
		break;
	case FShiftLessonAuto::EKind::Hover:
		if (Act.Node >= 0)
		{
			const ShiftSim::GraphNode& N = Run->GetSim().GetGraph().nodes[static_cast<size_t>(Act.Node)];
			if (bPseudoCursor)
			{
				PseudoMoveTo(SimToDesign(N.x, N.y), 0.45f);   // the scripted cursor glides there; TickPseudoCursor drives the fake cursor
			}
			else
			{
				bFakeCursor = true;
				FakeCursorDesign = SimToDesign(N.x, N.y);
			}
		}
		break;
	case FShiftLessonAuto::EKind::Click:
		bPlayerActed = true;
		if (bPseudoCursor && Act.Node >= 0)
		{
			const ShiftSim::GraphNode& N = Run->GetSim().GetGraph().nodes[static_cast<size_t>(Act.Node)];
			PseudoClick(SimToDesign(N.x, N.y), Act.bNight ? 1 : 0);
		}
		Run->QueueAction(Act.Node, Act.bNight);
		break;
	case FShiftLessonAuto::EKind::RightClick:
		bPlayerActed = true;
		if (bPseudoCursor && Act.Node >= 0)
		{
			const ShiftSim::GraphNode& N = Run->GetSim().GetGraph().nodes[static_cast<size_t>(Act.Node)];
			PseudoClick(SimToDesign(N.x, N.y), 2);
		}
		Run->QueueRally(Act.Node);
		break;
	case FShiftLessonAuto::EKind::NightOn:
		NightOverride = -1.0f;
		bNightLatch = true;    // like a person who presses N: the toggle is what the lesson has to cope with
		break;
	case FShiftLessonAuto::EKind::NightOff:
		NightOverride = 0.0f;
		bNightLatch = false;
		break;
	default:
		break;
	}
}

void AShiftPlayerController::StartAttract()
{
	// A new board every launch: the seed starts from the clock. QA runs keep the fixed sequence, so their screenshots stay comparable.
	static uint32 AttractCounter = 0;
	static const uint32 AttractBase = bQaRunFlag ? 1000u : static_cast<uint32>(FDateTime::Now().GetTicks() % 1000000007);
	const int32 AttractJam = Screen == EShiftScreen::Build ? JamIndex : 2;
	StartRun(AttractJam, AttractBase + (AttractCounter++) * 37u, true);
}

void AShiftPlayerController::StartPlay()
{
	ForecastService.Cancel();   // the plays of the forecast must not take the CPU from the jam being played
	Screen = EShiftScreen::Play;
	StartRun(JamIndex, Seed, false);
}

void AShiftPlayerController::RestartRun()
{
	if (Screen != EShiftScreen::Play)
	{
		return;
	}
	if (bLessonRun)
	{
		StartLesson(LessonChapter, 1);   // Restart in a lesson starts the lesson again
		return;
	}
	StartRun(JamIndex, bReplayMode ? Seed : Seed + 1u, false);
}

void AShiftPlayerController::EnterScreen(EShiftScreen NewScreen)
{
	const EShiftScreen Previous = Screen;
	if (Previous == EShiftScreen::Play && NewScreen != EShiftScreen::Play)
	{
		SaveRecording(TEXT("abandoned"));
	}
	Screen = NewScreen;
	bPaused = false;
	bSettingsOpen = false;
	if (Previous != NewScreen)
	{
		PlaySfx(EShiftSfx::Transition);
	}
	switch (Screen)
	{
	case EShiftScreen::Title:
	case EShiftScreen::JamSelect:
		ForecastService.Cancel();
		if (!bAttract || !Run.IsValid() || Previous == EShiftScreen::Play || Previous == EShiftScreen::Build)
		{
			StartAttract();
		}
		if (Screen == EShiftScreen::JamSelect)
		{
			RefreshJamCards();
		}
		break;
	case EShiftScreen::Build:
		WorldQuality = ShiftForecastRun::WorldQualities(JamIndex, Seed);
		Forecast = FShiftForecast();
		bForecastReady = false;
		RefreshForecast();
		StartAttract();
		break;
	case EShiftScreen::Play:
	default:
		break;
	}
}

void AShiftPlayerController::RefreshJamCards()
{
	JamCards.Reset();
	CardJam.Reset();
	LastRegistryVersion = FShiftJamRegistry::Get().Version();

	auto AddCard = [this](int32 Jam, const FString& Title, const FString& Subtitle)
		{
			const ShiftSim::JamDef Def = ShiftSim::DefaultJam(Jam);
			FShiftHudJamCard Card;
			Card.Title = Title;
			Card.Subtitle = Subtitle;
			Card.RivalGame = FShiftContent::EntryName(Jam, 1, Seed);
			Card.RivalAuthor = FShiftContent::EntryAuthor(Jam, 1, Seed);
			Card.RivalAuthor.RemoveFromStart(TEXT("@"));
			Card.RivalBlurb = FShiftContent::EntryBlurb(Jam, 1, Seed);
			Card.Facts.Add(FString::Printf(TEXT("%d voters in %d communities"), Def.nodes, Def.communities));
			Card.Facts.Add(FString::Printf(TEXT("%d celebrity hubs behind gates"), Def.celebs));
			Card.Facts.Add(FString::Printf(TEXT("Mod sweeps every %.0f-%.0f seconds"), Def.modIntervalMin, Def.modIntervalMax));
			Card.Difficulty = Jam + 1;
			Card.BestRank = ShiftSave::BestRank(Jam);
			JamCards.Add(Card);
			CardJam.Add(Jam);
		};

	// The jam select screen lists the jam pack plugins (Game Features) that are active; without any it falls back to the three built-in jams.
	const TArray<const UShiftJamPackData*> Packs = FShiftJamRegistry::Get().Active();
	for (const UShiftJamPackData* Pack : Packs)
	{
		if (Pack && JamCards.Num() < 3)
		{
			AddCard(FMath::Clamp(Pack->JamIndex, 0, 2), Pack->Title, Pack->Subtitle);
		}
	}
	if (JamCards.Num() == 0)
	{
		for (int32 i = 0; i < 3; ++i)
		{
			AddCard(i, FString(UTF8_TO_TCHAR(ShiftSim::DefaultJam(i).name.c_str())), JamSubtitle(i));
		}
	}
}

/** The build screen's forecast of this world: the quality list is exact and instant, the vote list is the average of the plays the service makes in the background (the lists
 *  show the progress until they are in) and is looked up, not recomputed, whenever the sliders move. */
void AShiftPlayerController::RefreshForecast()
{
	ForecastService.Request(JamIndex, Seed);
	ForecastService.Tick();
	const TSharedPtr<const FShiftForecastModel> Model = ForecastService.Find(JamIndex, Seed);
	bForecastReady = Model.IsValid();
	if (Model.IsValid())
	{
		Forecast = Model->Evaluate(Build);
		ForecastSpread = Model->PlayerSpread;
	}
	else
	{
		Forecast = FShiftForecast();
		ShiftForecastRun::FillQuality(JamIndex, WorldQuality, Build, Forecast);
		ForecastSpread = 0.0f;
	}
}

void AShiftPlayerController::SetBuildHours(int32 Slider, float Value01)
{
	int32* Fields[3] = { &Build.Quality, &Build.Thumbnail, &Build.Network };
	if (Slider < 0 || Slider > 2)
	{
		return;
	}
	int32& Field = *Fields[Slider];
	const int32 Others = Build.Spent() - Field;
	const int32 Target = FMath::Clamp(FMath::RoundToInt(Value01 * 8.0f), 0, 8);
	const int32 NewValue = FMath::Max(0, FMath::Min(Target, FShiftBuild::TotalHours - Others));
	if (NewValue != Field)
	{
		Field = NewValue;
		if (SliderSfxCooldown <= 0.0f)
		{
			PlaySfx(EShiftSfx::Slider);
			SliderSfxCooldown = 0.06f;
		}
	}
}

void AShiftPlayerController::SetPaused(bool bInPaused)
{
	if (Screen == EShiftScreen::Play && Run.IsValid() && !Run->IsOver())
	{
		bPaused = bInPaused;
	}
}

void AShiftPlayerController::ApplySoundSettings() const
{
	const bool bEffectsHeard = bEffectsOn && !bForceNoSfx;
	const bool bMusicHeard = bMusicOn && !bForceNoMusic;
	if (Audio)
	{
		Audio->SetEffectsMuted(!bEffectsHeard);
		Audio->SetMusicMuted(!bMusicHeard);
	}
	if (GEngine)
	{
		if (FAudioDevice* Device = GEngine->GetMainAudioDeviceRaw())
		{
			Device->SetTransientPrimaryVolume((bEffectsHeard || bMusicHeard) ? QaMasterVolume : 0.0f);
		}
	}
}

void AShiftPlayerController::ToggleMute()
{
	const bool bAnyOn = bEffectsOn || bMusicOn;
	SetEffectsOn(!bAnyOn);
	SetMusicOn(!bAnyOn);
}

void AShiftPlayerController::SetEffectsOn(bool bOn)
{
	bEffectsOn = bOn;
	ShiftSave::SetEffectsOn(bOn);
	ApplySoundSettings();
}

void AShiftPlayerController::SetMusicOn(bool bOn)
{
	bMusicOn = bOn;
	ShiftSave::SetMusicOn(bOn);
	ApplySoundSettings();
}

int32 AShiftPlayerController::CurrentWindowMode() const
{
	// Alt+Enter and F11 update the game user settings too, so the buttons stay in step with them.
	const UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	const int32 Mode = Settings ? static_cast<int32>(Settings->GetFullscreenMode()) : static_cast<int32>(EWindowMode::Windowed);
	return Mode == 0 && ShiftGlass::IsActive() ? 1 : Mode;   // the swap chain of a see-through window is never exclusive fullscreen
}

void AShiftPlayerController::SetWindowMode(int32 Mode)
{
#if WITH_EDITOR
	if (GetWorld() && GetWorld()->IsPlayInEditor())
	{
		// Inside Play In Editor the game window is the editor itself: changing its mode would act on the editor.
		ShowToast(TEXT("The editor cannot change the window mode. Run the game on its own."), AmberColor(), 2.8f);
		return;
	}
#endif
	UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!Settings)
	{
		return;
	}
	if (Mode == 0 && ShiftGlass::IsActive())
	{
		Mode = 1;   // a see-through window cannot be exclusive fullscreen: borderless fills the screen the same way
		ShowToast(TEXT("Fullscreen becomes borderless while the window is see-through."), AmberColor(), 2.8f);
	}
	const EWindowMode::Type NewMode = EWindowMode::ConvertIntToWindowMode(FMath::Clamp(Mode, 0, 2));
	FIntPoint Desktop = Settings->GetDesktopResolution();
	if (Desktop.X <= 0 || Desktop.Y <= 0)
	{
		Desktop = FIntPoint(1920, 1080);
	}
	// Fullscreen and borderless fill the monitor. A window is the biggest 16:9 size that leaves a margin around it (the game letterboxes to 16:9).
	FIntPoint Size = Desktop;
	if (NewMode == EWindowMode::Windowed)
	{
		static const FIntPoint Sizes[3] = { FIntPoint(2560, 1440), FIntPoint(1920, 1080), FIntPoint(1280, 720) };
		Size = Sizes[2];
		for (const FIntPoint& Candidate : Sizes)
		{
			if (Candidate.X * 10 <= Desktop.X * 9 && Candidate.Y * 10 <= Desktop.Y * 9)
			{
				Size = Candidate;
				break;
			}
		}
	}
	Settings->SetFullscreenMode(NewMode);
	Settings->SetScreenResolution(Size);
	Settings->ApplyResolutionSettings(false);
	if (!bQaRunFlag)
	{
		Settings->SaveSettings();   // QA runs never touch the player's saved settings
	}
	UE_LOG(LogShift, Log, TEXT("Window mode %d: %dx%d (desktop %dx%d)"), Mode, Size.X, Size.Y, Desktop.X, Desktop.Y);
}

bool AShiftPlayerController::IsNightHeldNow() const
{
	if (bLessonRun && Lesson.IsValid() && !Lesson->NightAllowed())
	{
		return false;   // night shift is the second lesson
	}
	return IsNightHeldRaw();
}

bool AShiftPlayerController::IsNightHeldRaw() const
{
	if (Run.IsValid() && Run->IsReplaying())
	{
		return Run->IsNightHeld();
	}
	if (NightOverride >= 0.0f)
	{
		return NightOverride > 0.5f;
	}
	const bool bShiftDown = FSlateApplication::IsInitialized() && FSlateApplication::Get().GetModifierKeys().IsShiftDown();
	return bShiftDown || bNightLatch;
}

FString AShiftPlayerController::NodeHandle(int32 Node) const
{
	return NodeHandles.IsValidIndex(Node) ? NodeHandles[Node] : FString::Printf(TEXT("voter_%03d"), Node);
}

FString AShiftPlayerController::EntryName(int32 EntryId) const
{
	return FShiftContent::EntryName(JamIndex, EntryId, Seed);
}

bool AShiftPlayerController::AchEligible() const
{
	static const bool bSetWorld = []() { FString Value; return FParse::Value(FCommandLine::Get(), TEXT("ShiftSet="), Value, false); }();
	static const bool bAchTest = FParse::Param(FCommandLine::Get(), TEXT("ShiftAchTest"));   // QA: a bot or scripted run counts, nothing is saved (see PollAchievements)
	if (bAchTest)
	{
		return Screen == EShiftScreen::Play && !bAttract && !bLessonRun && !bReplayMode && Run.IsValid();
	}
	return Screen == EShiftScreen::Play && !bAttract && !bQaRunFlag && !bLessonRun && !bReplayMode && !bSetWorld && NightOverride < 0.0f && Run.IsValid() && !Run->HasBot();
}

void AShiftPlayerController::PollAchievements()
{
	const uint32 Fresh = Ach.Mask() & ~AchSaved;
	if (Fresh == 0 || !AchEligible())
	{
		return;
	}
	AchSaved |= Fresh;
	static const bool bAchTest = FParse::Param(FCommandLine::Get(), TEXT("ShiftAchTest"));
	if (!bAchTest)
	{
		ShiftSave::SetAchievements(AchSaved);
	}
	for (int32 I = 0; I < ShiftAch::Num; ++I)
	{
		if ((Fresh & (1u << static_cast<uint32>(I))) == 0)
		{
			continue;
		}
		FString Name, Desc;
		bool bSecret = false;
		UE_LOG(LogShift, Display, TEXT("Achievement: earned %s at t=%.1f"), ShiftAch::IdOf(I), Run->GetSim().Time());
		if (FShiftContent::Achievement(ShiftAch::IdOf(I), Name, Desc, bSecret))
		{
			AchNewThisRun.Add(Name);
			PushFeed(FString::Printf(TEXT("ACHIEVEMENT: %s. %s"), *Name, *Desc), TealColor(), 5.0f, false);
			PlaySfx(EShiftSfx::Tip);
		}
	}
}

void AShiftPlayerController::FinishAchievements()
{
	if (!AchEligible() || !Run.IsValid())
	{
		return;
	}
	const ShiftSim::Sim& S = Run->GetSim();
	const FShiftResultFacts Facts = ShiftResults::MakeFacts(S, Run->GetFlipStats(), Run->GetBlameStats().Taken > 0, InitialShares);
	Ach.OnFinished(Facts, Run->GetFlipStats(), JamIndex);
	UE_LOG(LogShift, Display, TEXT("Achievements: finished jam=%d won=%d dq=%d rank=%d qualityRank=%d cheated=%d rivalCollapsed=%d earned mask=0x%x"), JamIndex, Facts.bWon ? 1 : 0, Facts.bDisqualified ? 1 : 0, Facts.Rank, Facts.QualityRank, Facts.bCheated ? 1 : 0, Facts.bRivalCollapsed ? 1 : 0, Ach.Mask());
	PollAchievements();
	if (!FParse::Param(FCommandLine::Get(), TEXT("ShiftAchTest")))
	{
		ShiftSave::RecordCareer(JamIndex, Facts.bWon, Facts.bDisqualified, static_cast<float>(S.Share(ShiftSim::kPlayerId)));
	}
	ResultsModel.NewAchievements = AchNewThisRun;
}

void AShiftPlayerController::ResetView()
{
	ViewPan = FVector2D::ZeroVector;
	ViewPanTarget = FVector2D::ZeroVector;
	bViewBounds = false;
	FString PanArg;
	TArray<FString> Parts;
	if (FParse::Value(FCommandLine::Get(), TEXT("ShiftPan="), PanArg, false) && PanArg.ParseIntoArray(Parts, TEXT(",")) == 2)
	{
		ViewPanTarget = FVector2D(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]));   // QA: start with the board panned
	}
}

void AShiftPlayerController::UpdateView(float DeltaTime)
{
	if (!Board || !Run.IsValid())
	{
		return;
	}
	if (!ViewCamera.IsValid())
	{
		ViewCamera = GetViewTarget();
		if (ViewCamera.IsValid())
		{
			ViewCameraBase = ViewCamera->GetActorLocation();
		}
	}
	if (!ViewCamera.IsValid())
	{
		return;
	}
	static const bool bQaCentre = FParse::Param(FCommandLine::Get(), TEXT("ShiftCentre"));   // QA runs keep the old framing (their click coordinates are sim coordinates) unless asked
	bool bTrailerOn = false;
#if !UE_BUILD_SHIPPING
	bTrailerOn = Trailer && Trailer->IsActive();   // the trailer moves the camera itself
#endif
	const bool bCentre = !bTrailerOn && (bAttract || !bQaRunFlag || bQaCentre);
	const bool bPan = bCentre && !bAttract;
	FVector2D Centre = FVector2D::ZeroVector;
	if (bCentre)
	{
		const ShiftSim::Params& P = Run->GetSim().GetParams();
		Centre.X = 0.5 * P.LayoutWidth - 0.5 * (P.LayoutMargin + P.LayoutWidth - P.LayoutMarginRight);   // the middle of the part the HUD leaves free, moved to the middle of the screen
	}
	if (bPan && !bViewBounds)
	{
		// how far the board may be panned: every voter stays inside the play field
		const ShiftSim::Sim& S = Run->GetSim();
		float MinX = 1.0e9f, MaxX = -1.0e9f, MinY = 1.0e9f, MaxY = -1.0e9f;
		for (int32 I = 0; I < S.NodeCount(); ++I)
		{
			const ShiftSim::GraphNode& N = S.GetGraph().nodes[static_cast<size_t>(I)];
			const float R = Board->NodeRadius(I);
			MinX = FMath::Min(MinX, static_cast<float>(N.x) - R);
			MaxX = FMath::Max(MaxX, static_cast<float>(N.x) + R);
			MinY = FMath::Min(MinY, static_cast<float>(N.y) - R);
			MaxY = FMath::Max(MaxY, static_cast<float>(N.y) + R);
		}
		const auto Range = [](float Min, float Max, float Shift, float SafeLo, float SafeHi, double& OutLo, double& OutHi)
			{
				double A = SafeLo - (Min + Shift);
				double B = SafeHi - (Max + Shift);
				if (A > B)
				{
					Swap(A, B);   // the board is bigger than the field: it may be moved over its whole extent
				}
				OutLo = A;
				OutHi = B;
			};
		Range(MinX, MaxX, static_cast<float>(Centre.X), 30.0f, 1250.0f, ViewPanLo.X, ViewPanHi.X);
		Range(MinY, MaxY, 0.0f, 60.0f, 660.0f, ViewPanLo.Y, ViewPanHi.Y);
		bViewBounds = true;
	}
	if (bPan)
	{
		ViewPanTarget.X = FMath::Clamp(ViewPanTarget.X, ViewPanLo.X, ViewPanHi.X);
		ViewPanTarget.Y = FMath::Clamp(ViewPanTarget.Y, ViewPanLo.Y, ViewPanHi.Y);
		ViewPan = FMath::Vector2DInterpTo(ViewPan, ViewPanTarget, DeltaTime, 16.0f);
	}
	else
	{
		ViewPan = FVector2D::ZeroVector;
		ViewPanTarget = FVector2D::ZeroVector;
	}
	ViewShift = Centre + ViewPan;
	// a point shown ViewShift to the right and down is a camera moved ViewShift to the left and up: design x is the world's Y, design y is the world's -X
	const FVector Wanted = bTrailerOn ? ViewCamera->GetActorLocation() : ViewCameraBase + FVector(ViewShift.Y, -ViewShift.X, 0.0);
	if (!ViewCamera->GetActorLocation().Equals(Wanted, 0.01f))
	{
		ViewCamera->SetActorLocation(Wanted);
	}
}

void AShiftPlayerController::HandleHudPan(const FVector2f& DeltaDesign)
{
	if (Screen != EShiftScreen::Play || bHowTo || bSettingsOpen || ResultsShown() || bAttract)
	{
		return;
	}
	ViewPanTarget += FVector2D(DeltaDesign.X, DeltaDesign.Y);
	ViewPan = ViewPanTarget;   // dragging has no lag
}

void AShiftPlayerController::QuitGame()
{
#if WITH_EDITOR
	// In Play In Editor this ends the play session. RequestExit would shut the whole editor down with PIE still running, which crashes in EndPlayMap.
	if (GEditor && GetWorld() && GetWorld()->IsPlayInEditor())
	{
		GEditor->RequestEndPlayMap();
		return;
	}
#endif
	FPlatformMisc::RequestExit(false);
}

void AShiftPlayerController::FillAccessModel(FShiftHudModel& M) const
{
	M.bMarkers = bMarkers;
	M.bLargeText = bLargeText;
	M.bReduceMotion = bReduceMotion;
	M.bGlassActive = ShiftGlass::IsActive();
	M.bGlassSaved = bGlassSaved;
	M.GlassOpacity = GlassOpacityPct;
	M.BackgroundOpacity = M.bGlassActive ? static_cast<float>(GlassOpacityPct) / 100.0f : 1.0f;
}

void AShiftPlayerController::FillExtrasModel(FShiftHudModel& M) const
{
	M.ExtrasPage = ExtrasPage;
	if (ExtrasPage == 0)
	{
		return;
	}
	if (ExtrasPage == 1)
	{
		const uint32 Saved = ShiftSave::Achievements();
		for (int32 I = 0; I < ShiftAch::Num; ++I)
		{
			FShiftHudAchItem Item;
			bool bSecret = false;
			if (FShiftContent::Achievement(ShiftAch::IdOf(I), Item.Name, Item.Desc, bSecret))
			{
				Item.bHidden = bSecret;
				Item.bEarned = (Saved & (1u << static_cast<uint32>(I))) != 0;
				M.Achievements.Add(Item);
			}
		}
		for (int32 Jam = 0; Jam < 3; ++Jam)
		{
			FShiftHudCareerRow Row;
			Row.Jam = FString::Printf(TEXT("JAM %d"), Jam + 1);
			Row.Runs = ShiftSave::CareerRuns(Jam);
			Row.Wins = ShiftSave::CareerWins(Jam);
			Row.Dq = ShiftSave::CareerDq(Jam);
			Row.BestRank = ShiftSave::BestRank(Jam);
			Row.BestShare = ShiftSave::CareerBestShare(Jam);
			M.Career.Add(Row);
		}
		return;
	}
	M.CreditsText = {
		TEXT("Project owner:"), TEXT("Hu\u00e2n Ri\u00eau"), TEXT(""),
		TEXT("Tester:"), TEXT("Phuong-Fang"),
	};
}

void AShiftPlayerController::PushFeed(const FString& Text, const FLinearColor& Color, float Seconds, bool bNews, bool bChatter)
{
	for (FFeedItem& Existing : Feed)
	{
		if (Existing.bNews == bNews && Existing.Text == Text)
		{
			Existing.Remaining = Seconds;   // the same line again: keep one card and renew it
			Existing.Duration = FMath::Max(0.1f, Seconds);
			return;
		}
	}
	FFeedItem Item;
	Item.Text = Text;
	Item.Color = Color;
	Item.Remaining = Seconds;
	Item.Duration = FMath::Max(0.1f, Seconds);
	Item.bNews = bNews;
	Item.bInk = bChatter;
	Item.bChatter = bChatter;
	Feed.Insert(Item, 0);
	while (Feed.Num() > 6)
	{
		Feed.Pop();
	}
}

bool AShiftPlayerController::HasActiveToast(float MinRemaining) const
{
	for (const FFeedItem& Item : Feed)
	{
		if (!Item.bNews && !Item.bChatter && Item.Remaining > MinRemaining)
		{
			return true;
		}
	}
	return false;
}

void AShiftPlayerController::ShowToast(const FString& Text, const FLinearColor& Color, float Seconds)
{
	PushFeed(Text, Color, Seconds, false);
}

FString AShiftPlayerController::BuildHeadline(const FString& Event, const FString& Entry, FRandomStream& Rng) const
{
	FString Line = FShiftContent::PickHeadline(Event, Rng);
	if (Line.IsEmpty())
	{
		return Line;
	}
	Line.ReplaceInline(TEXT("{entry}"), *Entry);
	Line.ReplaceInline(TEXT("{you}"), *EntryName(ShiftSim::kPlayerId));
	Line.ReplaceInline(TEXT("{rival}"), *EntryName(Run.IsValid() ? Run->GetSim().RivalId() : 1));
	if (Line.Contains(TEXT("{handle}")))
	{
		const FString Handle = NodeHandles.Num() > 0 ? NodeHandles[Rng.RandRange(0, NodeHandles.Num() - 1)] : FString(TEXT("someone"));
		Line.ReplaceInline(TEXT("{handle}"), *(TEXT("@") + Handle));
	}
	return Line;
}

void AShiftPlayerController::ShowHeadline(const FString& Event, const FString& Entry)
{
	const FString Line = BuildHeadline(Event, Entry, CosmeticRng);
	if (!Line.IsEmpty())
	{
		PushFeed(Line, AmberColor(), 6.5f, true);
	}
}

FString AShiftPlayerController::RandomHandle()
{
	return NodeHandles.Num() > 0 ? NodeHandles[CosmeticRng.RandRange(0, NodeHandles.Num() - 1)] : FString(TEXT("someone"));
}

void AShiftPlayerController::Chatter(const TCHAR* Context, const FString& Speaker, float Delay, float Seconds)
{
	if (bLessonRun || bAttract || ChatterQueue.Num() >= 2)
	{
		return;
	}
	FChatter C;
	C.Context = Context;
	C.Speaker = Speaker;
	C.Delay = Delay;
	C.Seconds = Seconds;
	ChatterQueue.Add(C);
}

void AShiftPlayerController::TickChatter(float DeltaTime)
{
	ChatterGap = FMath::Max(0.0f, ChatterGap - DeltaTime);
	if (ChatterQueue.Num() == 0)
	{
		return;
	}
	for (FChatter& C : ChatterQueue)
	{
		C.Delay -= DeltaTime;
	}
	if (ChatterQueue[0].Delay > 0.0f || ChatterGap > 0.0f)
	{
		return;
	}
	const FChatter C = ChatterQueue[0];
	ChatterQueue.RemoveAt(0);
	FString Sentiment;
	const FString Line = FShiftContent::PickCommentLine(C.Context, CosmeticRng, Sentiment);
	if (Line.IsEmpty())
	{
		return;
	}
	FLinearColor Color(0.55f, 0.58f, 0.62f);   // Neutral
	if (Sentiment == TEXT("Positive"))
	{
		Color = TealColor();
	}
	else if (Sentiment == TEXT("Negative"))
	{
		Color = RedColor();
	}
	else if (Sentiment == TEXT("Suspicious"))
	{
		Color = FLinearColor::FromSRGBColor(FColor(0xB7, 0x9B, 0xFF));
	}
	PushFeed(FString::Printf(TEXT("@%s: %s"), C.Speaker.IsEmpty() ? *RandomHandle() : *C.Speaker, *Line), Color, C.Seconds, false, true);
	ChatterGap = 2.5f;
}

FString AShiftPlayerController::RecordingsDir() const
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Recordings")));
}

void AShiftPlayerController::SaveRecording(const TCHAR* AbandonedResult)
{
	if (!Run.IsValid() || !Run->HasRecording() || bRecordingSaved)
	{
		return;
	}
	if (Run->IsRecording())
	{
		Run->FinishRecording(AbandonedResult);   // the jam is left unfinished
	}
	const FShiftRecording& R = Run->GetRecording();
	if (R.Result.IsEmpty() || (R.Result == TEXT("abandoned") && R.Events.Num() == 0))
	{
		return;   // nothing was played
	}
	const FString Path = FPaths::Combine(RecordingsDir(), FString::Printf(TEXT("shift_jam%d_%u_%s_%s.shiftrec.txt"), R.Jam + 1, R.Seed, *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")), *R.Result));
	if (R.Save(Path))
	{
		bRecordingSaved = true;
		RecordingPath = Path;
		UE_LOG(LogShift, Display, TEXT("Recording saved: %s (%d clicks, %d samples, result %s, hash %s)"), *Path, R.Events.Num(), R.Samples.Num(), *R.Result, *R.Hash);
	}
	else
	{
		UE_LOG(LogShift, Warning, TEXT("Recording could not be saved to %s"), *Path);
	}
}

void AShiftPlayerController::OpenRecordingsFolder() const
{
	IFileManager::Get().MakeDirectory(*RecordingsDir(), true);
	FPlatformProcess::ExploreFolder(*RecordingsDir());
}

void AShiftPlayerController::CopyResultToClipboard()
{
	if (ResultsModel.OutcomeTitle.IsEmpty())
	{
		return;
	}
	FPlatformApplicationMisc::ClipboardCopy(*BuildShareText());
	ShowToast(TEXT("Result copied to the clipboard."), TealColor(), 2.0f);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// input

void AShiftPlayerController::HandleUi(int32 Id, float Value01)
{
	const bool bSilent = Id == EShiftUi::SliderQuality || Id == EShiftUi::SliderThumbnail || Id == EShiftUi::SliderNetwork || Id == EShiftUi::GlassOpacity
		|| Id == EShiftUi::Upload || Id == EShiftUi::Blame || Id == EShiftUi::TitleStart || Id == EShiftUi::FakeInfo || Id == EShiftUi::SettingsPanel;
	if (!bSilent)
	{
		PlaySfx(EShiftSfx::Click);
	}
	switch (Id)
	{
	case EShiftUi::TitleStart:
		PlaySfx(EShiftSfx::Click);
		EnterScreen(EShiftScreen::JamSelect);
		break;
	case EShiftUi::Back:
		EnterScreen(Screen == EShiftScreen::Build ? EShiftScreen::JamSelect : EShiftScreen::Title);
		break;
	case EShiftUi::JamCard0:
	case EShiftUi::JamCard0 + 1:
	case EShiftUi::JamCard0 + 2:
		JamIndex = CardJam.IsValidIndex(Id - EShiftUi::JamCard0) ? CardJam[Id - EShiftUi::JamCard0] : FMath::Clamp(Id - EShiftUi::JamCard0, 0, 2);
		Seed = NewSeed();
		EnterScreen(EShiftScreen::Build);
		break;
	case EShiftUi::SliderQuality:
	case EShiftUi::SliderThumbnail:
	case EShiftUi::SliderNetwork:
		SetBuildHours(Id - EShiftUi::SliderQuality, Value01);
		break;
	case EShiftUi::Upload:
		PlaySfx(EShiftSfx::Upload);
		StartPlay();
		break;
	case EShiftUi::Retry:
		RestartRun();
		break;
	case EShiftUi::NextJam:
		if (JamIndex < 2)
		{
			++JamIndex;
			Seed = NewSeed();
			EnterScreen(EShiftScreen::Build);
		}
		break;
	case EShiftUi::Menu:
		if (bLessonRun)
		{
			LeaveLesson(EShiftScreen::Title);
			break;
		}
		EnterScreen(EShiftScreen::JamSelect);
		break;
	case EShiftUi::LearnToPlay:
		bHowTo = false;
		StartLesson(1, 1);
		break;
	case EShiftUi::HowTo:
		bHowTo = !bHowTo;
		bSettingsOpen = false;
		break;
	case EShiftUi::Markers:
		bMarkers = !bMarkers;
		if (!bQaRunFlag) { ShiftSave::SetFlag(TEXT("Markers"), bMarkers); }
		break;
	case EShiftUi::LargeText:
		bLargeText = !bLargeText;
		if (!bQaRunFlag) { ShiftSave::SetFlag(TEXT("LargeText"), bLargeText); }
		break;
	case EShiftUi::ReduceMotion:
		bReduceMotion = !bReduceMotion;
		if (!bQaRunFlag) { ShiftSave::SetFlag(TEXT("ReduceMotion"), bReduceMotion); }
		break;
	case EShiftUi::GlassToggle:
		bGlassSaved = !bGlassSaved;
		if (!bQaRunFlag) { ShiftSave::SetGlassWindow(bGlassSaved); }
		ShowToast(bGlassSaved ? TEXT("See-through window on: restart the game to apply.") : TEXT("See-through window off: restart the game to apply."), TealColor(), 3.0f);
		break;
	case EShiftUi::GlassOpacity:
	{
		const int32 Percent = FMath::Clamp(FMath::RoundToInt(Value01 * 20.0f), 0, 20) * 5;
		if (Percent != GlassOpacityPct)
		{
			GlassOpacityPct = Percent;
			if (!bQaRunFlag) { ShiftSave::SetGlassOpacity(Percent); }
		}
		break;
	}
	case EShiftUi::Achievements:
		ExtrasPage = 1;
		bSettingsOpen = false;
		break;
	case EShiftUi::Credits:
		ExtrasPage = 2;
		bSettingsOpen = false;
		break;
	case EShiftUi::Quit:
		if (Screen == EShiftScreen::Title)
		{
			QuitGame();
		}
		break;
	case EShiftUi::ExtrasClose:
		ExtrasPage = 0;
		break;
	case EShiftUi::Pause:
		SetPaused(!bPaused);
		break;
	case EShiftUi::LessonContinue:
		if (bLessonRun && Lesson.IsValid() && Run.IsValid())
		{
			Lesson->Continue(*Run);
		}
		break;
	case EShiftUi::LessonLeave:
		LeaveLesson(EShiftScreen::Title);
		break;
	case EShiftUi::LessonNext:
		if (bLessonRun && LessonChapter < FShiftLesson::ChapterCount())
		{
			StartLesson(LessonChapter + 1, 1);
		}
		break;
	case EShiftUi::LessonPlay:
		LeaveLesson(EShiftScreen::JamSelect);
		break;
	case EShiftUi::Resume:
		SetPaused(false);
		break;
	case EShiftUi::Blame:
		if (Run.IsValid() && !bLessonRun)
		{
			Run->TakeBlame();
		}
		break;
	case EShiftUi::CoachDismiss:
		DismissCoach();
		break;
	case EShiftUi::Mute:
		ToggleMute();
		break;
	case EShiftUi::Settings:
		bSettingsOpen = !bSettingsOpen;
		break;
	case EShiftUi::SettingsClose:
		bSettingsOpen = false;
		break;
	case EShiftUi::SettingsPanel:
		break;   // a click on the menu's padding does nothing
	case EShiftUi::EffectsOn:
		SetEffectsOn(true);
		break;
	case EShiftUi::EffectsOff:
		SetEffectsOn(false);
		break;
	case EShiftUi::MusicOn:
		SetMusicOn(true);
		break;
	case EShiftUi::MusicOff:
		SetMusicOn(false);
		break;
	case EShiftUi::WindowFullscreen:
	case EShiftUi::WindowBorderless:
	case EShiftUi::WindowWindowed:
		SetWindowMode(Id - EShiftUi::WindowFullscreen);
		break;
	case EShiftUi::Share:
		CopyResultToClipboard();
		break;
	case EShiftUi::Recordings:
		OpenRecordingsFolder();
		break;
	default:
		break;
	}
}

void AShiftPlayerController::HandleHudClick(const FVector2D& LocalPos, bool bShift, bool bRightButton)
{
	if (Screen != EShiftScreen::Play || !Run.IsValid() || !Board || Run->IsOver() || bPaused || bHowTo)
	{
		return;
	}
	FVector WorldLoc, WorldDir;
	if (!DeprojectScreenPositionToWorld(static_cast<float>(LocalPos.X), static_cast<float>(LocalPos.Y), WorldLoc, WorldDir))
	{
		return;
	}
	const int32 Node = Board->NodeAtWorld(FVector2D(WorldLoc.X, WorldLoc.Y));
	if (Node < 0)
	{
		return;
	}
	if (bLessonRun && Lesson.IsValid())
	{
		// a lesson allows only what the step teaches, on the voters it points at; anything else is turned away with a line that says what to do
		FString Why;
		if (!Lesson->AllowClick(Run->GetSim(), Node, bRightButton, bShift || IsNightHeldRaw(), Why))
		{
			if (!Why.IsEmpty())
			{
				PlaySfx(EShiftSfx::Locked);
				ShowToast(Why, AmberColor(), 2.2f);
			}
			return;
		}
	}
	bPlayerActed = true;
	IdleClock = 0.0f;
	if (bRightButton)
	{
		Run->QueueRally(Node);   // right-click: Rally a voter you hold
		return;
	}
	PlaySfx(EShiftSfx::Plug);
	Run->QueueAction(Node, bShift || IsNightHeldNow());
}

void AShiftPlayerController::HandleHudKey(const FKey& Key)
{
	if (Key == EKeys::M)
	{
		ToggleMute();
		return;
	}
	if (Screen == EShiftScreen::Title)
	{
		return;   // the main menu is for the mouse (and holding SHIFT, which is not a key press event here); only M above still works
	}
	if (bSettingsOpen && Key == EKeys::Escape)
	{
		bSettingsOpen = false;   // Esc closes the settings menu first, before it pauses or goes back
		return;
	}
	if (bHowTo)
	{
		if (Key == EKeys::H || Key == EKeys::Escape || Key == EKeys::Enter || Key == EKeys::SpaceBar)
		{
			bHowTo = false;   // the reference page closes
		}
		return;
	}
	if (ExtrasPage != 0)
	{
		if (Key == EKeys::Escape || Key == EKeys::Enter || Key == EKeys::SpaceBar || (Key == EKeys::A && ExtrasPage == 1) || (Key == EKeys::C && ExtrasPage == 2))
		{
			ExtrasPage = 0;
		}
		return;
	}
	if (Screen == EShiftScreen::Title && !bLessonRun && !bSettingsOpen && (Key == EKeys::A || Key == EKeys::C))
	{
		ExtrasPage = Key == EKeys::A ? 1 : 2;
		PlaySfx(EShiftSfx::Click);
		return;
	}
	if (Key == EKeys::H && !(Screen == EShiftScreen::Play && ResultsShown()))
	{
		bHowTo = true;   // one screen with everything: the goal, the controls, the three dangers
		bSettingsOpen = false;
		PlaySfx(EShiftSfx::Click);
		return;
	}
	if (Screen == EShiftScreen::Play && !bAttract && !ResultsShown() && !bSettingsOpen)
	{
		// pan the board: arrows or WASD (the keys repeat while held), Home puts it back
		const float Step = 56.0f;
		if (Key == EKeys::Left || Key == EKeys::A) { ViewPanTarget.X += Step; return; }
		if (Key == EKeys::Right || Key == EKeys::D) { ViewPanTarget.X -= Step; return; }
		if (Key == EKeys::Up || Key == EKeys::W) { ViewPanTarget.Y += Step; return; }
		if (Key == EKeys::Down || Key == EKeys::S) { ViewPanTarget.Y -= Step; return; }
		if (Key == EKeys::Home) { ViewPanTarget = FVector2D::ZeroVector; return; }
	}
	if (bLessonRun && Lesson.IsValid() && Screen == EShiftScreen::Play && !bPaused && Lesson->View().bReading && (Key == EKeys::Enter || Key == EKeys::SpaceBar))
	{
		HandleUi(EShiftUi::LessonContinue, 0.0f);
		return;
	}

	switch (Screen)
	{
	case EShiftScreen::Title:
		if (Key == EKeys::Enter || Key == EKeys::SpaceBar)
		{
			HandleUi(EShiftUi::TitleStart, 0.0f);
		}
		else if (Key == EKeys::Escape)
		{
			QuitGame();   // Esc on the title quits the game
		}
		break;
	case EShiftScreen::JamSelect:
		if (Key == EKeys::One) { HandleUi(EShiftUi::JamCard0, 0.0f); }
		else if (Key == EKeys::Two) { HandleUi(EShiftUi::JamCard0 + 1, 0.0f); }
		else if (Key == EKeys::Three) { HandleUi(EShiftUi::JamCard0 + 2, 0.0f); }
		else if (Key == EKeys::Escape || Key == EKeys::BackSpace) { HandleUi(EShiftUi::Back, 0.0f); }
		break;
	case EShiftScreen::Build:
		if (Key == EKeys::Enter) { HandleUi(EShiftUi::Upload, 0.0f); }
		else if (Key == EKeys::Escape || Key == EKeys::BackSpace) { HandleUi(EShiftUi::Back, 0.0f); }
		break;
	case EShiftScreen::Play:
		if (ResultsShown())
		{
			if (Key == EKeys::R) { HandleUi(EShiftUi::Retry, 0.0f); }
			else if (Key == EKeys::O) { HandleUi(EShiftUi::Recordings, 0.0f); }
			else if (Key == EKeys::Enter) { HandleUi(EShiftUi::NextJam, 0.0f); }
			else if (Key == EKeys::Escape) { HandleUi(EShiftUi::Menu, 0.0f); }
		}
		else if (Key == EKeys::SpaceBar || Key == EKeys::Escape)
		{
			SetPaused(!bPaused);
		}
		else if (Key == EKeys::N)
		{
			bNightLatch = !bNightLatch;
			if (bNightLatch && Screen == EShiftScreen::Play && AchEligible())
			{
				Ach.OnNightLatch();
			}
		}
		else if (Key == EKeys::B)
		{
			HandleUi(EShiftUi::Blame, 0.0f);
		}
		else if (Key == EKeys::E)
		{
			EdgeMode = (EdgeMode + 1) % 3;
			ShiftSave::SetEdgeMode(EdgeMode);
			ShowToast(EdgeMode == 0 ? TEXT("Relation lines: faint (E)") : (EdgeMode == 1 ? TEXT("Relation lines: off (E)") : TEXT("Relation lines: full (E)")), TealColor(), 1.4f);
		}
		else if (Key == EKeys::L)
		{
			LeaderboardMode = (LeaderboardMode + 1) % 3;
			ShiftSave::SetLeaderboardMode(LeaderboardMode);
		}
		else if (Key == EKeys::R && bPaused)
		{
			HandleUi(EShiftUi::Retry, 0.0f);
		}
		break;
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// events, tutorial

void AShiftPlayerController::HandleEvents(const TArray<FShiftEvent>& Events)
{
	for (const FShiftEvent& E : Events)
	{
		if (AchEligible())
		{
			Ach.OnEvent(E, Run->GetSim());
		}
		if (E.Type != EShiftEventType::DayChanged)
		{
			UE_LOG(LogShift, Log, TEXT("event type=%d node=%d prev=%d new=%d sock=%d value=%.2f"), static_cast<int32>(E.Type), E.Node, E.PrevOwner, E.NewOwner, E.bSock ? 1 : 0, E.Value);
		}
		switch (E.Type)
		{
		case EShiftEventType::PlayerFlipped:
		{
			TutEventMask |= EvPlug | (E.bSock ? EvSock : 0u);
			PlaySfx(E.bSock ? EShiftSfx::Sock : EShiftSfx::Flip);
			const FString Line = FShiftContent::PickComment(E.bSock ? TEXT("Sockpuppet") : TEXT("FlipToYou"), CosmeticRng);
			ShowToast(FString::Printf(TEXT("@%s: %s"), *NodeHandle(E.Node), Line.IsEmpty() ? TEXT("fine, you win me over") : *Line), TealColor());
			if (E.bSock && CosmeticRng.FRand() < 0.5f)
			{
				Chatter(TEXT("BotComment"), FShiftContent::PickHandleOfKind(TEXT("Bot"), CosmeticRng), 0.8f);
			}
			break;
		}
		case EShiftEventType::PlayerReinforced:
		{
			TutEventMask |= EvReinforce;
			PlaySfx(EShiftSfx::Reinforce);
			const FString Line = FShiftContent::PickComment(TEXT("Reinforce"), CosmeticRng);
			ShowToast(FString::Printf(TEXT("@%s: %s"), *NodeHandle(E.Node), Line.IsEmpty() ? TEXT("still with you") : *Line), TealColor(), 1.8f);
			break;
		}
		case EShiftEventType::PlayerParried:
		{
			TutEventMask |= EvReinforce;
			PlaySfx(EShiftSfx::Reinforce);
			if (Board)
			{
				Board->BounceNode(E.Node, E.PrevOwner, E.Value);
			}
			ShowToast(FString::Printf(TEXT("PARRIED: %s's plug on @%s bounced off"), *EntryName(E.PrevOwner), *NodeHandle(E.Node)), TealColor(), 1.8f);
			break;
		}
		case EShiftEventType::PlayerWeakened:
		{
			TutEventMask |= EvPlug;
			PlaySfx(EShiftSfx::Weaken);
			const FString Line = FShiftContent::PickComment(TEXT("Dent"), CosmeticRng);
			ShowToast(FString::Printf(TEXT("@%s: %s"), *NodeHandle(E.Node), Line.IsEmpty() ? TEXT("hmm, maybe") : *Line), AmberColor(), 1.8f);
			break;
		}
		case EShiftEventType::PlayerGated:
			TutEventMask |= EvGated;
			PlaySfx(EShiftSfx::Gated);
			GatedSeenNodes.AddUnique(E.Node);
			ShowToast(FString::Printf(TEXT("@%s ignores you: only %.0f%% of the way to being noticed. Night shift ignores gates."), *NodeHandle(E.Node), E.Value * 100.0f), RedColor(), 3.0f);
			break;
		case EShiftEventType::PlayerLocked:
			PlaySfx(EShiftSfx::Locked);
			ShowToast(FString::Printf(TEXT("@%s was just flipped and is immune for a moment."), *NodeHandle(E.Node)), AmberColor(), 1.6f);
			break;
		case EShiftEventType::PlayerNoPosts:
			PlaySfx(EShiftSfx::NoPosts);
			ShowToast(TEXT("Out of Posts. They regenerate over time."), AmberColor(), 1.6f);
			break;
		case EShiftEventType::PlayerRefused:
			if (Run->GetSim().ShadowbanRemaining() > 0.0)
			{
				ShowToast(TEXT("Shadowbanned: nobody sees your posts for a moment."), RedColor(), 1.4f);
			}
			break;
		case EShiftEventType::Strike:
			TutEventMask |= EvStrike;
			PlaySfx(EShiftSfx::Strike);
			PlaySfx(EShiftSfx::Shadowban, 0.7f);
			ShowToast(FString::Printf(TEXT("STRIKE %d of %d: the mods noticed. %d sockpuppet vote%s reverted (-%.1f%% of the vote)."), static_cast<int32>(E.Value),
				static_cast<int32>(Run->GetSim().GetParams().MaxStrikes), E.Count, E.Count == 1 ? TEXT("") : TEXT("s"), E.Share * 100.0f), RedColor(), 3.2f);
			{
				const int32 Max = static_cast<int32>(Run->GetSim().GetParams().MaxStrikes);
				const int32 Number = static_cast<int32>(E.Value);
				if (Number == Max - 1)
				{
					TutEventMask |= EvStrikeLast;
				}
				PushAlert(AlertStrike, Number == Max - 1 ? FString::Printf(TEXT("STRIKE %d OF %d: ONE MORE AND YOU ARE OUT"), Number, Max) : FString::Printf(TEXT("STRIKE %d OF %d"), Number, Max),
					E.Count > 0 ? FString::Printf(TEXT("%d fake vote%s reverted (-%.1f%% of the vote). Banned %.0f s."), E.Count, E.Count == 1 ? TEXT("") : TEXT("s"), E.Share * 100.0f, Run->GetSim().GetParams().ShadowbanSeconds)
					: FString::Printf(TEXT("Caught in the red zone. Banned %.0f s."), Run->GetSim().GetParams().ShadowbanSeconds),
					RedColor(), 3.4f, 50);
				EdgeFlash = 1.0f;
				EdgeColor = RedColor();
				Chatter(TEXT("Strike"), FShiftContent::PickHandleOfKind(TEXT("Mod"), CosmeticRng), 1.4f);
				Chatter(TEXT("Shadowban"), FShiftContent::PickHandleOfKind(TEXT("Lurker"), CosmeticRng), 3.2f);
			}
			break;
		case EShiftEventType::BlameOffered:
			// the offer itself is read from the run every frame (FillAlertModel); this starts its cues
			OfferAge = 0.0f;
			BlameCue = 0.5f;
			EdgeFlash = 1.0f;
			EdgeColor = OrangeColor();
			UE_LOG(LogShift, Display, TEXT("Alert: t=%.1f kind=%d offer open %.1f s, fake %.1f%%"), Run->GetSim().Time(), AlertBlameOffer, E.Value, E.Share * 100.0f);
			break;
		case EShiftEventType::BlameRefused:
			bBlameRefused = true;
			BlameRefusedShare = E.Share;
			break;
		case EShiftEventType::Disqualified:
		{
			PlaySfx(EShiftSfx::DQ, 1.3f);
			const ShiftSim::Params& DP = Run->GetSim().GetParams();
			FString Reason;
			if (bBlameRefused)
			{
				Reason = DP.BlameBasis > 0.5 ? FString::Printf(TEXT("Blame refused: %.0f%% fake votes over your strikes, the limit is %.0f%%."), BlameRefusedShare * 100.0f, DP.BlameMaxFake * 100.0f)
					: FString::Printf(TEXT("Blame refused: %.0f%% fake votes, the limit is %.0f%%."), BlameRefusedShare * 100.0f, DP.BlameMaxFake * 100.0f);
			}
			else if (Run->BlameUsed())
			{
				Reason = TEXT("Third strike again, and the blame was already used up.");
			}
			else if (Run->GetBlameStats().Lapsed > 0)
			{
				Reason = TEXT("You did not shift the blame in time.");
			}
			else
			{
				Reason = FString::Printf(TEXT("%d strikes."), static_cast<int32>(DP.MaxStrikes));
			}
			ResultsHold = 1.8f;
			ShowHeadline(TEXT("Disqualified"), EntryName(ShiftSim::kPlayerId));
			PushAlert(AlertDisqualified, TEXT("DISQUALIFIED"), Reason, DeepRedColor(), ResultsHold, 100);
			EdgeFlash = 1.0f;
			EdgeColor = RedColor();
			break;
		}
		case EShiftEventType::PlayerLostVoter:
			TutEventMask |= EvLost;
			LostRecently.Add({ E.Node, E.NewOwner, static_cast<int32>(E.Value) });
			break;
		case EShiftEventType::PlayerRallied:
		{
			const ShiftSim::Params& RP = Run->GetSim().GetParams();
			PlaySfx(EShiftSfx::Reinforce);
			ShowToast(FString::Printf(TEXT("RALLY: %d voter%s shielded for %.0fs, +%.0f%% conviction"), E.Count, E.Count == 1 ? TEXT("") : TEXT("s"), RP.RallyLock, RP.RallyBoost * 100.0), TealColor(), 2.0f);
			RallyFxNode = E.Node;
			RallyFxAge = 0.0f;
			if (Board)
			{
				// a ripple: every voter the Rally shields pulses once
				const ShiftSim::Sim& RS = Run->GetSim();
				for (int32 Other = 0; Other < RS.NodeCount(); ++Other)
				{
					if (Other == E.Node || (RS.Opinion(Other) == ShiftSim::kPlayerId && RS.ShieldRemaining(Other) > 0.0))
					{
						Board->FlashNode(Other);
					}
				}
			}
			break;
		}
		case EShiftEventType::PlayerRallyBlocked:
			PlaySfx(EShiftSfx::Locked);
			ShowToast(E.Value < 0.5f ? FString::Printf(TEXT("Rally is cooling down: %.0fs."), Run->GetSim().RallyCooldownRemaining()) : FString(TEXT("Rally works on a voter you hold (right-click).")), AmberColor(), 1.6f);
			break;
		case EShiftEventType::PlayerOverheated:
			PlaySfx(EShiftSfx::NoPosts);
			ShowToast(TEXT("Sockpuppets overheated: let Heat cool before the next one."), RedColor(), 2.2f);
			break;
		case EShiftEventType::FinalPushStart:
			PlaySfx(EShiftSfx::ModWarn);
			PushAlert(AlertFinalPush, TEXT("FINAL PUSH"), FString::Printf(TEXT("%s goes all-in for %.0f s: a burst of attacks. Your Posts refill +%.0f%%."), *EntryName(Run->GetSim().RivalId()), E.Value, E.Share * 100.0f), RedColor(), 3.6f, 20);
			ShowToast(FString::Printf(TEXT("%s goes all-in for the last %.0f s: a burst of attacks until it runs low on Posts. Your Posts refill %.0f%% faster."), *EntryName(Run->GetSim().RivalId()), E.Value, E.Share * 100.0f), RedColor(), 4.0f);
			break;
		case EShiftEventType::EntryTookBig:
			TutEventMask |= EvPackHit;
			UE_LOG(LogShift, Log, TEXT("Pack: t=%.1f %s took @%s (weight %.0f) from %s"), Run.IsValid() ? Run->GetSim().Time() : 0.0, *EntryName(E.NewOwner), *NodeHandle(E.Node), E.Value, *EntryName(E.PrevOwner));
			ShowToast(FString::Printf(TEXT("%s took @%s (weight %.0f) from %s"), *EntryName(E.NewOwner), *NodeHandle(E.Node), E.Value, *EntryName(E.PrevOwner)), ShiftPalette::Entry(E.NewOwner), 3.5f);
			if (Board)
			{
				Board->FlashNode(E.Node);
			}
			break;
		case EShiftEventType::EyeAwayWarn:
			TutEventMask |= EvEyeAway;
			PlaySfx(EShiftSfx::Tip);
			ShowToast(FString::Printf(TEXT("The Mod Eye looks away in %.0f s: night shift will be free."), E.Value), PurpleColor(), 3.5f);
			break;
		case EShiftEventType::EyeAwayStart:
			PlaySfx(EShiftSfx::NightOn);
			ShowToast(FString::Printf(TEXT("THE MOD EYE LOOKS AWAY for %.0f s: hold SHIFT, sockpuppets add no Heat."), E.Value), PurpleColor(), 4.0f);
			break;
		case EShiftEventType::EyeAwayEnd:
			PlaySfx(EShiftSfx::NightOff);
			ShowToast(TEXT("The Mod Eye is back. Fake votes you hold heat you up again."), AmberColor(), 3.0f);
			break;
		case EShiftEventType::FinalAudit:
			PlaySfx(EShiftSfx::ModSweep);
			PushAlert(AlertFinalAudit, TEXT("FINAL AUDIT"), E.Count > 0 ? FString::Printf(TEXT("%d fake vote%s purged (-%.1f%% of the vote)."), E.Count, E.Count == 1 ? TEXT("") : TEXT("s"), E.Value * 100.0f) : FString(TEXT("Clean: no fake votes left to purge.")),
				E.Count > 0 ? DeepRedColor() : TealColor(), 3.0f, 30);
			ShowToast(E.Count > 0 ? FString::Printf(TEXT("FINAL AUDIT: %d sockpuppet vote%s purged (%.1f%% of the vote)."), E.Count, E.Count == 1 ? TEXT("") : TEXT("s"), E.Value * 100.0f)
				: FString(TEXT("FINAL AUDIT: no sockpuppet votes left to purge.")), E.Count > 0 ? RedColor() : TealColor(), 3.5f);
			break;
		case EShiftEventType::ModTelegraph:
			TutEventMask |= EvTelegraph;
			if (CosmeticRng.FRand() < 0.6f)
			{
				Chatter(TEXT("ModScan"), FShiftContent::PickHandleOfKind(TEXT("Mod"), CosmeticRng), 0.3f);
			}
			break;
		case EShiftEventType::BlameTaken:
			PlaySfx(EShiftSfx::Blame);
			PushAlert(AlertBlameTaken, TEXT("BLAME SHIFTED"), FString::Printf(TEXT("Strike taken back. The mods are busy with %s now."), *EntryName(Run->GetSim().RivalId())), TealColor(), 2.8f, 60);
			EdgeFlash = 0.0f;
			ShowToast(TEXT("You shifted the blame. The mods are busy with the rival now."), TealColor(), 3.0f);
			ShowHeadline(TEXT("RivalCollapses"), EntryName(Run->GetSim().RivalId()));
			break;
		case EShiftEventType::ModSweepClear:
			PlaySfx(EShiftSfx::ModSweep);
			ShowToast(TEXT("Mod sweep: all clear."), TealColor(), 1.6f);
			ShowHeadline(TEXT("ModSweep"), FString());
			break;
		case EShiftEventType::ModSweepHit:
			PlaySfx(EShiftSfx::ModSweep);
			ShowToast(TEXT("Mod sweep caught you in the red zone."), RedColor(), 2.4f);
			ShowHeadline(TEXT("ModSweep"), FString());
			break;
		case EShiftEventType::DirectorEvent:
		{
			static const TCHAR* const Kinds[3] = { TEXT("AlgorithmBoost"), TEXT("StreamerDiscovers"), TEXT("DramaThread") };
			static const TCHAR* const Talk[3] = { TEXT("AlgorithmBoost"), TEXT("StreamerFeature"), TEXT("Drama") };
			ShowHeadline(Kinds[FMath::Clamp(E.Node, 0, 2)], EntryName(E.NewOwner));
			if (E.Node >= 0 && E.Node < 3)
			{
				Chatter(Talk[E.Node], FString(), 1.2f);
			}
			break;
		}
		case EShiftEventType::SpotlightStart:
			if (!Run->HasBot() && !bLessonRun)
			{
				PlaySfx(EShiftSfx::Tip);
				SpotFlashClock = 0.0f;
				PushFeed(FString::Printf(TEXT("SPOTLIGHT: a streamer is looking at @%s. Flip or back them within %.0f s for +%.0f Post."), *NodeHandle(E.Node), E.Value, Run->GetPolicy().SpotlightBonusPosts), AmberColor(), E.Value, false, true);
				if (Board)
				{
					Board->FlashNode(E.Node);
				}
			}
			break;
		case EShiftEventType::SpotlightHit:
			if (!Run->HasBot())
			{
				PlaySfx(EShiftSfx::Reinforce);
				ShowToast(FString::Printf(TEXT("Spotlight! +%.0f Post: the streamer liked that."), E.Value), TealColor(), 2.4f);
			}
			break;
		case EShiftEventType::SpotlightEnd:
			break;
		case EShiftEventType::DayChanged:
			PlaySfx(EShiftSfx::Day);
			break;
		default:
			break;
		}
	}
}

void AShiftPlayerController::ResetAlerts()
{
	AlertNow = FAlert();
	AlertQueue.Reset();
	AlertAge = 0.0f;
	OfferAge = 0.0f;
	BlameCue = 0.0f;
	EdgeFlash = 0.0f;
	ResultsHold = 0.0f;
	bBlameRefused = false;
	BlameRefusedShare = 0.0f;
}

/** An urgent event takes over the bottom bar. The highest priority is on screen; a lower one waits its turn (three at most), and a higher one replaces it. */
void AShiftPlayerController::PushAlert(int32 Kind, const FString& Title, const FString& Detail, const FLinearColor& Color, float Duration, int32 Priority)
{
	UE_LOG(LogShift, Display, TEXT("Alert: t=%.1f kind=%d '%s' / '%s'"), Run.IsValid() ? Run->GetSim().Time() : 0.0, Kind, *Title, *Detail);
	FAlert Item;
	Item.Kind = Kind;
	Item.Title = Title;
	Item.Detail = Detail;
	Item.Color = Color;
	Item.Duration = Duration;
	Item.Priority = Priority;
	if (AlertNow.Kind == AlertNone || Priority >= AlertNow.Priority)
	{
		AlertNow = Item;
		AlertAge = 0.0f;
		return;
	}
	AlertQueue.Add(Item);
	AlertQueue.StableSort([](const FAlert& A, const FAlert& B) { return A.Priority > B.Priority; });
	if (AlertQueue.Num() > 3)
	{
		AlertQueue.SetNum(3);
	}
}

/** Once a frame. Everything here stands still while the game is paused: an alert does not run out, the Blame offer does not sound, and the frame does not fade. */
void AShiftPlayerController::TickAlerts(float DeltaTime)
{
	ResultsHold = FMath::Max(0.0f, ResultsHold - DeltaTime);
	if (Screen != EShiftScreen::Play || bAttract || !Run.IsValid() || bPaused)
	{
		return;
	}
	EdgeFlash = FMath::Max(0.0f, EdgeFlash - DeltaTime * 1.4f);
	if (Run->BlameAvailable())
	{
		OfferAge += DeltaTime;
		BlameCue -= DeltaTime;
		if (BlameCue <= 0.0f)
		{
			PlaySfx(EShiftSfx::ModWarn, 1.8f);   // the repeating warning: the one sound the offer has, so it is not missed with the eyes elsewhere
			BlameCue = 0.8f;
		}
	}
	else
	{
		OfferAge = 0.0f;
		BlameCue = 0.5f;
	}
	if (AlertNow.Kind != AlertNone)
	{
		AlertAge += DeltaTime;
		if (AlertAge >= AlertNow.Duration)
		{
			if (AlertQueue.Num() > 0)
			{
				AlertNow = AlertQueue[0];
				AlertQueue.RemoveAt(0);
				AlertAge = 0.0f;
			}
			else
			{
				AlertNow = FAlert();
			}
		}
	}
}

void AShiftPlayerController::FillAlertModel(FShiftHudModel& M) const
{
	M.EdgeFlash = bReduceMotion ? EdgeFlash * 0.35f : EdgeFlash;
	M.EdgeColor = EdgeColor;
	FShiftHudAlert& A = M.Alert;
	if (Run->BlameAvailable())
	{
		// The offer: while it is open it owns the bar, whatever else is queued. It is read from the run, so a pause or a take ends it at once.
		const ShiftSim::Params& P = Run->GetSim().GetParams();
		const float Left = static_cast<float>(Run->BlameSecondsLeft());
		A.Kind = AlertBlameOffer;
		A.Title = FString::Printf(TEXT("STRIKE %d OF %d: SHIFT THE BLAME?  (B)"), Run->GetSim().Strikes(), static_cast<int32>(P.MaxStrikes));
		A.Detail = P.BlameBasis > 0.5 ? FString::Printf(TEXT("%.0f%% fake votes over your strikes (limit %.0f%%): click here or press B."), Run->BlameFakeShare() * 100.0, P.BlameMaxFake * 100.0)
			: FString::Printf(TEXT("%.0f%% fake votes (limit %.0f%%): click here or press B to blame the rival."), Run->BlameFakeShare() * 100.0, P.BlameMaxFake * 100.0);
		A.Color = OrangeColor();
		A.Frac = FMath::Clamp(Left / FMath::Max(0.1f, static_cast<float>(Run->BlameWindowSeconds())), 0.0f, 1.0f);
		A.SecondsLeft = Left;
		A.Age = OfferAge;
		A.bClickable = true;
		const float Pulse = 0.5f + 0.5f * FMath::Sin(OfferAge * 9.42478f);   // 1.5 Hz: slow enough to be safe, fast enough to be seen
		if (!bPaused && 0.30f + 0.35f * Pulse > M.EdgeFlash)
		{
			M.EdgeFlash = 0.30f + 0.35f * Pulse;
			M.EdgeColor = OrangeColor();
		}
	}
	else if (AlertNow.Kind != AlertNone)
	{
		A.Kind = AlertNow.Kind;
		A.Title = AlertNow.Title;
		A.Detail = AlertNow.Detail;
		A.Color = AlertNow.Color;
		A.Frac = FMath::Clamp(1.0f - AlertAge / FMath::Max(0.1f, AlertNow.Duration), 0.0f, 1.0f);
		A.Age = AlertAge;
	}
}

/** Samples every entry's share once a second of run time, for the leaderboard's trend column: who gained and who lost over the last 20 s. */
void AShiftPlayerController::TickShareTrend()
{
	const ShiftSim::Sim& S = Run->GetSim();
	const auto AddSample = [this, &S]()
		{
			for (int32 E = 0; E < S.NumEntries(); ++E)
			{
				ShareHistory[E].Add(static_cast<float>(S.Share(E)));
				if (ShareHistory[E].Num() > 21)
				{
					ShareHistory[E].RemoveAt(0);
				}
			}
		};
	if (ShareSampleTime < 0.0 || S.Time() + 1.0e-6 < ShareSampleTime || ShareHistory.Num() != S.NumEntries())
	{
		ShareHistory.Reset();
		ShareHistory.SetNum(S.NumEntries());
		ShareSampleTime = S.Time();
		AddSample();
		return;
	}
	while (S.Time() >= ShareSampleTime + 1.0)
	{
		ShareSampleTime += 1.0;
		AddSample();
	}
}

void AShiftPlayerController::TickLostVoters(float DeltaTime)
{
	LostCooldown = FMath::Max(0.0f, LostCooldown - DeltaTime);
	if (LostRecently.Num() == 0 || !Run.IsValid())
	{
		return;
	}
	if (LostCooldown > 0.0f || HasActiveToast(0.6f) || Run->IsOver())
	{
		LostRecently.Reset();   // a fresher toast is showing, or one was just shown: the board's wobble and flash already told the player
		return;
	}
	// The heaviest loss names the toast. A lone light voter is left to the board; three at once, or any voter with followers, is worth a line.
	const ShiftSim::Sim& S = Run->GetSim();
	const FLostVoter* Heaviest = nullptr;
	double HeaviestWeight = -1.0;
	for (const FLostVoter& Lost : LostRecently)
	{
		const double Weight = S.GetGraph().nodes[Lost.Node].weight;
		if (Weight > HeaviestWeight)
		{
			HeaviestWeight = Weight;
			Heaviest = &Lost;
		}
	}
	const int32 Count = LostRecently.Num();
	if (Heaviest && (Count >= 3 || HeaviestWeight >= 60.0))
	{
		const FString Line = FShiftContent::PickComment(Heaviest->NewOwner == S.RivalId() ? TEXT("FlipToRival") : TEXT("FlipToOther"), CosmeticRng);
		const FString Quote = Line.IsEmpty() ? FString(TEXT("changed my mind")) : Line;
		int32 Snatched = 0;
		for (const FLostVoter& Lost : LostRecently)
		{
			Snatched += Lost.Cause != 0 ? 1 : 0;
		}
		FString Text;
		if (Count == 1)
		{
			Text = Heaviest->Cause == 0 ? FString::Printf(TEXT("@%s drifted away: %s"), *NodeHandle(Heaviest->Node), *Quote)
				: FString::Printf(TEXT("@%s was snatched by %s"), *NodeHandle(Heaviest->Node), *EntryName(Heaviest->NewOwner));
		}
		else
		{
			Text = FString::Printf(TEXT("Lost %d voters (%d snatched, %d drifted). @%s: %s"), Count, Snatched, Count - Snatched, *NodeHandle(Heaviest->Node), *Quote);
		}
		ShowToast(Text, AmberColor(), 2.6f);
		PlaySfx(EShiftSfx::Weaken, 0.6f);
		LostCooldown = 3.0f;
	}
	LostRecently.Reset();
}

FString AShiftPlayerController::ExpandTutorialText(const FString& Text) const
{
	if (!Run.IsValid() || !Text.Contains(TEXT("{")))
	{
		return Text;
	}
	// Numbers come from the sim's tuning, so a retune cannot leave a card with stale figures.
	const ShiftSim::Params& P = Run->GetSim().GetParams();
	FString Out = Text;
	Out.ReplaceInline(TEXT("{sockheat}"), *FString::Printf(TEXT("%.0f"), P.SockHeat));
	Out.ReplaceInline(TEXT("{heatrate}"), *FString::Printf(TEXT("%.0f"), P.HeatNightRate));
	Out.ReplaceInline(TEXT("{redzone}"), *FString::Printf(TEXT("%.0f"), P.HeatRedZone));
	Out.ReplaceInline(TEXT("{strikes}"), *FString::Printf(TEXT("%.0f"), P.MaxStrikes));
	Out.ReplaceInline(TEXT("{ban}"), *FString::Printf(TEXT("%.0f"), P.ShadowbanSeconds));
	Out.ReplaceInline(TEXT("{rallycost}"), *FString::Printf(TEXT("%.0f"), P.RallyCost));
	Out.ReplaceInline(TEXT("{rallyboost}"), *FString::Printf(TEXT("%.0f%%"), P.RallyBoost * 100.0));
	Out.ReplaceInline(TEXT("{rallycd}"), *FString::Printf(TEXT("%.0f"), P.RallyCooldown));
	Out.ReplaceInline(TEXT("{rallylock}"), *FString::Printf(TEXT("%.0f"), P.RallyLock));
	Out.ReplaceInline(TEXT("{eyeaway}"), *FString::Printf(TEXT("%.0f"), Run->GetPolicy().EyeAwaySeconds));
	Out.ReplaceInline(TEXT("{fakelimit}"), *FString::Printf(TEXT("%.0f"), P.SockHeldHeat > 0.0 ? 100.0 * P.HeatDecay / P.SockHeldHeat : 100.0));
	Out.ReplaceInline(TEXT("{grudge}"), *FString::Printf(TEXT("%.0f"), Run->GetPolicy().GrudgeSeconds));
	Out.ReplaceInline(TEXT("{sympathy}"), *FString::Printf(TEXT("%.0f"), 100.0 * Run->GetSim().GetParams().BacklashLead));
	Out.ReplaceInline(TEXT("{pushpct}"), *FString::Printf(TEXT("%.0f"), 100.0 * FMath::Max(0.0, Run->GetPolicy().FinalPushRegen - 1.0)));
	Out.ReplaceInline(TEXT("{review}"), *FString::Printf(TEXT("%.0f"), P.SockReview));
	Out.ReplaceInline(TEXT("{launder}"), *FString::Printf(TEXT("%.0f%%"), P.SockLaunderConv * 100.0));
	Out.ReplaceInline(TEXT("{blamefake}"), *FString::Printf(TEXT("%.0f"), P.BlameMaxFake * 100.0));
	return Out;
}

void AShiftPlayerController::QueueCoach(int32 Index)
{
	if (Index < 0 || Index >= TutCount)
	{
		return;
	}
	const int32 Bit = 1 << Index;
	if ((CoachSeenMask & Bit) != 0 || (CoachQueuedMask & Bit) != 0)
	{
		return;
	}
	CoachQueuedMask |= Bit;
	CoachQueue.Add(Index);
}

void AShiftPlayerController::DismissCoach()
{
	if (CoachCurrent >= 0)
	{
		CoachTimer = 0.0f;
	}
}

void AShiftPlayerController::TickTutorial(float DeltaTime)
{
	const bool bActive = bCoachEnabled && !bLessonRun && Screen == EShiftScreen::Play && !bAttract && Run.IsValid() && !Run->IsOver();
	if (!bActive)
	{
		CoachAlpha = 0.0f;
		CoachCurrent = -1;
		return;
	}
	if (bPaused)
	{
		return;
	}

	RunClock += DeltaTime;
	const ShiftSim::Sim& S = Run->GetSim();
	auto Seen = [this](int32 Index) { return (CoachSeenMask & (1 << Index)) != 0; };

	if (RunClock > 0.6f)
	{
		QueueCoach(TutJamStart);
	}
	if (HoverNode >= 0 && Seen(TutJamStart))
	{
		QueueCoach(TutFirstHover);
	}
	if (Seen(TutFirstHover) && !bPlayerActed && RunClock > 6.0f)
	{
		QueueCoach(TutFirstPlug);
	}
	if ((TutEventMask & EvPlug) != 0)
	{
		QueueCoach(TutFirstConvictionRing);
	}
	if (bPlayerActed && S.Posts() < 1.0)
	{
		QueueCoach(TutPostsLow);
	}
	if ((TutEventMask & EvReinforce) != 0)
	{
		QueueCoach(TutFirstReinforce);
	}
	if ((TutEventMask & EvGated) != 0)
	{
		QueueCoach(TutFirstGateSeen);
	}
	for (const int32 Node : GatedSeenNodes)
	{
		if (!S.IsGated(Node))
		{
			QueueCoach(TutFirstGateOpened);
		}
	}
	if (S.DayIndex() >= 8 && Run->GetPolicy().EyeAwayCount < 0.5)
	{
		QueueCoach(TutShiftUnlocked);
	}
	if ((TutEventMask & EvEyeAway) != 0)
	{
		QueueCoach(TutShiftUnlocked);   // the first Mod Eye window is the night shift's introduction: the plain card first, then the window's
		QueueCoach(TutEyeAway);
	}
	if (S.SockBoughtWeight() > 0.0)
	{
		QueueCoach(TutFake);
	}
	if (S.TotalSeconds() - FMath::RoundToInt(static_cast<float>(Run->GetPolicy().MegaDays)) * S.Jam().secPerDay - S.Time() <= 12.0)
	{
		QueueCoach(TutPush);
	}
	if (S.Heat() > 4.0)
	{
		QueueCoach(TutFirstHeat);
	}
	if (Run->ModsEnabled() && (S.Heat() > 4.0 || (TutEventMask & EvTelegraph) != 0))
	{
		QueueCoach(TutFirstModEye);
	}
	if ((TutEventMask & EvLost) != 0)
	{
		QueueCoach(TutFirstLostVoter);
	}
	if (!S.PendingAttacks().empty())
	{
		for (const ShiftSim::PendingAttack& A : S.PendingAttacks())
		{
			if (S.Opinion(A.node) == ShiftSim::kPlayerId)
			{
				QueueCoach(TutThreat);
				break;
			}
		}
	}
	if ((Seen(TutFirstLostVoter) || Seen(TutThreat)) && S.Posts() >= S.GetParams().RallyCost)
	{
		QueueCoach(TutRally);
	}
	if ((TutEventMask & EvSock) != 0)
	{
		QueueCoach(TutFirstSockpuppet);
	}
	if (S.GetParams().FinalAudit > 0.5 && S.TotalSeconds() - S.Time() <= FMath::Max(10.0, S.GetParams().SockReview))
	{
		QueueCoach(TutFinalAudit);
	}
	if ((TutEventMask & EvStrike) != 0)
	{
		QueueCoach(TutFirstStrike);
	}
	if ((TutEventMask & EvStrikeLast) != 0)
	{
		QueueCoach(TutBlameRule);   // one strike from the end: how the last chance works
	}
	if ((TutEventMask & EvPackHit) != 0)
	{
		QueueCoach(TutPackHit);
	}
	if (RunClock > 12.0f && !S.Disqualified() && S.Leader() == ShiftSim::kPlayerId)
	{
		QueueCoach(TutFirstLead);   // the other entries pile onto whoever leads
	}
	for (const ShiftSim::PendingAttack& A : S.PendingAttacks())
	{
		// a ring from an other colour on a voter of yours while it holds a grudge: it is coming for what you took
		if (A.who != S.RivalId() && S.Opinion(A.node) == ShiftSim::kPlayerId && S.Time() - S.LastTakenByPlayer(A.who) < Run->GetPolicy().GrudgeSeconds)
		{
			QueueCoach(TutGrudge);
			break;
		}
	}
	{
		int32 SympathyFrom = -1;
		if (S.SympathyPressure(&SympathyFrom, nullptr) > 0.0 && SympathyFrom == ShiftSim::kPlayerId)
		{
			QueueCoach(TutSympathy);   // a lead of more than 25 points makes voters root for the underdog
		}
	}
	if (RunClock > 80.0f)
	{
		QueueCoach(TutKeysHint);
	}
	TutEventMask = 0;

	if (CoachCurrent < 0)
	{
		CoachAlpha = FMath::Max(0.0f, CoachAlpha - DeltaTime / 0.35f);
		CoachCooldown -= DeltaTime;
		if (CoachQueue.Num() > 0 && CoachCooldown <= 0.0f)
		{
			const int32 Next = CoachQueue[0];
			CoachQueue.RemoveAt(0);
			const FString Text = ExpandTutorialText(FShiftContent::PickTutorial(TutTriggers[Next]));
			CoachSeenMask |= 1 << Next;
			if (bCoachPersist)
			{
				ShiftSave::SetTutorialSeenMask(CoachSeenMask);
			}
			if (!Text.IsEmpty())
			{
				PlaySfx(EShiftSfx::Tip);
				UE_LOG(LogShift, Log, TEXT("Coach card shown: %s at run time %.1f s"), TutTriggers[Next], RunClock);
				CoachCurrent = Next;
				CoachText = Text;
				CoachTimer = FMath::Clamp(3.5f + 0.055f * static_cast<float>(Text.Len()), 5.0f, 11.0f);
			}
		}
	}
	else
	{
		CoachAlpha = FMath::Min(1.0f, CoachAlpha + DeltaTime / 0.35f);
		CoachTimer -= DeltaTime;
		if (CoachTimer <= 0.0f)
		{
			CoachCurrent = -1;
			CoachCooldown = 1.2f;
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// model

void AShiftPlayerController::BuildHoverTooltip(FShiftHudTooltip& T) const
{
	T.bVisible = false;
	if (HoverNode < 0 || !Run.IsValid() || !Board)
	{
		return;
	}
	const ShiftSim::Sim& S = Run->GetSim();
	const ShiftSim::GraphNode& Node = S.GetGraph().nodes[HoverNode];

	T.bVisible = true;
	T.Cursor = EShiftCursor::Plug;
	T.Anchor = FVector2f(Node.x + ViewShift.X, Node.y + ViewShift.Y);
	T.AnchorRadius = Board->NodeRadius(HoverNode);
	T.Title = TEXT("@") + NodeHandle(HoverNode);

	const TCHAR* RoleName = TEXT("Voter");
	switch (Node.role)
	{
	case ShiftSim::NodeRole::LocalHub: RoleName = TEXT("Community hub"); break;
	case ShiftSim::NodeRole::Celebrity: RoleName = TEXT("Celebrity"); break;
	case ShiftSim::NodeRole::Bridge: RoleName = TEXT("Bridge"); break;
	default: break;
	}
	T.Line1 = FString::Printf(TEXT("%s  -  %d followers  -  weight %.0f"), RoleName, Node.followerCount, Node.weight);

	const int32 OwnerId = S.Opinion(HoverNode);
	T.bHasOwner = OwnerId >= 0;
	T.OwnerColor = OwnerId >= 0 ? ShiftPalette::Entry(OwnerId) : ShiftPalette::Undecided();
	T.Conviction = static_cast<float>(S.Conviction(HoverNode));
	T.Line2 = OwnerId >= 0 ? FString::Printf(TEXT("Backs %s (%.0f%% sure)"), *EntryName(OwnerId), T.Conviction * 100.0f) : FString(TEXT("Undecided"));

	const bool bNight = IsNightHeldNow();
	const ShiftSim::Params& P = S.GetParams();
	const FLinearColor SockColor = FLinearColor::FromSRGBColor(FColor(0xB7, 0x9B, 0xFF));
	if (OwnerId == ShiftSim::kPlayerId)
	{
		T.Cursor = EShiftCursor::Reinforce;
		int32 ThreatWho = -1;
		double ThreatLeft = 0.0;
		double ThreatTotal = 0.0;
		if (S.ThreatOn(HoverNode, &ThreatWho, &ThreatLeft, &ThreatTotal))
		{
			T.Status = FString::Printf(TEXT("%s plugs this voter in %.1fs"), *EntryName(ThreatWho), ThreatLeft);
			T.StatusColor = ShiftPalette::Entry(ThreatWho);
		}
		else if (S.ShieldRemaining(HoverNode) > 0.0)
		{
			T.Status = FString::Printf(TEXT("Shielded for %.1fs: nothing can plug it"), S.ShieldRemaining(HoverNode));
			T.StatusColor = TealColor();
		}
		else if (S.IsSockpuppet(HoverNode))
		{
			T.Status = P.SockLaunderConv <= 1.0
				? FString::Printf(TEXT("Fake vote (%.0f%% sure). Real at %.0f%%"), T.Conviction * 100.0f, P.SockLaunderConv * 100.0)
				: FString(TEXT("Sockpuppet: fragile, a strike takes it back"));
			T.StatusColor = SockColor;
		}
		else if (S.LockRemaining(HoverNode) <= 0.0 && S.SecondsToFlip(HoverNode) < 8.0)
		{
			// Who pushes: the neighbours (followed or following) that back the entry out-pushing this voter. Winning them is how to hold it.
			const int32 Challenger = S.Challenger(HoverNode);
			int32 Pushers = 0;
			if (Challenger >= 0)
			{
				for (const int32 Other : Node.follows) { Pushers += S.Opinion(Other) == Challenger ? 1 : 0; }
				for (const int32 Other : Node.followers) { Pushers += S.Opinion(Other) == Challenger ? 1 : 0; }
			}
			T.Status = Challenger >= 0 ? FString::Printf(TEXT("Leaves in ~%.0fs: %d neighbours back %s"), FMath::Max(1.0, S.SecondsToFlip(HoverNode)), Pushers, *EntryName(Challenger))
				: FString::Printf(TEXT("Under pressure: leaves in about %.0fs"), FMath::Max(1.0, S.SecondsToFlip(HoverNode)));
			T.StatusColor = AmberColor();
		}
		else
		{
			// The gates bind the rival too (Params::GatesApplyToAll): a hub of yours whose followers mostly back someone else is out of its reach.
			const ShiftSim::GateInfo RivalGate = S.Gate(HoverNode, S.RivalId());
			if (RivalGate.applies && !RivalGate.open)
			{
				T.Status = FString::Printf(TEXT("Rival locked out: %.0f%% back it, needs %.0f%%"), RivalGate.progress * 100.0, RivalGate.needed * 100.0);   // short: the card's status line fits about 45 characters
			}
			else
			{
				T.Status = TEXT("Yours. Safe for now");
			}
			T.StatusColor = TealColor();
		}
	}
	else if (S.LockRemaining(HoverNode) > 0.0)
	{
		T.Cursor = EShiftCursor::Blocked;
		T.Status = FString::Printf(TEXT("Immune for %.1fs"), S.LockRemaining(HoverNode));
		T.StatusColor = AmberColor();
	}
	else if (bNight && P.SockOverheatRefuse > 0.5 && S.Heat() + P.SockHeat >= P.HeatMax - 1.0e-9)
	{
		T.Cursor = EShiftCursor::Blocked;
		T.Status = TEXT("Overheated: let Heat cool before the next sockpuppet");
		T.StatusColor = RedColor();
	}
	else if (bNight)
	{
		T.Cursor = EShiftCursor::Sock;
		const double HeatAfter = FMath::Min(S.Heat() + P.SockHeat, P.HeatMax);
		const bool bRed = HeatAfter > P.HeatRedZone;
		T.Status = FString::Printf(TEXT("%s. Heat %.0f -> %.0f%s"), S.IsGated(HoverNode) ? TEXT("Sockpuppet: ignores the gate") : TEXT("Sockpuppet: fast but fragile"),
			S.Heat(), HeatAfter, bRed ? TEXT(" (red zone)") : TEXT(""));
		T.StatusColor = bRed ? RedColor() : SockColor;
	}
	else if (S.IsGated(HoverNode))
	{
		const ShiftSim::GateInfo Gate = S.Gate(HoverNode);
		T.Cursor = EShiftCursor::Blocked;
		T.Status = FString::Printf(TEXT("Gated: %.0f%% of followers back you, need %.0f%%"), Gate.progress * 100.0, Gate.needed * 100.0);
		T.StatusColor = RedColor();
	}
	else
	{
		const int32 Need = S.PlugsNeeded(ShiftSim::kPlayerId, HoverNode, 1.0, true);
		T.Cursor = Need >= ShiftSim::kUnreachable ? EShiftCursor::Blocked : EShiftCursor::Plug;
		T.Status = Need >= ShiftSim::kUnreachable ? FString(TEXT("Cannot be flipped right now")) : FString::Printf(TEXT("%d %s to flip it"), Need, Need == 1 ? TEXT("Plug") : TEXT("Plugs"));
		T.StatusColor = TealColor();
	}

	// What the player can do with this voter, and with which keys (a replay has no controls)
	if (Run->IsReplaying())
	{
		T.Cursor = EShiftCursor::Arrow;
		return;
	}
	auto PostsText = [](double Cost) -> FString
		{
			const double Whole = FMath::RoundToDouble(Cost);
			return FMath::Abs(Cost - Whole) < 0.05 ? FString::Printf(TEXT("%.0f %s"), Whole, Whole == 1.0 ? TEXT("Post") : TEXT("Posts")) : FString::Printf(TEXT("%.1f Posts"), Cost);
		};
	auto Add = [&T](const TCHAR* Key, const FString& Text, bool bAvailable)
		{
			FShiftHudAction& A = T.Actions.AddDefaulted_GetRef();
			A.Key = Key;
			A.Text = Text;
			A.bAvailable = bAvailable;
		};
	const bool bNightOk = !(bLessonRun && Lesson.IsValid() && !Lesson->NightAllowed());
	const double PostsHeld = S.Posts();
	const FString SockText = FString::Printf(TEXT("Sockpuppet: %s, +%.0f Heat"), S.IsGated(HoverNode) ? TEXT("skips the gate") : TEXT("fast but fake"), P.SockHeat);
	if (OwnerId == ShiftSim::kPlayerId)
	{
		int32 AttackerId = -1;
		double AttackLeft = 0.0;
		double AttackTotal = 0.0;
		if (S.ThreatOn(HoverNode, &AttackerId, &AttackLeft, &AttackTotal))
		{
			const double Cost = FMath::Max(1.0, P.ParryCost);
			Add(TEXT("CLICK"), FString::Printf(TEXT("Parry: stop the attack, %s"), *PostsText(Cost)), PostsHeld >= Cost);
		}
		else if (S.IsSockpuppet(HoverNode))
		{
			Add(TEXT("CLICK"), TEXT("Back it: make it a real vote, 1 Post"), PostsHeld >= 1.0);
		}
		else
		{
			Add(TEXT("CLICK"), TEXT("Reinforce: make it stronger, 1 Post"), PostsHeld >= 1.0);
		}
		if (S.RallyCooldownRemaining() > 0.0)
		{
			Add(TEXT("RIGHT-CLICK"), FString::Printf(TEXT("Rally: ready in %.0fs"), S.RallyCooldownRemaining()), false);
		}
		else
		{
			Add(TEXT("RIGHT-CLICK"), FString::Printf(TEXT("Rally: shield it and neighbours, %s"), *PostsText(P.RallyCost)), PostsHeld >= P.RallyCost);
		}
	}
	else if (S.LockRemaining(HoverNode) > 0.0)
	{
		Add(TEXT("CLICK"), FString::Printf(TEXT("Wait: immune for %.0fs"), FMath::Max(1.0, S.LockRemaining(HoverNode))), false);
	}
	else if (bNight)
	{
		const bool bHot = P.SockOverheatRefuse > 0.5 && S.Heat() + P.SockHeat >= P.HeatMax - 1.0e-9;
		Add(TEXT("SHIFT + CLICK"), bHot ? FString(TEXT("Too hot: wait for Heat to fall")) : SockText, !bHot);
	}
	else
	{
		const int32 Need = S.PlugsNeeded(ShiftSim::kPlayerId, HoverNode, 1.0, true);
		if (S.IsGated(HoverNode))
		{
			Add(TEXT("CLICK"), TEXT("Gated: win its followers first"), false);
		}
		else if (Need >= ShiftSim::kUnreachable)
		{
			Add(TEXT("CLICK"), TEXT("Cannot be flipped now"), false);
		}
		else
		{
			Add(TEXT("CLICK"), FString::Printf(TEXT("Plug: %s, %d more to flip"), *PostsText(P.PlayerPlugCost), Need), PostsHeld >= P.PlayerPlugCost);
		}
		if (bNightOk)
		{
			Add(TEXT("SHIFT + CLICK"), SockText, true);
		}
	}
}

FString AShiftPlayerController::BuildShareText() const
{
	const ShiftSim::Sim& S = Run->GetSim();
	const int32 Rank = S.RankOf(ShiftSim::kPlayerId) + 1;
	const double Total = FMath::Max(1.0, S.TotalWeight());
	const double Votes = S.Tally(ShiftSim::kPlayerId);
	const double SockVotes = S.SockpuppetWeight();
	const FShiftFlipStats& Flips = Run->GetFlipStats();
	return FString::Printf(TEXT("SHIFT HAPPENS - %s\n%s\n%s place of %d. Honest votes %.1f%%, sockpuppet votes %.1f%%, strikes %d. Won %d voters, %d drifted away, %d snatched.\n#GameOff2026"),
		UTF8_TO_TCHAR(S.Jam().name.c_str()), *ResultsModel.OutcomeTitle, *Ordinal(Rank), S.NumEntries(), (Votes - SockVotes) / Total * 100.0,
		SockVotes / Total * 100.0, S.Strikes(), Flips.Gains, Flips.LossHerd, Flips.LossRival + Flips.LossOthers);
}

void AShiftPlayerController::BuildResults(FShiftHudModel& Out) const
{
	const ShiftSim::Sim& S = Run->GetSim();
	const bool bDq = S.Disqualified();
	const int32 Rank = S.RankOf(ShiftSim::kPlayerId) + 1;
	const double Total = FMath::Max(1.0, S.TotalWeight());
	const double Votes = S.Tally(ShiftSim::kPlayerId);
	const double SockVotes = S.SockBoughtWeight();
	const FShiftFlipStats& Flips = Run->GetFlipStats();
	const FShiftResultFacts Facts = ShiftResults::MakeFacts(S, Flips, Run->GetBlameStats().Taken > 0, InitialShares);
	const int32 QualityRank = Facts.QualityRank;
	const bool bWon = Facts.bWon;

	FRandomStream Rng(static_cast<int32>(Seed * 31u + 7u));
	if (bDq)
	{
		Out.OutcomeTitle = TEXT("DISQUALIFIED");
		Out.OutcomeColor = RedColor();
	}
	else if (bWon)
	{
		Out.OutcomeTitle = TEXT("YOU WON THE JAM");
		Out.OutcomeColor = TealColor();
	}
	else
	{
		Out.OutcomeTitle = FString::Printf(TEXT("%s PLACE"), *Ordinal(Rank).ToUpper());
		Out.OutcomeColor = AmberColor();
	}

	FString Blurb = ShiftResults::PickBlurb(ShiftResults::Classify(Facts), Rng);
	if (Blurb.IsEmpty())
	{
		Blurb = bWon ? TEXT("The best-known entry won. Funny how that works.") : TEXT("Fame won again.");
	}
	const int32 Plays = FMath::Max(3, FMath::RoundToInt(static_cast<float>(Votes) * 0.035f));
	Blurb = Replace(Blurb, TEXT("{rank}"), Rank);
	Blurb = Replace(Blurb, TEXT("{votes}"), FMath::RoundToInt(static_cast<float>(Votes)));
	Blurb = Replace(Blurb, TEXT("{honest}"), FMath::RoundToInt(static_cast<float>(Votes - SockVotes)));
	Blurb = Replace(Blurb, TEXT("{sock}"), FMath::RoundToInt(static_cast<float>(SockVotes)));
	Blurb = Replace(Blurb, TEXT("{qualityRank}"), QualityRank);
	Blurb = Replace(Blurb, TEXT("{plays}"), Plays);
	Out.OutcomeLine1 = Blurb;
	Out.OutcomeLine2 = FString::Printf(TEXT("Your game: quality rank %d of %d, final rank %d. Honest votes %.1f%%, sockpuppet votes %.1f%%."),
		QualityRank, S.NumEntries(), Rank, (Votes - SockVotes) / Total * 100.0, SockVotes / Total * 100.0);
	FRandomStream WinkRng(static_cast<int32>(Seed * 53u + 11u));   // its own stream: the Wink line does not move when the blurb pool changes
	Out.OutcomeLine3 = FShiftContent::PickBlurb(TEXT("Wink"), TEXT("Closing"), WinkRng);
	{
		FRandomStream NewsRng(static_cast<int32>(Seed * 97u + 13u));
		if (!bDq)
		{
			Out.OutcomeNews = BuildHeadline(bWon ? TEXT("YouWin") : TEXT("RivalWins"), EntryName(S.Leader()), NewsRng);
			if (!bWon && S.Leader() != S.RivalId())
			{
				Out.OutcomeNews.Reset();   // RivalWins names the rival: only when it did win
			}
		}
	}
	Out.PlaysLine = FString::Printf(TEXT("%d votes cast for your game. About %d people played it. You won %d voters: %d drifted away, %d were snatched."), FMath::RoundToInt(static_cast<float>(Votes)),
		Plays, Flips.Gains, Flips.LossHerd, Flips.LossRival + Flips.LossOthers);
	if (Flips.SockGains > 0)
	{
		Out.PlaysLine += FString::Printf(TEXT(" Sockpuppets: %d flipped, %d made real, %d reverted by strikes, %d purged by the audit."), Flips.SockGains, Flips.Laundered, Flips.StrikeReverts, Flips.AuditPurges);
	}
	Out.HonestShare = static_cast<float>((Votes - SockVotes) / Total);
	Out.SockShare = static_cast<float>(SockVotes / Total);
	Out.bCanNextJam = JamIndex < 2;

	// The thesis, from this very run: does the better game or the more famous one get the votes?
	Out.QualityDots.Reset();
	Out.FameDots.Reset();
	for (int32 E = 0; E < S.NumEntries(); ++E)
	{
		FShiftHudDot Dot;
		Dot.Color = ShiftPalette::Entry(E);
		Dot.bPlayer = E == ShiftSim::kPlayerId;
		Dot.Y = static_cast<float>(S.Share(E));
		Dot.X = static_cast<float>(S.GetEntry(E).quality);
		Out.QualityDots.Add(Dot);
		Dot.X = InitialShares.IsValidIndex(E) ? InitialShares[E] : 0.0f;
		Out.FameDots.Add(Dot);
	}
	Out.QualityFit = FShiftHudFit::Compute(Out.QualityDots);
	Out.FameFit = FShiftHudFit::Compute(Out.FameDots);

	if (!bAttract && !bDq && !bQaRunFlag && !Run->HasBot())
	{
		ShiftSave::RecordResult(JamIndex, Rank);
	}
}

/** What the build comes to in play, in words and numbers, from the same rules the sim runs (ShiftBuildRules::Summary). */
void AShiftPlayerController::FillBuildEffects(FShiftHudBuild& B) const
{
	B.Hours[0] = Build.Quality;
	B.Hours[1] = Build.Thumbnail;
	B.Hours[2] = Build.Network;
	const ShiftSim::BuildSummary Sm = ShiftBuildRules::Summary(JamIndex, Build);
	const ShiftSim::Params Neutral;
	B.Effects[0] = FString::Printf(TEXT("Voters you win start %.0f%% sure, pull on neighbours %+.0f%%"), Sm.flipConv * 100.0, (Sm.appeal - 1.0) * 100.0);
	B.Effects[1] = FString::Printf(TEXT("A Plug costs %.2f Post, first look x%.2f"), Sm.plugCost, Sm.clout / FMath::Max(1.0e-6, Neutral.PlayerClout));
	B.Effects[2] = FString::Printf(TEXT("Post bank %.0f, refill %.2f/s, %d start voter%s"), Sm.postsMax, Sm.postsRegen, Sm.startNodes, Sm.startNodes == 1 ? TEXT("") : TEXT("s"));
}

void AShiftPlayerController::BuildMenuModel(FShiftHudModel& M)
{
	M.Screen = Screen;
	M.bEffectsOn = bEffectsOn;
	M.bMusicOn = bMusicOn;
	M.WindowMode = CurrentWindowMode();
	M.bSettingsOpen = bSettingsOpen;
	M.bLessonNudge = bLessonNudge;
	FillExtrasModel(M);
	FillAccessModel(M);
	M.bHowTo = bHowTo;
	if (bHowTo)
	{
		for (int32 Block = 1; Block <= FShiftContent::LessonStepCount(0); ++Block)
		{
			M.HowToText.Add(ExpandTutorialText(FShiftContent::LessonText(0, Block)));
		}
	}
	M.bShiftDown = IsNightHeldNow();
	M.Night = NightBlend;
	M.bNightActive = M.bShiftDown;
	M.JamCards = JamCards;
	M.SelectedJam = JamIndex;

	FShiftHudBuild& B = M.Build;
	FillBuildEffects(B);
	B.TotalHours = FShiftBuild::TotalHours;
	B.JamTitle = FString(UTF8_TO_TCHAR(ShiftSim::DefaultJam(JamIndex).name.c_str()));
	B.RivalFollowers = RivalFollowers(JamIndex);
	B.bForecastReady = bForecastReady;
	B.ForecastProgress = ForecastService.Progress();
	B.ForecastRank = Forecast.PlayerRank;
	B.ForecastRankLo = Forecast.RankLo;
	B.ForecastRankHi = Forecast.RankHi;
	B.ForecastShare = Forecast.PlayerShare;
	B.ForecastSpread = ForecastSpread;
	B.QualityRank = Forecast.QualityRank;
	for (const int32 E : Forecast.QualityOrder)
	{
		FShiftHudForecastRow Row;
		Row.EntryId = E;
		Row.Name = EntryName(E);
		Row.Value = Forecast.Quality.IsValidIndex(E) ? Forecast.Quality[E] : 0.0f;
		Row.Color = ShiftPalette::Entry(E);
		Row.bPlayer = E == ShiftSim::kPlayerId;
		B.ByQuality.Add(Row);
	}
	for (const int32 E : Forecast.ShareOrder)
	{
		FShiftHudForecastRow Row;
		Row.EntryId = E;
		Row.Name = EntryName(E);
		Row.Value = Forecast.Share.IsValidIndex(E) ? Forecast.Share[E] : 0.0f;
		Row.Color = ShiftPalette::Entry(E);
		Row.bPlayer = E == ShiftSim::kPlayerId;
		B.ByForecast.Add(Row);
	}
}

void AShiftPlayerController::BuildHudModel(FShiftHudModel& M)
{
	if (Screen != EShiftScreen::Play)
	{
		BuildMenuModel(M);
		return;
	}

	const ShiftSim::Sim& S = Run->GetSim();
	const ShiftSim::Params& P = S.GetParams();

	M.Screen = EShiftScreen::Play;
	M.bEffectsOn = bEffectsOn;
	M.bMusicOn = bMusicOn;
	M.WindowMode = CurrentWindowMode();
	M.bSettingsOpen = bSettingsOpen;
	M.JamName = FString(UTF8_TO_TCHAR(S.Jam().name.c_str()));
	const int32 Day = S.DayIndex();
	M.Day = Day + 1;
	M.Days = S.Jam().days;
	M.DateLabel = Day < 31 ? FString::Printf(TEXT("Dec %d"), Day + 1) : FString(TEXT("Jan 1"));
	M.DeadlineLabel = TEXT("voting closes Jan 1, 13:37 PST");
	M.RunProgress = static_cast<float>(S.Time() / FMath::Max(1.0, S.TotalSeconds()));

	const std::vector<int> Order = S.Ranking();
	for (size_t Rank = 0; Rank < Order.size(); ++Rank)
	{
		FShiftHudRow Row;
		const int32 E = Order[Rank];
		Row.EntryId = E;
		Row.Rank = static_cast<int32>(Rank);
		Row.Name = EntryName(E);
		Row.Share = static_cast<float>(S.Share(E));
		Row.Votes = static_cast<float>(S.Tally(E));
		Row.Trend = (ShareHistory.IsValidIndex(E) && ShareHistory[E].Num() >= 6) ? 100.0f * (Row.Share - ShareHistory[E][0]) : 0.0f;
		Row.Color = ShiftPalette::Entry(E);
		Row.bPlayer = E == ShiftSim::kPlayerId;
		Row.bRival = E == S.RivalId();
		M.Rows.Add(Row);
	}

	M.LeaderboardMode = LeaderboardMode;
	M.FakeShare = static_cast<float>(FMath::Max(S.AuditWeight(), S.SockBoughtWeight()) / FMath::Max(1.0, S.TotalWeight()));
	M.FakeLimit = P.SockHeldHeat > 0.0 ? static_cast<float>(FMath::Min(1.0, P.HeatDecay / P.SockHeldHeat)) : (P.SockTolerance < 1.0 ? static_cast<float>(P.SockTolerance) : 0.0f);
	M.RallyCooldown = static_cast<float>(S.RallyCooldownRemaining());
	M.RallyCost = static_cast<float>(P.RallyCost);
	M.Posts = static_cast<float>(S.Posts());
	M.PostsMax = static_cast<float>(P.PostsMax);
	M.Heat = static_cast<float>(S.Heat() / P.HeatMax);
	M.HeatRedZone = static_cast<float>(P.HeatRedZone / P.HeatMax);
	M.Strikes = S.Strikes();
	M.MaxStrikes = static_cast<int32>(P.MaxStrikes);
	M.ShadowbanSeconds = static_cast<float>(S.ShadowbanRemaining());
	const bool bNight = IsNightHeldNow();
	M.bShowHeat = true;
	M.bHeatActive = bNight || S.Heat() > 0.5 || S.Strikes() > 0;
	M.HeatProjected = bNight ? static_cast<float>(FMath::Min(S.Heat() + P.SockHeat, P.HeatMax) / P.HeatMax) : 0.0f;
	M.bModsEnabled = Run->ModsEnabled();
	M.NextSweepIn = static_cast<float>(Run->NextModSweep() - S.Time());
	const double SecondsLeft = S.TotalSeconds() - S.Time();
	M.FinalAuditIn = (P.FinalAudit > 0.5 && SecondsLeft > 0.0 && SecondsLeft <= FMath::Max(10.0, P.SockReview)) ? static_cast<float>(SecondsLeft) : -1.0f;
	M.Night = NightBlend;
	M.bNightActive = bNight;
	M.bPaused = bPaused;
	FillBuildEffects(M.Build);
	M.bOver = ResultsShown();   // after a disqualification the results wait a moment, so the alert is seen
	M.Feed.Reset();
	for (const FFeedItem& Item : Feed)
	{
		if (M.Feed.Num() >= 4)
		{
			break;
		}
		FShiftHudFeedItem Out;
		Out.Text = Item.Text;
		Out.Color = Item.Color;
		Out.bNews = Item.bNews;
		Out.bInk = Item.bInk;
		Out.Alpha = FMath::Clamp(Item.Remaining / 0.4f, 0.0f, 1.0f) * FMath::Clamp((Item.Duration - Item.Remaining) / 0.12f, 0.0f, 1.0f);
		M.Feed.Add(Out);
	}
	{
		const ShiftSim::PolicyParams& Pol = Run->GetPolicy();
		const double PushStart = S.TotalSeconds() - FMath::RoundToInt(static_cast<float>(Pol.MegaDays)) * S.Jam().secPerDay;
		const double ToPush = PushStart - S.Time();
		M.FinalPushIn = (ToPush > 0.0 && ToPush <= 12.0) ? static_cast<float>(ToPush) : -1.0f;
		M.FinalPushLeft = (ToPush <= 0.0 && SecondsLeft > 0.0) ? static_cast<float>(SecondsLeft) : -1.0f;
		M.FinalPushBonus = static_cast<float>(FMath::Max(0.0, Pol.FinalPushRegen - 1.0));
		M.RivalPosts = static_cast<float>(Run->RivalPosts());
		M.RivalPostsMax = static_cast<float>(Run->RivalPostsMax());
	}
	{
		int32 SympathyFrom = -1;
		const double Pressure = S.SympathyPressure(&SympathyFrom, nullptr);
		if (Pressure > 0.0 && P.Backlash > 0.0)
		{
			M.SympathyPoints = static_cast<float>(100.0 * Pressure / P.Backlash);
			M.SympathyLeader = EntryName(SympathyFrom);
		}
	}
	M.EyeAwayLeft = S.EyeAwayRemaining() > 0.0 ? static_cast<float>(S.EyeAwayRemaining()) : -1.0f;
	M.EyeAwayWarnIn = (Run->EyeWindowAnnounced() && S.EyeAwayRemaining() <= 0.0) ? static_cast<float>(FMath::Max(0.0, Run->NextEyeWindowStart() - S.Time())) : -1.0f;
	M.FakeHeatRate = static_cast<float>(P.SockHeldHeat * (S.EyeAwayRemaining() > 0.0 ? P.EyeAwayHeldMult : 1.0) * M.FakeShare);
	M.FakeInfo = FString::Printf(TEXT("Votes you bought with sockpuppets: %.0f%% of the vote right now, backed or not. Holding them heats you up by %.1f a second (you cool by %.0f a second), so above %.0f%% your Heat climbs. A strike undoes every one of them, and the final audit also purges any bought in the last %.0f s."),
		M.FakeShare * 100.0f, M.FakeHeatRate, P.HeatDecay, M.FakeLimit * 100.0f, P.SockReview);
	if (!bNight && !S.Disqualified() && S.ShadowbanRemaining() <= 0.0)
	{
		if (S.EyeAwayRemaining() > 0.0)
		{
			M.NightHint = FString::Printf(TEXT("free for %.0fs: hold SHIFT"), S.EyeAwayRemaining());
		}
		else
		{
			int32 GatedHub = -1;
			double GatedWeight = 80.0;
			for (int32 I = 0; I < S.NodeCount(); ++I)
			{
				if (S.GetGraph().IsGateNode(I) && S.IsGated(I) && S.Opinion(I) != ShiftSim::kPlayerId && S.LockRemaining(I) <= 0.0 && S.GetGraph().nodes[static_cast<size_t>(I)].weight > GatedWeight)
				{
					GatedWeight = S.GetGraph().nodes[static_cast<size_t>(I)].weight;
					GatedHub = I;
				}
			}
			if (GatedHub >= 0)
			{
				M.NightHint = FString::Printf(TEXT("SHIFT skips @%s's gate"), *NodeHandle(GatedHub));
			}
		}
	}
	M.CoachText = CoachText;
	M.CoachAlpha = CoachAlpha;
	M.bBlameOffer = Run->BlameAvailable();
	M.BlameSeconds = static_cast<float>(Run->BlameSecondsLeft());
	M.BlameWindow = static_cast<float>(Run->BlameWindowSeconds());
	FillAlertModel(M);
	if (bMarkers)
	{
		const ShiftSim::Sim& MS = Run->GetSim();
		for (int32 I = 0; I < MS.NodeCount(); ++I)
		{
			const int32 Holder = MS.Opinion(I);
			if (Holder >= 0)
			{
				const ShiftSim::GraphNode& N = MS.GetGraph().nodes[static_cast<size_t>(I)];
				FShiftHudModel::FMarker Marker;
				Marker.X = static_cast<float>(N.x + ViewShift.X);
				Marker.Y = static_cast<float>(N.y + ViewShift.Y);
				Marker.Owner = Holder;
				M.Markers.Add(Marker);
			}
		}
	}
	BuildHoverTooltip(M.Tooltip);

	if (M.bOver)
	{
		if (!bResultsBuilt)
		{
			BuildResults(ResultsModel);
			bResultsBuilt = true;
			FinishAchievements();
			SaveRecording(TEXT("abandoned"));
			if (bReplayMode && Run->IsReplaying())
			{
				const FString Hash = FString::Printf(TEXT("%016llx"), static_cast<unsigned long long>(S.StateHash()));
				UE_LOG(LogShift, Display, TEXT("Replay: finished at %.1fs, hash %s, recorded %s: %s"), S.Time(), *Hash, *ReplaySource.Hash, Hash == ReplaySource.Hash ? TEXT("MATCH") : TEXT("DIVERGED"));
			}
			if (!S.Disqualified())
			{
				PlaySfx(S.Leader() == ShiftSim::kPlayerId ? EShiftSfx::Win : EShiftSfx::Lose);   // the disqualification sounded when it happened
			}
		}
		M.OutcomeTitle = ResultsModel.OutcomeTitle;
		M.OutcomeLine1 = ResultsModel.OutcomeLine1;
		M.OutcomeLine2 = ResultsModel.OutcomeLine2;
		M.OutcomeLine3 = ResultsModel.OutcomeLine3;
		M.OutcomeNews = ResultsModel.OutcomeNews;
		M.NewAchievements = ResultsModel.NewAchievements;
		M.PlaysLine = ResultsModel.PlaysLine;
		M.OutcomeColor = ResultsModel.OutcomeColor;
		M.HonestShare = ResultsModel.HonestShare;
		M.SockShare = ResultsModel.SockShare;
		M.bCanNextJam = ResultsModel.bCanNextJam;
		M.QualityDots = ResultsModel.QualityDots;
		M.FameDots = ResultsModel.FameDots;
		M.QualityFit = ResultsModel.QualityFit;
		M.FameFit = ResultsModel.FameFit;
		M.RecordingPath = RecordingPath;
	}
	M.bReplay = Run->IsReplaying();
	M.bInLesson = bLessonRun;
	if (bLessonRun && Lesson.IsValid())
	{
		M.Lesson = Lesson->View();
		for (FShiftLessonSpot& Spot : M.Lesson.Spots)
		{
			Spot.Center += FVector2f(ViewShift.X, ViewShift.Y);
		}
		M.bModsEnabled = M.bModsEnabled && M.Lesson.bModEye;
	}
	FillAccessModel(M);
	M.bHowTo = bHowTo;
	if (bHowTo)
	{
		for (int32 Block = 1; Block <= FShiftContent::LessonStepCount(0); ++Block)
		{
			M.HowToText.Add(ExpandTutorialText(FShiftContent::LessonText(0, Block)));
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// tick

void AShiftPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (ShiftGlass::IsActive())
	{
		// the viewport can rebuild its show flags (window creation, resize), so the invert is held every frame
		if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		{
			if (!Viewport->EngineShowFlags.AlphaInvert)
			{
				Viewport->EngineShowFlags.SetAlphaInvert(true);
				UE_LOG(LogShift, Display, TEXT("Glass: the alpha invert was off and is on again"));
			}
			// QA: -ShiftGlassDiag=<file> writes the state the see-through window depends on (a Shipping build has no log), once, a few seconds in
			static bool bDiagDone = false;
			FString DiagFile;
			if (!bDiagDone && GetWorld()->GetTimeSeconds() > 4.0f && FParse::Value(FCommandLine::Get(), TEXT("ShiftGlassDiag="), DiagFile))
			{
				bDiagDone = true;
				auto Cvar = [](const TCHAR* Name) -> FString
					{
						IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name);
						return Var ? Var->GetString() : FString(TEXT("(missing)"));
					};
				FString Text;
				Text += FString::Printf(TEXT("active=%d\nAlphaInvert=%d\n"), ShiftGlass::IsActive() ? 1 : 0, Viewport->EngineShowFlags.AlphaInvert ? 1 : 0);
				for (const TCHAR* Name : { TEXT("r.PostProcessing.PropagateAlpha"), TEXT("r.TransparentViewportClear"), TEXT("r.ScreenPercentage"), TEXT("r.DefaultBackBufferPixelFormat"), TEXT("r.D3D12.UseAllowTearing"), TEXT("r.AntiAliasingMethod") })
				{
					Text += FString::Printf(TEXT("%s=%s\n"), Name, *Cvar(Name));
				}
				FFileHelper::SaveStringToFile(Text, *DiagFile);
			}
		}
	}
	if (AudioMeter && !AudioMeter->Tick(DeltaTime))
	{
		const int32 Failures = AudioMeter->GetFailures();
		AudioMeter = nullptr;
		UE_LOG(LogShift, Log, TEXT("AudioMeter finished: %s"), Failures == 0 ? TEXT("all checks passed") : TEXT("FAILURES, see the [FAIL] lines above"));
		FPlatformMisc::RequestExitWithStatus(false, Failures == 0 ? 0 : 3);
		return;
	}
#if !UE_BUILD_SHIPPING
	if (Trailer)
	{
		Trailer->Tick(DeltaTime);   // may start a run or skip sim time, so before the run is looked at
		if ((!Run.IsValid() || !Board) && Hud.IsValid())
		{
			// the game is still starting: the trailer's black cover already hides it
			FShiftHudModel Cover;
			Trailer->FillModel(Cover.Trailer);
			Hud->SetModel(MoveTemp(Cover));
		}
	}
#endif
	if (!Run.IsValid() || !Board)
	{
		return;
	}

	FrameSum += DeltaTime;
	FrameMax = FMath::Max(FrameMax, static_cast<double>(DeltaTime));
	++FrameCount;

	ScriptClock += DeltaTime;
	while (ScriptedUi.Num() > 0 && ScriptedUi[0].Time <= ScriptClock)
	{
		const FScriptedUi Action = ScriptedUi[0];
		ScriptedUi.RemoveAt(0);
		UE_LOG(LogShift, Log, TEXT("scripted ui at t=%.1f id=%d value=%.2f"), Action.Time, Action.Id, Action.Value);
		HandleUi(Action.Id, Action.Value);
		if (!Run.IsValid() || !Board)
		{
			return;
		}
	}
	while (ScriptedMouse.Num() > 0 && ScriptedMouse[0].Time <= ScriptClock)
	{
		const FScriptedMouse Event = ScriptedMouse[0];
		ScriptedMouse.RemoveAt(0);
		UE_LOG(LogShift, Log, TEXT("scripted mouse at t=%.1f kind=%d design=(%.0f,%.0f)"), Event.Time, Event.Kind, Event.Design.X, Event.Design.Y);
		if (Hud.IsValid())
		{
			Hud->QaMouse(Event.Kind, Event.Design);
		}
		if (!Run.IsValid() || !Board)
		{
			return;
		}
	}
	while (Screen == EShiftScreen::Play && ScriptedClicks.Num() > 0 && ScriptedClicks[0].Time <= ScriptClock)
	{
		const FScriptedClick Click = ScriptedClicks[0];
		ScriptedClicks.RemoveAt(0);
		UE_LOG(LogShift, Log, TEXT("scripted click at t=%.1f design=(%.0f,%.0f) night=%d"), Click.Time, Click.Design.X, Click.Design.Y, Click.bNight ? 1 : 0);
		if (bPseudoCursor)
		{
			// The scripted cursor rings where it clicks, and the click is aimed in the widget's own pixels so the footage is right at any window size.
			PseudoClick(Click.Design, Click.bNight ? 1 : 0);
			HandleHudClick(Hud.IsValid() ? FVector2D(Hud->DesignToLocal(FVector2f(Click.Design))) : Click.Design, Click.bNight, false);
		}
		else
		{
			HandleHudClick(Click.Design, Click.bNight, false);
		}
	}

	if (Screen != EShiftScreen::Play && Run->IsOver())
	{
		StartAttract();   // the attract run reached the end: start another so the title never goes still
	}

	if (FShiftJamRegistry::Get().Version() != LastRegistryVersion)
	{
		RefreshJamCards();   // a jam pack finished activating (or went away)
	}
	const bool bPlaying = Screen == EShiftScreen::Play;
	if (bAutoPauseOnFocusLoss && bPlaying && !bPaused && !Run->IsOver() && !FPlatformApplicationMisc::IsThisApplicationForeground())
	{
		bPaused = true;
		bNightLatch = false;
	}
	const bool bNight = IsNightHeldNow();
	Run->SetNightHeld(bPlaying && bNight);
	// the world stands still in the pause menu, behind the reference page, and while a lesson card is being read
	Run->SetPaused(bPlaying && (bPaused || bHowTo || (bLessonRun && Lesson.IsValid() && Lesson->IsFrozen())));
	Run->Tick(DeltaTime);

	const float NightTarget = NightOverride >= 0.0f ? NightOverride : (bNight ? 1.0f : 0.0f);
	NightBlend = FMath::FInterpTo(NightBlend, NightTarget, DeltaTime, 7.0f);

	UpdateView(DeltaTime);
	TickPseudoCursor(DeltaTime);
	if (bFakeCursor)
	{
		CursorWorld = Board->SimToWorld(FakeCursorDesign.X - ViewShift.X, FakeCursorDesign.Y - ViewShift.Y);
	}
	else
	{
		float MouseX = 0.0f, MouseY = 0.0f;
		FVector WorldLoc, WorldDir;
		if (GetMousePosition(MouseX, MouseY) && DeprojectScreenPositionToWorld(MouseX, MouseY, WorldLoc, WorldDir))
		{
			CursorWorld = FVector2D(WorldLoc.X, WorldLoc.Y);
		}
	}
	HoverNode = (bPlaying && !bPaused && !bHowTo && !bSettingsOpen && !Run->IsOver()) ? Board->NodeAtWorld(CursorWorld) : -1;

	EventScratch.Reset();
	Run->DrainEvents(EventScratch);
	if (bPlaying && !bAttract)
	{
		TickLesson(DeltaTime);
		HandleEvents(EventScratch);
		TickLostVoters(DeltaTime);
		TickShareTrend();
		TickChatter(DeltaTime);
		if (Board && Run->SpotlightNode() >= 0 && !Run->HasBot() && !bPaused)
		{
			SpotFlashClock += DeltaTime;
			if (SpotFlashClock >= 0.9f)
			{
				SpotFlashClock = 0.0f;
				Board->FlashNode(Run->SpotlightNode());
			}
		}
		if (AchEligible() && !Run->IsOver() && !bPaused)
		{
			Ach.Tick(Run->GetSim(), Run->GetFlipStats(), DeltaTime, IsNightHeldNow());
			PollAchievements();
		}
		if (!Run->IsOver() && !bPaused && !bLessonRun)
		{
			IdleClock += DeltaTime;
			if (IdleClock >= 10.0f)
			{
				IdleClock = 0.0f;
				Chatter(TEXT("Idle"), FString(), 0.0f);
			}
			// a gate that opened for the player: a voter says so, and the hub flashes
			const ShiftSim::Sim& GS = Run->GetSim();
			for (const int32 Node : GatedSeenNodes)
			{
				if (!GS.IsGated(Node) && !GateChatted.Contains(Node))
				{
					GateChatted.Add(Node);
					Chatter(TEXT("GateOpen"), NodeHandle(Node), 0.0f);
					if (Board)
					{
						Board->FlashNode(Node);
					}
				}
			}
		}

		const ShiftSim::Sim& Sim = Run->GetSim();
		const int32 Leader = Sim.Leader();
		if (LastLeader >= 0 && Leader != LastLeader && !Run->IsOver() && !bLessonRun)
		{
			ShowHeadline(TEXT("LeadChange"), EntryName(Leader));
		}
		LastLeader = Leader;
		const int32 Day = Sim.DayIndex();
		if (Day != LastHeadlineDay)
		{
			LastHeadlineDay = Day;
			if (Day == Sim.Jam().days - 3)
			{
				ShowHeadline(TEXT("MegaCampaign"), EntryName(Sim.RivalId()));
			}
			else if (Day == Sim.Jam().days - 1)
			{
				ShowHeadline(TEXT("FinalHours"), EntryName(Leader));
			}
		}
	}
	for (FFeedItem& Item : Feed)
	{
		Item.Remaining -= DeltaTime;
	}
	Feed.RemoveAll([](const FFeedItem& Item) { return Item.Remaining <= 0.0f; });
	SliderSfxCooldown = FMath::Max(0.0f, SliderSfxCooldown - DeltaTime);
	TickTutorial(DeltaTime);
	TickAlerts(DeltaTime);
	TickAudio(DeltaTime);

	if (Screen == EShiftScreen::Build)
	{
		RefreshForecast();
	}

	const ShiftSim::Sim& S = Run->GetSim();
	FShiftBoardView View;
	View.CursorWorld = CursorWorld;
	View.HoverNode = HoverNode;
	View.Time = GetWorld()->GetTimeSeconds();
	View.Night = NightBlend;
	View.Heat = static_cast<float>(S.Heat() / S.GetParams().HeatMax);
	View.Beat = Audio ? Audio->BeatPulse() : 0.0f;
	View.EdgeMode = EdgeMode;
	View.BackgroundOpacity = static_cast<float>(GlassOpacityPct) / 100.0f;
	RallyFxAge = FMath::Min(RallyFxAge + DeltaTime, 99.0f);
	View.RallyNode = RallyFxAge < 1.2f ? RallyFxNode : -1;
	Board->Update(S, View, DeltaTime);

	if (Hud.IsValid())
	{
		FShiftHudModel Model;
		BuildHudModel(Model);
		Model.bSystemCursor = bSystemCursor;
		Model.bPseudoCursor = bPseudoCursor && bPseudoShown;
		Model.PseudoCursor = FVector2f(PseudoPos);
#if !UE_BUILD_SHIPPING
		if (Trailer)
		{
			Trailer->FillModel(Model.Trailer);
			Model.bPseudoCursor = Model.bPseudoCursor && !Model.Trailer.bHideCursor;
		}
#endif
		Hud->SetModel(MoveTemp(Model));
	}
}

void AShiftPlayerController::PseudoMoveTo(const FVector2D& Design, float Seconds)
{
	if (!bPseudoShown)
	{
		// the first waypoint: the cursor enters from the lower right instead of appearing out of nowhere
		bPseudoShown = true;
		PseudoPos = FVector2D(FMath::Clamp(Design.X + 260.0, 40.0, 1240.0), FMath::Clamp(Design.Y + 170.0, 40.0, 700.0));
	}
	PseudoFrom = PseudoPos;
	PseudoTo = Design;
	PseudoClock = 0.0f;
	PseudoSeconds = FMath::Max(0.02f, Seconds);
}

void AShiftPlayerController::PseudoClick(const FVector2D& Design, int32 Kind)
{
	if (!bPseudoShown || FVector2D::Distance(PseudoPos, Design) > 6.0)
	{
		PseudoMoveTo(Design, bPseudoShown ? 0.12f : 0.3f);
	}
	if (Hud.IsValid())
	{
		Hud->PushRipple(FVector2f(Design), Kind);
	}
}

void AShiftPlayerController::TickPseudoCursor(float DeltaTime)
{
	if (!bPseudoCursor || !Run.IsValid())
	{
		return;
	}

	// Scripted clicks: head for the next one so the cursor has arrived and rested about half a second before it lands.
	if (Screen == EShiftScreen::Play && ScriptedClicks.Num() > 0)
	{
		const FScriptedClick& Next = ScriptedClicks[0];
		if (Next.Time != PseudoPlannedClick)
		{
			const float Travel = FMath::Clamp(static_cast<float>(FVector2D::Distance(PseudoPos, Next.Design)) / 1500.0f + 0.2f, 0.25f, 0.65f);
			if (ScriptClock >= Next.Time - (Travel + 0.45f))
			{
				PseudoMoveTo(Next.Design, FMath::Clamp(Next.Time - ScriptClock - 0.2f, 0.12f, Travel));
				PseudoPlannedClick = Next.Time;
			}
		}
	}

	// A bot plays: the cursor follows each of its actions and rings on the voter it acted on.
	if (Run->HasBot())
	{
		const int32 Count = Run->BotActionCount();
		if (Count != PseudoBotActions)
		{
			PseudoBotActions = Count;
			const int32 Node = Run->BotLastNode();
			if (Node >= 0 && Node < Run->GetSim().NodeCount())
			{
				const ShiftSim::GraphNode& N = Run->GetSim().GetGraph().nodes[static_cast<size_t>(Node)];
				UE_LOG(LogShift, Log, TEXT("Pseudo bot action %d: node %d at design (%.0f,%.0f) kind %d, sim time %.1f"), Count, Node, N.x, N.y, Run->BotLastKind(), Run->GetSim().Time());
				PseudoClick(SimToDesign(N.x, N.y), Run->BotLastKind());
			}
		}
	}

	if (PseudoClock < PseudoSeconds)
	{
		PseudoClock = FMath::Min(PseudoClock + DeltaTime, PseudoSeconds);
		const double T = static_cast<double>(PseudoClock) / static_cast<double>(PseudoSeconds);
		const double Ease = T * T * T * (T * (T * 6.0 - 15.0) + 10.0);   // smootherstep: it leaves and arrives gently
		PseudoPos = PseudoFrom + (PseudoTo - PseudoFrom) * Ease;
	}
	if (bPseudoShown)
	{
		bFakeCursor = true;   // the hover card and the voter highlight follow the scripted cursor
		FakeCursorDesign = PseudoPos;
	}
}

void AShiftPlayerController::TickAudio(float DeltaTime)
{
	if (!Audio || !Run.IsValid())
	{
		return;
	}
	const ShiftSim::Sim& S = Run->GetSim();
	const ShiftSim::Params& P = S.GetParams();
	const bool bPlaying = Screen == EShiftScreen::Play && !bAttract;
	const float HeatFrac = static_cast<float>(S.Heat() / P.HeatMax);
	const float RedFrac = static_cast<float>(P.HeatRedZone / P.HeatMax);

	// music: night crossfade from the SHIFT key, tension from heat and from the closing days
	float Intensity = 0.0f;
	float Volume = 1.0f;
	if (bPlaying)
	{
		const float Progress = static_cast<float>(S.Time() / FMath::Max(1.0, S.TotalSeconds()));
		Intensity = FMath::Max(HeatFrac, FMath::Clamp((Progress - 0.78f) / 0.22f, 0.0f, 1.0f));
		if (bPaused)
		{
			Volume = 0.40f;
		}
		else if (Run->IsOver())
		{
			Volume = 0.55f;
			Intensity = 0.0f;
		}
		else if (Run->BlameAvailable())
		{
			Volume = 0.5f;   // the music steps back while the Blame offer is open, so its warning is heard
		}
	}
	Audio->Tick(DeltaTime, NightBlend, Intensity, Volume);

	if (!bPlaying || bPaused || Run->IsOver())
	{
		LastHoverNode = HoverNode;
		return;
	}

	const bool bNight = IsNightHeldNow();
	if (bNight != bPrevNight)
	{
		PlaySfx(bNight ? EShiftSfx::NightOn : EShiftSfx::NightOff);
		bPrevNight = bNight;
	}

	HeartbeatTimer -= DeltaTime;
	if (HeatFrac > RedFrac && HeartbeatTimer <= 0.0f)
	{
		const float Over = FMath::Clamp((HeatFrac - RedFrac) / FMath::Max(0.01f, 1.0f - RedFrac), 0.0f, 1.0f);
		PlaySfx(EShiftSfx::Heartbeat, 0.5f + 0.5f * Over);
		HeartbeatTimer = FMath::Lerp(1.05f, 0.55f, Over);
	}

	const double ToSweep = Run->NextModSweep() - S.Time();
	if (Run->ModsEnabled() && (bNight || S.Heat() > 0.5 || S.Strikes() > 0) && ToSweep < 3.0 && ToSweep > 0.0 && LastWarnedSweep != Run->NextModSweep())
	{
		PlaySfx(EShiftSfx::ModWarn);
		LastWarnedSweep = Run->NextModSweep();
	}
	const double ToAudit = S.TotalSeconds() - S.Time();
	if (S.GetParams().FinalAudit > 0.5 && ToAudit <= FMath::Max(10.0, S.GetParams().SockReview) && ToAudit > 0.0 && !bAuditWarned)
	{
		PlaySfx(EShiftSfx::ModWarn);
		bAuditWarned = true;   // once per run: the HUD banner counts the last seconds down
	}

	if (HoverNode != LastHoverNode && HoverNode >= 0)
	{
		PlaySfx(EShiftSfx::Hover);
	}
	LastHoverNode = HoverNode;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// QA hooks

void AShiftPlayerController::ScheduleScreenshotFromCommandLine()
{
	FString Path;
	if (!FParse::Value(FCommandLine::Get(), TEXT("ShiftShot="), Path) || Path.IsEmpty())
	{
		return;
	}

	float Delay = 4.0f;
	FParse::Value(FCommandLine::Get(), TEXT("ShiftShotDelay="), Delay);

	ShotPath = FPaths::ConvertRelativePathToFull(Path);
	bQuitAfterShot = FParse::Param(FCommandLine::Get(), TEXT("ShiftQuit"));

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(ShotPath), /*Tree*/ true);

	// -ShiftShotTimes=1.0,2.5,...: one screenshot per time in a single run, named <path without .png>_<time in hundredths>.png (the trailer is authored with these)
	FString TimesText;
	if (FParse::Value(FCommandLine::Get(), TEXT("ShiftShotTimes="), TimesText, false))
	{
		TArray<FString> Parts;
		TimesText.ParseIntoArray(Parts, TEXT(","));
		MultiShotTimers.SetNum(Parts.Num());
		float Last = 0.0f;
		for (int32 i = 0; i < Parts.Num(); ++i)
		{
			const float Time = FMath::Max(0.1f, FCString::Atof(*Parts[i]));
			Last = FMath::Max(Last, Time);
			const FString File = FPaths::GetPath(ShotPath) / FString::Printf(TEXT("%s_%04d.png"), *FPaths::GetBaseFilename(ShotPath), FMath::RoundToInt(Time * 100.0f));
			GetWorldTimerManager().SetTimer(MultiShotTimers[i], FTimerDelegate::CreateWeakLambda(this, [File]()
				{
					FScreenshotRequest::RequestScreenshot(File, /*bInShowUI*/ true, /*bAddFilenameSuffix*/ false);
				}), Time, false);
		}
		if (bQuitAfterShot)
		{
			GetWorldTimerManager().SetTimer(QuitTimer, this, &AShiftPlayerController::QuitNow, Last + 2.5f, false);
		}
		UE_LOG(LogShift, Log, TEXT("%d screenshots scheduled (-ShiftShotTimes), the last at %.1fs (quit after: %d)"), Parts.Num(), Last, bQuitAfterShot ? 1 : 0);
		return;
	}

	GetWorldTimerManager().SetTimer(ShotTimer, this, &AShiftPlayerController::TakeScreenshot, FMath::Max(0.1f, Delay), false);
	UE_LOG(LogShift, Log, TEXT("Screenshot scheduled in %.1fs -> %s (quit after: %d)"), Delay, *ShotPath, bQuitAfterShot ? 1 : 0);
}

void AShiftPlayerController::LogWorldDiagnostics() const
{
	UE_LOG(LogShift, Log, TEXT("--- world diagnostics (%s) ---"), GetWorld() ? *GetWorld()->GetMapName() : TEXT("no world"));
	if (PlayerCameraManager)
	{
		const FMinimalViewInfo& View = PlayerCameraManager->GetCameraCacheView();
		UE_LOG(LogShift, Log, TEXT("View: loc=%s rot=%s projection=%d orthoWidth=%.1f aspect=%.3f viewTarget=%s"),
			*View.Location.ToString(), *View.Rotation.ToString(), static_cast<int32>(View.ProjectionMode), View.OrthoWidth,
			View.AspectRatio, GetViewTarget() ? *GetViewTarget()->GetName() : TEXT("null"));
	}
	if (FrameCount > 0)
	{
		UE_LOG(LogShift, Log, TEXT("Frame time: avg %.2f ms (%.0f fps), max %.2f ms over %d frames"),
			FrameSum / FrameCount * 1000.0, FrameCount / FMath::Max(FrameSum, 1.0e-6), FrameMax * 1000.0, FrameCount);
	}
	if (Run.IsValid())
	{
		const ShiftSim::Sim& S = Run->GetSim();
		UE_LOG(LogShift, Log, TEXT("Sim: screen=%d t=%.1fs day=%d posts=%.1f heat=%.0f strikes=%d over=%d leader=%d you=%.1f%% (rank %d) rival=%.1f%%"),
			static_cast<int32>(Screen), S.Time(), S.DayIndex() + 1, S.Posts(), S.Heat(), S.Strikes(), Run->IsOver() ? 1 : 0, S.Leader(),
			S.Share(ShiftSim::kPlayerId) * 100.0, S.RankOf(ShiftSim::kPlayerId) + 1, S.Share(S.RivalId()) * 100.0);
	}
}

void AShiftPlayerController::TakeScreenshot()
{
	LogWorldDiagnostics();
	FScreenshotRequest::RequestScreenshot(ShotPath, /*bInShowUI*/ true, /*bAddFilenameSuffix*/ false);
	UE_LOG(LogShift, Log, TEXT("Screenshot requested: %s"), *ShotPath);

	if (bQuitAfterShot)
	{
		GetWorldTimerManager().SetTimer(QuitTimer, this, &AShiftPlayerController::QuitNow, 2.0f, false);
	}
}

void AShiftPlayerController::QuitNow()
{
	UE_LOG(LogShift, Log, TEXT("Quitting (-ShiftQuit)."));
	FPlatformMisc::RequestExit(false);
}
