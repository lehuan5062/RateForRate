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
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformMisc.h"
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
	FLinearColor RedColor() { return FLinearColor::FromSRGBColor(FColor(0xFF, 0x4D, 0x5A)); }

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
		TutCount
	};

	const TCHAR* const TutTriggers[TutCount] =
	{
		TEXT("JamStart"), TEXT("FirstHover"), TEXT("FirstPlug"), TEXT("FirstConvictionRing"), TEXT("PostsLow"), TEXT("FirstReinforce"),
		TEXT("FirstGateSeen"), TEXT("FirstGateOpened"), TEXT("ShiftUnlocked"), TEXT("FirstHeat"), TEXT("FirstModEye"), TEXT("FirstStrike"),
		TEXT("BlameAvailable"), TEXT("KeysHint"),
	};

	enum ETutEvent : uint32
	{
		EvPlug = 1u << 0,
		EvReinforce = 1u << 1,
		EvGated = 1u << 2,
		EvStrike = 1u << 3,
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
			if (BotName.Equals(TEXT("random"), ESearchCase::IgnoreCase)) { Kind = ShiftSim::BotKind::Random; }
			else if (BotName.Equals(TEXT("greedy"), ESearchCase::IgnoreCase)) { Kind = ShiftSim::BotKind::Greedy; }
			else if (BotName.Equals(TEXT("sock"), ESearchCase::IgnoreCase)) { Kind = ShiftSim::BotKind::GreedySock; }
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
	bPaused = false;
	bNightLatch = false;
	NightBlend = 0.0f;
	ToastRemaining = 0.0f;
	HeadlineRemaining = 0.0f;
	HoverNode = -1;
	LastLeader = S.Leader();
	LastHeadlineDay = -1;
	RunClock = 0.0f;
	bPlayerActed = false;
	TutEventMask = 0;
	GatedSeenNodes.Reset();
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
		bForecastDirty = true;
		ForecastCooldown = 0.0f;
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

void AShiftPlayerController::RefreshForecast()
{
	Forecast = ShiftBuildRules::Forecast(JamIndex, Seed, Build, 4);
	bForecastDirty = false;
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
		bForecastDirty = true;
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

void AShiftPlayerController::ShowToast(const FString& Text, const FLinearColor& Color, float Seconds)
{
	ToastText = Text;
	ToastColor = Color;
	ToastRemaining = Seconds;
	ToastDuration = FMath::Max(0.1f, Seconds);
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
	HeadlineText = Line;
	HeadlineRemaining = 6.5f;
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
		|| Id == EShiftUi::Upload || Id == EShiftUi::Blame || Id == EShiftUi::TitleStart;
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
		if (Run.IsValid() && Run->IsOver())
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
			TutEventMask |= EvPlug;
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
			ShowToast(FString::Printf(TEXT("STRIKE %d of %d: the mods noticed. Sockpuppets reverted."), static_cast<int32>(E.Value),
				static_cast<int32>(Run->GetSim().GetParams().MaxStrikes)), RedColor(), 3.2f);
			break;
		case EShiftEventType::BlameTaken:
			PlaySfx(EShiftSfx::Blame);
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
	if (S.DayIndex() >= 8)
	{
		QueueCoach(TutShiftUnlocked);
	}
	if (S.Heat() > 4.0)
	{
		QueueCoach(TutFirstHeat);
		if (Run->ModsEnabled())
		{
			QueueCoach(TutFirstModEye);
		}
	}
	if ((TutEventMask & EvStrike) != 0)
	{
		QueueCoach(TutFirstStrike);
		QueueCoach(TutBlameAvailable);
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
			const FString Text = FShiftContent::PickTutorial(TutTriggers[Next]);
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
	if (OwnerId == ShiftSim::kPlayerId)
	{
		T.Status = TEXT("Yours: click to reinforce");
		T.StatusColor = TealColor();
	}
	else if (S.LockRemaining(HoverNode) > 0.0)
	{
		T.Status = FString::Printf(TEXT("Immune for %.1fs"), S.LockRemaining(HoverNode));
		T.StatusColor = AmberColor();
	}
	else if (bNight)
	{
		T.Status = S.IsGated(HoverNode) ? TEXT("Sockpuppet: ignores the gate (+heat)") : TEXT("Sockpuppet: fast but fragile (+heat)");
		T.StatusColor = FLinearColor::FromSRGBColor(FColor(0xB7, 0x9B, 0xFF));
	}
	else if (S.IsGated(HoverNode))
	{
		const ShiftSim::GateInfo Gate = S.Gate(HoverNode);
		T.Status = FString::Printf(TEXT("Gated: %.0f%% of followers back you, need %.0f%%"), Gate.progress * 100.0, Gate.needed * 100.0);
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
	return FString::Printf(TEXT("SHIFT HAPPENS - %s\n%s\n%s place of %d. Honest votes %.1f%%, sockpuppet votes %.1f%%, strikes %d.\n#GameOff2026"),
		UTF8_TO_TCHAR(S.Jam().name.c_str()), *ResultsModel.OutcomeTitle, *Ordinal(Rank), S.NumEntries(), (Votes - SockVotes) / Total * 100.0,
		SockVotes / Total * 100.0, S.Strikes());
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
	Out.PlaysLine = FString::Printf(TEXT("%d votes cast for your game. About %d people played it."), FMath::RoundToInt(static_cast<float>(Votes)), Plays);
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
	B.Hours[0] = Build.Quality;
	B.Hours[1] = Build.Thumbnail;
	B.Hours[2] = Build.Network;
	B.TotalHours = FShiftBuild::TotalHours;
	B.JamTitle = FString(UTF8_TO_TCHAR(ShiftSim::DefaultJam(JamIndex).name.c_str()));
	B.RivalFollowers = RivalFollowers(JamIndex);
	B.ForecastRank = Forecast.PlayerRank;
	B.ForecastShare = Forecast.PlayerShare;
	B.QualityRank = Forecast.QualityRank;
	for (const int32 E : Forecast.QualityOrder)
	{
		FShiftHudForecastRow Row;
		Row.Name = EntryName(E);
		Row.Value = Forecast.Quality.IsValidIndex(E) ? Forecast.Quality[E] : 0.0f;
		Row.Color = ShiftPalette::Entry(E);
		Row.bPlayer = E == ShiftSim::kPlayerId;
		B.ByQuality.Add(Row);
	}
	for (const int32 E : Forecast.ShareOrder)
	{
		FShiftHudForecastRow Row;
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
		Row.Color = ShiftPalette::Entry(E);
		Row.bPlayer = E == ShiftSim::kPlayerId;
		Row.bRival = E == S.RivalId();
		M.Rows.Add(Row);
	}

	M.Posts = static_cast<float>(S.Posts());
	M.PostsMax = static_cast<float>(P.PostsMax);
	M.Heat = static_cast<float>(S.Heat() / P.HeatMax);
	M.HeatRedZone = static_cast<float>(P.HeatRedZone / P.HeatMax);
	M.Strikes = S.Strikes();
	M.MaxStrikes = static_cast<int32>(P.MaxStrikes);
	M.ShadowbanSeconds = static_cast<float>(S.ShadowbanRemaining());
	const bool bNight = IsNightHeldNow();
	M.bShowHeat = bNight || S.Heat() > 0.5 || S.Strikes() > 0;
	M.bModsEnabled = Run->ModsEnabled() && M.bShowHeat;
	M.NextSweepIn = static_cast<float>(Run->NextModSweep() - S.Time());
	M.Night = NightBlend;
	M.bNightActive = bNight;
	M.bPaused = bPaused;
	M.bOver = Run->IsOver();
	M.Toast = ToastText;
	M.ToastAlpha = ToastRemaining > 0.0f ? FMath::Clamp(ToastRemaining / 0.4f, 0.0f, 1.0f) * FMath::Clamp((ToastDuration - ToastRemaining) / 0.12f, 0.0f, 1.0f) : 0.0f;
	M.ToastColor = ToastColor;
	M.Headline = HeadlineText;
	M.HeadlineAlpha = HeadlineRemaining > 0.0f ? FMath::Clamp(HeadlineRemaining / 0.6f, 0.0f, 1.0f) * FMath::Clamp((6.5f - HeadlineRemaining) / 0.3f, 0.0f, 1.0f) : 0.0f;
	M.CoachText = CoachText;
	M.CoachAlpha = CoachAlpha;
	M.bBlameOffer = Run->BlameAvailable();
	M.BlameSeconds = static_cast<float>(Run->BlameSecondsLeft());
	M.BlameWindow = static_cast<float>(Run->BlameWindowSeconds());
	BuildHoverTooltip(M.Tooltip);

	if (M.bOver)
	{
		if (!bResultsBuilt)
		{
			BuildResults(ResultsModel);
			bResultsBuilt = true;
			PlaySfx(S.Disqualified() ? EShiftSfx::DQ : (S.Leader() == ShiftSim::kPlayerId ? EShiftSfx::Win : EShiftSfx::Lose));
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
	ToastRemaining = FMath::Max(0.0f, ToastRemaining - DeltaTime);
	HeadlineRemaining = FMath::Max(0.0f, HeadlineRemaining - DeltaTime);
	SliderSfxCooldown = FMath::Max(0.0f, SliderSfxCooldown - DeltaTime);
	TickTutorial(DeltaTime);
	TickAudio(DeltaTime);

	if (Screen == EShiftScreen::Build && bForecastDirty)
	{
		ForecastCooldown -= DeltaTime;
		if (ForecastCooldown <= 0.0f)
		{
			RefreshForecast();
			ForecastCooldown = 0.1f;
		}
	}

	const ShiftSim::Sim& S = Run->GetSim();
	FShiftBoardView View;
	View.CursorWorld = CursorWorld;
	View.HoverNode = HoverNode;
	View.Time = GetWorld()->GetTimeSeconds();
	View.Night = NightBlend;
	View.Heat = static_cast<float>(S.Heat() / S.GetParams().HeatMax);
	View.Beat = Audio ? Audio->BeatPulse() : 0.0f;
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
