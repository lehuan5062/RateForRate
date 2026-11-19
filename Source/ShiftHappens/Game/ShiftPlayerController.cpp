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
#include "Containers/StringConv.h"
#include "Containers/UnrealString.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/EngineTypes.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Game/ShiftContent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/Platform.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformApplicationMisc.h"
#include "InputCoreTypes.h"
#include "Jams/ShiftJamPack.h"
#include "Logging/LogMacros.h"
#include "Math/Color.h"
#include "Math/MathFwd.h"
#include "Math/RandomStream.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/CommandLine.h"
#include "Misc/CString.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ShiftBuild.h"
#include "ShiftHappens.h"
#include "ShiftRun.h"
#include "Sim/ShiftGraph.h"
#include "Sim/ShiftParams.h"
#include "Sim/ShiftSim.h"
#include <string>
#include "Templates/SharedPointer.h"
#include "Templates/UnrealTemplate.h"
#include "TimerManager.h"
#include "UI/ShiftHudModel.h"
#include "UI/SShiftHud.h"
#include "UnrealClient.h"
#include "UObject/UObjectGlobals.h"
#include <vector>
#include "Widgets/DeclarativeSyntaxSupport.h"

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
		default: return TEXT("72 voters, two celebrities and a Mega Campaign in the last days. The rival has 1.2M followers.");
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
		TutCount
	};

	const TCHAR* const TutTriggers[TutCount] =
	{
		TEXT("JamStart"), TEXT("FirstHover"), TEXT("FirstPlug"), TEXT("FirstConvictionRing"), TEXT("PostsLow"), TEXT("FirstReinforce"),
		TEXT("FirstGateSeen"), TEXT("FirstGateOpened"), TEXT("ShiftUnlocked"), TEXT("FirstHeat"), TEXT("FirstModEye"), TEXT("FirstStrike"),
		TEXT("BlameAvailable"), TEXT("KeysHint"), TEXT("FirstLostVoter"), TEXT("FirstSockpuppet"), TEXT("FinalAudit"), TEXT("RallyHint"), TEXT("ThreatRing"), TEXT("EyeAway"), TEXT("FirstFake"), TEXT("FinalPush"), TEXT("BlameRule"), TEXT("FirstLead"), TEXT("PackHit"), TEXT("Sympathy"),
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

	const TCHAR* Cmd = FCommandLine::Get();
	bool bQaRun = false;

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
	bQaRun |= FParse::Param(Cmd, TEXT("ShiftBot")) || FString(Cmd).Contains(TEXT("ShiftBot=")) || FString(Cmd).Contains(TEXT("ShiftClicks="))
		|| FString(Cmd).Contains(TEXT("ShiftWarp=")) || FString(Cmd).Contains(TEXT("ShiftSpeed="));
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
				Event.Kind = F[3] == TEXT("d") ? 1 : (F[3] == TEXT("u") ? 2 : 0);
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
	bMuted = ShiftSave::IsMuted();
	EdgeMode = ShiftSave::EdgeMode();
	LeaderboardMode = ShiftSave::LeaderboardMode();
	ApplyMute();

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

	if (Start == EShiftScreen::Play)
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
	}

	if (!FParse::Param(Cmd, TEXT("ShiftNoUI")) && GEngine && GEngine->GameViewport)
	{
		Hud = SNew(SShiftHud)
			.OnClick(FOnShiftHudClick::CreateUObject(this, &AShiftPlayerController::HandleHudClick))
			.OnUi(FOnShiftHudUi::CreateUObject(this, &AShiftPlayerController::HandleUi))
			.OnKey(FOnShiftHudKey::CreateUObject(this, &AShiftPlayerController::HandleHudKey));
		GEngine->GameViewport->AddViewportWidgetContent(Hud.ToSharedRef(), 10);

		FInputModeGameAndUI Mode;
		Mode.SetWidgetToFocus(Hud);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		SetInputMode(Mode);
	}

	ScheduleScreenshotFromCommandLine();
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
	bAttract = bInAttract;
	const int32 RunJam = FMath::Clamp(InJamIndex, 0, 2);
	if (!bAttract)
	{
		JamIndex = RunJam;
		Seed = InSeed;
	}

	Run = MakeShared<FShiftRun>();
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
	if (!Run->Start(RunJam, InSeed, bAttract ? FShiftBuild() : Build, /*bInModsEnabled*/ true))
	{
		Run.Reset();
		return;
	}
	CosmeticRng.Initialize(static_cast<int32>(InSeed * 7919u + 13u));
	if (!FParse::Param(FCommandLine::Get(), TEXT("ShiftNoStateTree")))
	{
		Run->SetDriver(FShiftBehavior::Create(*Run));   // null (native policies) when the StateTree assets are missing
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
		if (FParse::Value(FCommandLine::Get(), TEXT("ShiftBot="), BotName))
		{
			ShiftSim::BotKind Kind = ShiftSim::BotKind::Idle;
			if (!ShiftSim::BotKindFromName(std::string(TCHAR_TO_UTF8(*BotName)), &Kind))
			{
				Kind = ShiftSim::BotKind::Idle;   // an unknown name plays nothing
			}
			Run->SetBot(Kind);
		}
		if (FParse::Value(FCommandLine::Get(), TEXT("ShiftSpeed="), Speed))
		{
			Run->SetTimeScale(Speed);
		}
		if (FParse::Value(FCommandLine::Get(), TEXT("ShiftWarp="), Warp))
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

	if (Board)
	{
		Board->Destroy();
		Board = nullptr;
	}
	Board = GetWorld()->SpawnActor<AShiftBoard>(FVector::ZeroVector, FRotator::ZeroRotator);
	Board->Build(S);

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
	bAuditWarned = false;
	RallyFxNode = -1;
	RallyFxAge = 99.0f;
	CoachQueue.Reset();
	CoachQueuedMask = 0;
	CoachCurrent = -1;
	CoachAlpha = 0.0f;
	CoachCooldown = 0.0f;

	UE_LOG(LogShift, Log, TEXT("Run started: jam=%d (%hs) seed=%u attract=%d nodes=%d edges=%d rival=%.1f%% you=%.1f%% bot=%d speed=%.1f warp=%.0fs build=%d/%d/%d"),
		RunJam, S.Jam().name.c_str(), InSeed, bAttract ? 1 : 0, S.NodeCount(), Board->EdgeCount(), S.Share(S.RivalId()) * 100.0,
		S.Share(ShiftSim::kPlayerId) * 100.0, Run->HasBot() ? 1 : 0, Speed, Warp, Build.Quality, Build.Thumbnail, Build.Network);
}

void AShiftPlayerController::StartAttract()
{
	static uint32 AttractCounter = 0;
	const int32 AttractJam = Screen == EShiftScreen::Build ? JamIndex : 2;
	StartRun(AttractJam, 1000u + (AttractCounter++) * 37u, true);
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
	StartRun(JamIndex, Seed + 1u, false);
}

void AShiftPlayerController::EnterScreen(EShiftScreen NewScreen)
{
	const EShiftScreen Previous = Screen;
	Screen = NewScreen;
	bPaused = false;
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

void AShiftPlayerController::ApplyMute() const
{
	if (GEngine)
	{
		if (FAudioDevice* Device = GEngine->GetMainAudioDeviceRaw())
		{
			Device->SetTransientPrimaryVolume(bMuted ? 0.0f : QaMasterVolume);
		}
	}
}

void AShiftPlayerController::ToggleMute()
{
	bMuted = !bMuted;
	ShiftSave::SetMuted(bMuted);
	ApplyMute();
}

bool AShiftPlayerController::IsNightHeldNow() const
{
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

void AShiftPlayerController::PushFeed(const FString& Text, const FLinearColor& Color, float Seconds, bool bNews)
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
		if (!Item.bNews && Item.Remaining > MinRemaining)
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

void AShiftPlayerController::ShowHeadline(const FString& Event, const FString& Entry)
{
	FString Line = FShiftContent::PickHeadline(Event, CosmeticRng);
	if (Line.IsEmpty())
	{
		return;
	}
	Line.ReplaceInline(TEXT("{entry}"), *Entry);
	Line.ReplaceInline(TEXT("{you}"), *EntryName(ShiftSim::kPlayerId));
	Line.ReplaceInline(TEXT("{rival}"), *EntryName(Run.IsValid() ? Run->GetSim().RivalId() : 1));
	if (Line.Contains(TEXT("{handle}")))
	{
		const FString Handle = NodeHandles.Num() > 0 ? NodeHandles[CosmeticRng.RandRange(0, NodeHandles.Num() - 1)] : FString(TEXT("someone"));
		Line.ReplaceInline(TEXT("{handle}"), *(TEXT("@") + Handle));
	}
	PushFeed(Line, AmberColor(), 6.5f, true);
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
	const bool bSilent = Id == EShiftUi::SliderQuality || Id == EShiftUi::SliderThumbnail || Id == EShiftUi::SliderNetwork
		|| Id == EShiftUi::Upload || Id == EShiftUi::Blame || Id == EShiftUi::TitleStart || Id == EShiftUi::FakeInfo;
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
		EnterScreen(EShiftScreen::JamSelect);
		break;
	case EShiftUi::Resume:
		SetPaused(false);
		break;
	case EShiftUi::Blame:
		if (Run.IsValid())
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
	case EShiftUi::Share:
		CopyResultToClipboard();
		break;
	default:
		break;
	}
}

void AShiftPlayerController::HandleHudClick(const FVector2D& LocalPos, bool bShift, bool bRightButton)
{
	if (Screen != EShiftScreen::Play || !Run.IsValid() || !Board || Run->IsOver() || bPaused)
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
	bPlayerActed = true;
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

	switch (Screen)
	{
	case EShiftScreen::Title:
		if (Key == EKeys::Enter || Key == EKeys::SpaceBar)
		{
			HandleUi(EShiftUi::TitleStart, 0.0f);
		}
		else if (Key == EKeys::Escape)
		{
			FPlatformMisc::RequestExit(false);   // Esc on the title quits the game
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
			ShowHeadline(Kinds[FMath::Clamp(E.Node, 0, 2)], EntryName(E.NewOwner));
			break;
		}
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
	M.EdgeFlash = EdgeFlash;
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
	const bool bActive = bCoachEnabled && Screen == EShiftScreen::Play && !bAttract && Run.IsValid() && !Run->IsOver();
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
	T.Anchor = FVector2f(Node.x, Node.y);
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
		int32 ThreatWho = -1;
		double ThreatLeft = 0.0;
		double ThreatTotal = 0.0;
		if (S.ThreatOn(HoverNode, &ThreatWho, &ThreatLeft, &ThreatTotal))
		{
			T.Status = FString::Printf(TEXT("%s plugs this voter in %.1fs: click to parry, or Rally"), *EntryName(ThreatWho), ThreatLeft);
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
				? FString::Printf(TEXT("Sockpuppet (%.0f%% sure): reinforce to %.0f%% to make it real"), T.Conviction * 100.0f, P.SockLaunderConv * 100.0)
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
			T.Status = Challenger >= 0 ? FString::Printf(TEXT("Flips back in ~%.0fs: %d neighbours back %s. Reinforce, or win them"), FMath::Max(1.0, S.SecondsToFlip(HoverNode)), Pushers, *EntryName(Challenger))
				: FString::Printf(TEXT("Under pressure: flips back in about %.0fs. Click to reinforce"), FMath::Max(1.0, S.SecondsToFlip(HoverNode)));
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
				T.Status = TEXT("Yours: click to reinforce, right-click to Rally");
			}
			T.StatusColor = TealColor();
		}
	}
	else if (S.LockRemaining(HoverNode) > 0.0)
	{
		T.Status = FString::Printf(TEXT("Immune for %.1fs"), S.LockRemaining(HoverNode));
		T.StatusColor = AmberColor();
	}
	else if (bNight && P.SockOverheatRefuse > 0.5 && S.Heat() + P.SockHeat >= P.HeatMax - 1.0e-9)
	{
		T.Status = TEXT("Overheated: let Heat cool before the next sockpuppet");
		T.StatusColor = RedColor();
	}
	else if (bNight)
	{
		const double HeatAfter = FMath::Min(S.Heat() + P.SockHeat, P.HeatMax);
		const bool bRed = HeatAfter > P.HeatRedZone;
		T.Status = FString::Printf(TEXT("%s. Heat %.0f -> %.0f%s"), S.IsGated(HoverNode) ? TEXT("Sockpuppet: ignores the gate") : TEXT("Sockpuppet: fast but fragile"),
			S.Heat(), HeatAfter, bRed ? TEXT(" (red zone)") : TEXT(""));
		T.StatusColor = bRed ? RedColor() : SockColor;
	}
	else if (S.IsGated(HoverNode))
	{
		const ShiftSim::GateInfo Gate = S.Gate(HoverNode);
		T.Status = FString::Printf(TEXT("Gated: %.0f%% of followers back you, need %.0f%%. Hold SHIFT to skip it"), Gate.progress * 100.0, Gate.needed * 100.0);
		T.StatusColor = RedColor();
	}
	else
	{
		const int32 Need = S.PlugsNeeded(ShiftSim::kPlayerId, HoverNode, 1.0, true);
		T.Status = Need >= ShiftSim::kUnreachable ? FString(TEXT("Cannot be flipped right now")) : FString::Printf(TEXT("Click: Plug (-1 Post), %d to flip"), Need);
		T.StatusColor = TealColor();
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
	const double SockVotes = S.SockpuppetWeight();
	const int32 QualityRank = [&S]()
		{
			int32 Better = 0;
			for (int32 E = 1; E < S.NumEntries(); ++E)
			{
				if (S.GetEntry(E).quality > S.GetEntry(ShiftSim::kPlayerId).quality)
				{
					++Better;
				}
			}
			return Better + 1;
		}();
	const int32 Leader = S.Leader();
	const bool bWon = !bDq && Leader == ShiftSim::kPlayerId;
	const double Runner = [&S, Leader]() { double Best = 0.0; for (int32 E = 0; E < S.NumEntries(); ++E) { if (E != Leader) { Best = FMath::Max(Best, S.Tally(E)); } } return Best; }();
	const double Margin = bWon ? (Votes - Runner) / Total : (S.Tally(Leader) - Votes) / Total;

	FRandomStream Rng(static_cast<int32>(Seed * 31u + 7u));
	FString Outcome, Condition;
	if (bDq)
	{
		Outcome = TEXT("DQ");
		Condition = (S.Time() / FMath::Max(1.0, S.TotalSeconds())) < 0.5 ? TEXT("DQEarly") : TEXT("DQLate");
		Out.OutcomeTitle = TEXT("DISQUALIFIED");
		Out.OutcomeColor = RedColor();
	}
	else if (bWon)
	{
		Outcome = TEXT("Win");
		Condition = SockVotes > 0.25 * Votes ? TEXT("SockpuppetWin") : (SockVotes < 1.0 ? TEXT("CleanWin") : (Margin < 0.03 ? TEXT("NarrowWin") : (Votes / Total > 0.5 ? TEXT("Landslide") : TEXT("CleanWin"))));
		Out.OutcomeTitle = TEXT("YOU WON THE JAM");
		Out.OutcomeColor = TealColor();
	}
	else
	{
		Outcome = TEXT("Lose");
		Condition = Margin < 0.04 ? TEXT("NearMiss") : (QualityRank <= 3 ? TEXT("QualityIgnored") : TEXT("NobodyPlayed"));
		Out.OutcomeTitle = FString::Printf(TEXT("%s PLACE"), *Ordinal(Rank).ToUpper());
		Out.OutcomeColor = AmberColor();
	}

	FString Blurb = FShiftContent::PickBlurb(Outcome, Condition, Rng);
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
	Out.OutcomeLine3 = FShiftContent::PickBlurb(TEXT("Wink"), TEXT("Closing"), Rng);
	const FShiftFlipStats& Flips = Run->GetFlipStats();
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
	M.bMuted = bMuted;
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
	M.bMuted = bMuted;
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
	BuildHoverTooltip(M.Tooltip);

	if (M.bOver)
	{
		if (!bResultsBuilt)
		{
			BuildResults(ResultsModel);
			bResultsBuilt = true;
			if (!S.Disqualified())
			{
				PlaySfx(S.Leader() == ShiftSim::kPlayerId ? EShiftSfx::Win : EShiftSfx::Lose);   // the disqualification sounded when it happened
			}
		}
		M.OutcomeTitle = ResultsModel.OutcomeTitle;
		M.OutcomeLine1 = ResultsModel.OutcomeLine1;
		M.OutcomeLine2 = ResultsModel.OutcomeLine2;
		M.OutcomeLine3 = ResultsModel.OutcomeLine3;
		M.PlaysLine = ResultsModel.PlaysLine;
		M.OutcomeColor = ResultsModel.OutcomeColor;
		M.HonestShare = ResultsModel.HonestShare;
		M.SockShare = ResultsModel.SockShare;
		M.bCanNextJam = ResultsModel.bCanNextJam;
		M.QualityDots = ResultsModel.QualityDots;
		M.FameDots = ResultsModel.FameDots;
		M.QualityFit = ResultsModel.QualityFit;
		M.FameFit = ResultsModel.FameFit;
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// tick

void AShiftPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (AudioMeter && !AudioMeter->Tick(DeltaTime))
	{
		const int32 Failures = AudioMeter->GetFailures();
		AudioMeter = nullptr;
		UE_LOG(LogShift, Log, TEXT("AudioMeter finished: %s"), Failures == 0 ? TEXT("all checks passed") : TEXT("FAILURES, see the [FAIL] lines above"));
		FPlatformMisc::RequestExitWithStatus(false, Failures == 0 ? 0 : 3);
		return;
	}
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
		HandleHudClick(Click.Design, Click.bNight, false);
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
	Run->SetPaused(bPlaying && bPaused);
	Run->Tick(DeltaTime);

	const float NightTarget = NightOverride >= 0.0f ? NightOverride : (bNight ? 1.0f : 0.0f);
	NightBlend = FMath::FInterpTo(NightBlend, NightTarget, DeltaTime, 7.0f);

	if (bFakeCursor)
	{
		CursorWorld = Board->SimToWorld(FakeCursorDesign.X, FakeCursorDesign.Y);
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
	HoverNode = (bPlaying && !bPaused && !Run->IsOver()) ? Board->NodeAtWorld(CursorWorld) : -1;

	EventScratch.Reset();
	Run->DrainEvents(EventScratch);
	if (bPlaying && !bAttract)
	{
		HandleEvents(EventScratch);
		TickLostVoters(DeltaTime);
		TickShareTrend();

		const ShiftSim::Sim& Sim = Run->GetSim();
		const int32 Leader = Sim.Leader();
		if (LastLeader >= 0 && Leader != LastLeader && !Run->IsOver())
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
	RallyFxAge = FMath::Min(RallyFxAge + DeltaTime, 99.0f);
	View.RallyNode = RallyFxAge < 1.2f ? RallyFxNode : -1;
	Board->Update(S, View, DeltaTime);

	if (Hud.IsValid())
	{
		FShiftHudModel Model;
		BuildHudModel(Model);
		Hud->SetModel(MoveTemp(Model));
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
