
// Shift Happens - Game Off 2026

#if !UE_BUILD_SHIPPING

#include "Trailer/ShiftTrailer.h"

#include "Audio/ShiftAudio.h"
#include "Behavior/ShiftPolicies.h"
#include "Board/ShiftBoard.h"
#include "Camera/CameraComponent.h"
#include "Containers/Array.h"
#include "Containers/StringConv.h"
#include "Containers/UnrealString.h"
#include "Engine/World.h"
#include "Game/ShiftPlayerController.h"
#include "Game/ShiftRun.h"
#include "GameFramework/Actor.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/IConsoleManager.h"
#include "HAL/Platform.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Logging/LogMacros.h"
#include "Math/MathFwd.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/App.h"
#include "Misc/Build.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreMiscDefines.h"
#include "Misc/CString.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ShiftHappens.h"
#include <string>
#include "Templates/Casts.h"
#include "Templates/UniquePtr.h"
#include "Templates/UnrealTemplate.h"
#include "UI/ShiftHudModel.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

namespace
{
	/** One bar of the music: four beats at 112.5 BPM. */
	constexpr double BarSeconds = 4.0 * 60.0 / 112.5;

	TArray<FString> Tokens(const FString& Args)
	{
		TArray<FString> Out;
		Args.ParseIntoArray(Out, TEXT(" "), true);
		return Out;
	}

	float Num(const TArray<FString>& T, int32 Index, float Default = 0.0f)
	{
		return T.IsValidIndex(Index) ? FCString::Atof(*T[Index]) : Default;
	}

	/** "<seconds> <text...>" for slam and caption. */
	void SecondsAndText(const FString& Args, float& OutSeconds, FString& OutText)
	{
		int32 Space = INDEX_NONE;
		if (Args.FindChar(TEXT(' '), Space))
		{
			OutSeconds = FCString::Atof(*Args.Left(Space));
			OutText = Args.Mid(Space + 1).TrimStartAndEnd();
		}
		else
		{
			OutSeconds = FCString::Atof(*Args);
			OutText.Reset();
		}
	}
}

FShiftTrailer::FShiftTrailer(AShiftPlayerController& InOwner)
	: Owner(InOwner)
{
	FParse::Value(FCommandLine::Get(), TEXT("ShiftTrailerLatency="), Latency);
	FParse::Value(FCommandLine::Get(), TEXT("ShiftTrailerPreroll="), PrerollSeconds);
}

bool FShiftTrailer::Load(const FString& Path)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		UE_LOG(LogShift, Error, TEXT("Trailer: cannot read the cue file %s"), *Path);
		return false;
	}
	Cues.Reset();
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, true);
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		const FString Line = Lines[i].TrimStartAndEnd();
		if (Line.IsEmpty() || Line.StartsWith(TEXT("#")) || Line.StartsWith(TEXT("Id,")))
		{
			continue;
		}
		// Id,Bar,Cmd,Args: split at the first three commas (the text of a slam or a caption never holds one)
		int32 C1 = INDEX_NONE, C2 = INDEX_NONE, C3 = INDEX_NONE;
		if (!Line.FindChar(TEXT(','), C1) || (C2 = Line.Find(TEXT(","), ESearchCase::CaseSensitive, ESearchDir::FromStart, C1 + 1)) == INDEX_NONE)
		{
			UE_LOG(LogShift, Warning, TEXT("Trailer: line %d has too few columns: %s"), i + 1, *Line);
			continue;
		}
		C3 = Line.Find(TEXT(","), ESearchCase::CaseSensitive, ESearchDir::FromStart, C2 + 1);
		FCue Cue;
		Cue.Id = Line.Left(C1);
		Cue.Bar = FCString::Atof(*Line.Mid(C1 + 1, C2 - C1 - 1));
		Cue.Cmd = Line.Mid(C2 + 1, (C3 == INDEX_NONE ? Line.Len() : C3) - C2 - 1).TrimStartAndEnd();
		Cue.Args = C3 == INDEX_NONE ? FString() : Line.Mid(C3 + 1).TrimStartAndEnd();
		Cues.Add(MoveTemp(Cue));
	}
	Cues.StableSort([](const FCue& A, const FCue& B) { return A.Bar < B.Bar; });
	UE_LOG(LogShift, Log, TEXT("Trailer: %d cues from %s, the last at bar %.2f"), Cues.Num(), *Path, Cues.Num() > 0 ? Cues.Last().Bar : 0.0f);
	return Cues.Num() > 0;
}

void FShiftTrailer::Start()
{
	bActive = true;
	bWaitingForMusic = true;
	WaitSeconds = 0.0;
	Clock = 0.0;
	NextCue = 0;
	Black = BlackFrom = BlackTarget = 1.0f;   // nothing shows before the first card
	BlackSeconds = 0.0f;
	White = 0.0f;
	SpotAlpha = 0.0f;
	SpotUntil = -1.0;
	QuitAt = -1.0;
	Texts.Reset();
	ZoomT0 = -1.0;
	Zoom = 1.0f;

	// the real mouse and the focus stay out of it: the scripted cursor is the only cursor
	Owner.bPseudoCursor = true;
	Owner.bFakeCursor = true;
	Owner.FakeCursorDesign = FVector2D(-1000.0, -1000.0);
	Owner.bAutoPauseOnFocusLoss = false;
	Owner.bPaused = false;
	bPreroll = false;
	if (Owner.Audio && Owner.Audio->HasMusic() && PrerollSeconds > 0.0f)
	{
		// the first seconds of a game are not fit to film (the window changes size, textures and shaders load): the screen stays black and the
		// music is held until they are over, then the music restarts and bar 0 begins
		bPreroll = true;
		PrerollLeft = PrerollSeconds;
		Owner.Audio->HoldMusic();
	}
	else if (Owner.Audio)
	{
		Owner.Audio->RestartMusic();
	}
	UE_LOG(LogShift, Log, TEXT("Trailer: started (latency %.3f s, preroll %.1f s), waiting for the music clock"), Latency, bPreroll ? PrerollSeconds : 0.0f);
}

void FShiftTrailer::Begin()
{
	bWaitingForMusic = false;
	Clock = 0.0;
	WallStart = FPlatformTime::Seconds();
	for (FCue& Cue : Cues)
	{
		Cue.Time = Cue.Bar * BarSeconds + Latency;
	}
	Cues.StableSort([](const FCue& A, const FCue& B) { return A.Time < B.Time; });
	// the clicks of the script are scheduled on the controller's own script clock so that the scripted cursor has time to glide to each one
	for (const FCue& Cue : Cues)
	{
		if (Cue.Cmd.Equals(TEXT("click"), ESearchCase::IgnoreCase))
		{
			const TArray<FString> T = Tokens(Cue.Args);
			AShiftPlayerController::FScriptedClick Click;
			Click.Time = Owner.ScriptClock + static_cast<float>(Cue.Bar * BarSeconds) + Latency;
			Click.Design = FVector2D(Num(T, 0), Num(T, 1));
			Click.bNight = T.IsValidIndex(2) && T[2].Equals(TEXT("n"), ESearchCase::IgnoreCase);
			Owner.ScriptedClicks.Add(Click);
		}
	}
	Owner.ScriptedClicks.Sort([](const AShiftPlayerController::FScriptedClick& A, const AShiftPlayerController::FScriptedClick& B) { return A.Time < B.Time; });
	UE_LOG(LogShift, Log, TEXT("Trailer: bar 0 (music clock %s after %.2f s), %d clicks scheduled"),
		(Owner.Audio && Owner.Audio->MusicClockRunning()) ? TEXT("running") : TEXT("not running"), WaitSeconds, Owner.ScriptedClicks.Num());
}

void FShiftTrailer::Tick(float DeltaTime)
{
	if (!bActive)
	{
		return;
	}
	if (bPreroll)
	{
		PrerollLeft -= DeltaTime;
		if (PrerollLeft > 0.0)
		{
			return;
		}
		bPreroll = false;
		WaitSeconds = 0.0;
		if (Owner.Audio)
		{
			Owner.Audio->RestartMusic();
		}
	}
	if (bWaitingForMusic)
	{
		WaitSeconds += DeltaTime;
		const bool bHasMusic = Owner.Audio && Owner.Audio->HasMusic();
		if (bHasMusic && !Owner.Audio->MusicClockRunning() && WaitSeconds < 3.0)
		{
			return;   // headless runs and a missing music asset do not wait
		}
		Begin();
	}
	Clock += DeltaTime;

	while (NextCue < Cues.Num() && Cues[NextCue].Time <= Clock)
	{
		const FCue& Cue = Cues[NextCue++];
		Execute(Cue);
		if (!bActive)
		{
			return;
		}
	}

	TickZoom();

	// the cover fades linearly to its target
	if (BlackSeconds > 0.0f && BlackAge < BlackSeconds)
	{
		BlackAge = FMath::Min(BlackSeconds, BlackAge + DeltaTime);
		Black = FMath::Lerp(BlackFrom, BlackTarget, BlackAge / BlackSeconds);
	}
	else
	{
		Black = BlackTarget;
	}
	White = FMath::Max(0.0f, White - DeltaTime / FMath::Max(0.02f, WhiteSeconds));
	const float SpotTarget = Clock < SpotUntil ? 1.0f : 0.0f;
	SpotAlpha = FMath::FInterpConstantTo(SpotAlpha, SpotTarget, DeltaTime, 6.0f);
	for (FShiftTrailerText& T : Texts)
	{
		T.Age += DeltaTime;
	}
	Texts.RemoveAll([](const FShiftTrailerText& T) { return T.Age >= T.Life; });

	if (QuitAt >= 0.0 && Clock >= QuitAt)
	{
		UE_LOG(LogShift, Log, TEXT("Trailer: the end (clock %.2f s = %.2f bars, wall %.2f s)"), Clock, Clock / BarSeconds, FPlatformTime::Seconds() - WallStart);
		bActive = false;
		Owner.TrailerQuit();
	}
}

void FShiftTrailer::TickZoom()
{
	float Target = 1.0f;
	if (ZoomT0 >= 0.0)
	{
		const float T = static_cast<float>(Clock - ZoomT0);
		auto Smooth = [](float X)
			{
				X = FMath::Clamp(X, 0.0f, 1.0f);
				return X * X * (3.0f - 2.0f * X);
			};
		float S = 1.0f;
		if (T < ZoomIn)
		{
			S = Smooth(T / FMath::Max(0.01f, ZoomIn));
		}
		else if (T >= ZoomIn + ZoomHold)
		{
			S = 1.0f - Smooth((T - ZoomIn - ZoomHold) / FMath::Max(0.01f, ZoomOut));
		}
		Target = 1.0f + (ZoomScale - 1.0f) * S;
		if (T >= ZoomIn + ZoomHold + ZoomOut)
		{
			ZoomT0 = -1.0;
			Target = 1.0f;
		}
	}
	Zoom = Target;
	ApplyCamera();
}

void FShiftTrailer::ApplyCamera()
{
	if (!Owner.Board || (Zoom <= 1.0001f && !bCameraMoved))
	{
		return;
	}
	AActor* Camera = CameraActor.Get();
	if (!Camera)
	{
		Camera = Owner.GetViewTarget();
		if (!Camera)
		{
			return;
		}
		CameraActor = Camera;
		CameraBase = Camera->GetActorLocation();
		if (const UCameraComponent* Found = Camera->FindComponentByClass<UCameraComponent>())
		{
			OrthoBase = Found->OrthoWidth;
		}
	}
	UCameraComponent* Component = Camera->FindComponentByClass<UCameraComponent>();
	if (!Component)
	{
		return;
	}
	if (Zoom <= 1.0001f)
	{
		Camera->SetActorLocation(CameraBase);
		Component->OrthoWidth = OrthoBase;
		bCameraMoved = false;
		return;
	}
	// the camera looks at the design point Centre, chosen so that the focus point stays where it is on the screen
	const FVector2f Middle(640.0f, 360.0f);
	const FVector2f Centre = ZoomFocus - (ZoomFocus - Middle) / Zoom;
	const FVector2D World = Owner.Board->SimToWorld(Centre.X, Centre.Y);
	Camera->SetActorLocation(FVector(World.X, World.Y, CameraBase.Z));
	Component->OrthoWidth = OrthoBase / Zoom;
	bCameraMoved = true;
}

void FShiftTrailer::Execute(const FCue& Cue)
{
	const FString Cmd = Cue.Cmd.ToLower();
	const TArray<FString> T = Tokens(Cue.Args);
	UE_LOG(LogShift, Log, TEXT("Trailer: bar %.2f (%.2f s, wall %.2f s) %s %s"), Cue.Bar, Clock, FPlatformTime::Seconds() - WallStart, *Cmd, *Cue.Args);

	if (Cmd == TEXT("black"))
	{
		BlackFrom = Black;
		BlackTarget = FMath::Clamp(Num(T, 0), 0.0f, 1.0f);
		BlackSeconds = Num(T, 1);
		BlackAge = 0.0f;
		if (BlackSeconds <= 0.0f)
		{
			Black = BlackTarget;
		}
	}
	else if (Cmd == TEXT("flash"))
	{
		White = 1.0f;
		WhiteSeconds = FMath::Max(0.05f, Num(T, 0, 0.2f));
	}
	else if (Cmd == TEXT("title"))
	{
		Owner.EnterScreen(EShiftScreen::Title);
	}
	else if (Cmd == TEXT("run"))
	{
		const int32 Jam = FMath::RoundToInt(Num(T, 0, 2.0f));
		const uint32 Seed = static_cast<uint32>(FMath::Max(0.0f, Num(T, 1, 7.0f)));
		const FString Bot = (T.IsValidIndex(2) && T[2] != TEXT("-")) ? T[2] : FString();
		Owner.TrailerBeginRun(Jam, Seed, Bot, Num(T, 4, 1.0f), Num(T, 3, 0.0f));
		if (Owner.NightOverride > 0.5f)
		{
			Owner.NightBlend = 1.0f;   // a run that starts while night is held starts dark (StartRun resets the blend to day)
		}
	}
	else if (Cmd == TEXT("warp"))
	{
		Owner.TrailerWarp(Num(T, 0));
	}
	else if (Cmd == TEXT("bot"))
	{
		if (Owner.Run.IsValid() && T.IsValidIndex(0))
		{
			ShiftSim::BotKind Kind = ShiftSim::BotKind::Idle;
			if (!ShiftSim::BotKindFromName(std::string(TCHAR_TO_UTF8(*T[0])), &Kind))
			{
				Kind = ShiftSim::BotKind::Idle;
			}
			Owner.Run->SetBot(Kind);
			Owner.PseudoBotActions = Owner.Run->BotActionCount();
		}
	}
	else if (Cmd == TEXT("speed"))
	{
		if (Owner.Run.IsValid())
		{
			Owner.Run->SetTimeScale(Num(T, 0, 1.0f));
		}
	}
	else if (Cmd == TEXT("night"))
	{
		const bool bOn = Num(T, 0) > 0.5f;
		Owner.NightOverride = bOn ? 1.0f : 0.0f;
		if (T.IsValidIndex(1) && T[1].Equals(TEXT("now"), ESearchCase::IgnoreCase))
		{
			Owner.NightBlend = bOn ? 1.0f : 0.0f;   // no fade: used under a cover
		}
	}
	else if (Cmd == TEXT("tension"))
	{
		if (Owner.Audio)
		{
			Owner.Audio->SetTensionPin(Num(T, 0, -1.0f));
		}
	}
	else if (Cmd == TEXT("click"))
	{
		// scheduled at the start of the script (Begin), so the scripted cursor can glide to it first
	}
	else if (Cmd == TEXT("hover"))
	{
		Owner.PseudoMoveTo(FVector2D(Num(T, 0), Num(T, 1)), Num(T, 2, 0.45f));
	}
	else if (Cmd == TEXT("spot"))
	{
		if (T.IsValidIndex(0) && T[0].Equals(TEXT("off"), ESearchCase::IgnoreCase))
		{
			SpotUntil = Clock;
		}
		else if (T.IsValidIndex(0) && T[0].Equals(TEXT("mods"), ESearchCase::IgnoreCase))
		{
			bSpotMods = true;
			SpotUntil = Clock + Num(T, 1, 2.0f);
		}
		else
		{
			bSpotMods = false;
			SpotRect = FVector4f(Num(T, 0), Num(T, 1), Num(T, 2), Num(T, 3));
			SpotUntil = Clock + Num(T, 4, 2.0f);
		}
	}
	else if (Cmd == TEXT("zoom"))
	{
		ZoomIn = Num(T, 0, 0.6f);
		ZoomHold = Num(T, 1, 1.0f);
		ZoomOut = Num(T, 2, 0.6f);
		ZoomScale = FMath::Max(1.0f, Num(T, 3, 1.5f));
		ZoomFocus = FVector2f(Num(T, 4, 640.0f), Num(T, 5, 360.0f));
		ZoomT0 = Clock;
	}
	else if (Cmd == TEXT("slam") || Cmd == TEXT("caption"))
	{
		FShiftTrailerText Text;
		SecondsAndText(Cue.Args, Text.Life, Text.Text);
		Text.bSlam = Cmd == TEXT("slam");
		// a slam may start with settings: y=<design y of its middle> px=<size> box=<0|1>
		while (Text.bSlam)
		{
			int32 Space = INDEX_NONE;
			const FString Head = Text.Text.FindChar(TEXT(' '), Space) ? Text.Text.Left(Space) : Text.Text;
			if (Head.StartsWith(TEXT("y=")))
			{
				Text.Y = FCString::Atof(*Head.Mid(2));
			}
			else if (Head.StartsWith(TEXT("px=")))
			{
				Text.Px = FCString::Atof(*Head.Mid(3));
			}
			else if (Head.StartsWith(TEXT("box=")))
			{
				Text.bBox = FCString::Atoi(*Head.Mid(4)) != 0;
			}
			else
			{
				break;
			}
			Text.Text = Space == INDEX_NONE ? FString() : Text.Text.Mid(Space + 1).TrimStartAndEnd();
		}
		Texts.Add(MoveTemp(Text));
	}
	else if (Cmd == TEXT("end"))
	{
		BlackFrom = Black;
		BlackTarget = 1.0f;
		BlackSeconds = 0.4f;
		BlackAge = 0.0f;
		QuitAt = Clock + 0.7;
	}
	else
	{
		UE_LOG(LogShift, Warning, TEXT("Trailer: unknown command '%s' (cue %s)"), *Cmd, *Cue.Id);
	}
}

void FShiftTrailer::FillModel(FShiftHudTrailer& Out) const
{
	Out.bActive = bActive;
	Out.Black = Black;
	Out.White = White;
	Out.SpotAlpha = SpotAlpha;
	Out.SpotRect = SpotRect;
	Out.bSpotMods = bSpotMods;
	Out.bHideCursor = Black > 0.5f;
	Out.Zoom = Zoom;
	Out.ZoomFocus = ZoomFocus;
	Out.Texts = Texts;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// the controller's side of the trailer run

void AShiftPlayerController::StartTrailer()
{
	FString Path;
	FParse::Value(FCommandLine::Get(), TEXT("ShiftTrailer="), Path, false);
	if (Path.IsEmpty())
	{
		Path = FPaths::ProjectDir() / TEXT("Data/Trailer.csv");
	}
	// the film is recorded from a window that sits behind the others: the engine would mute an unfocused game (UnfocusedVolumeMultiplier defaults to 0)
	FApp::SetUnfocusedVolumeMultiplier(1.0f);
	Trailer = MakeUnique<FShiftTrailer>(*this);
	if (!Trailer->Load(Path))
	{
		Trailer.Reset();
		return;
	}
	Trailer->Start();
}

void AShiftPlayerController::TrailerBeginRun(int32 Jam, uint32 InSeed, const FString& Bot, float Speed, float Warp)
{
	bRunOverride = true;
	RunOverrideBot = Bot;
	RunOverrideSpeed = 1.0f;   // the warp counts real seconds times the speed: warp at 1x, then the run takes its speed
	RunOverrideWarp = Warp;
	Screen = EShiftScreen::Play;
	StartRun(Jam, InSeed, false);
	bRunOverride = false;
	if (Run.IsValid())
	{
		if (Speed != 1.0f)
		{
			Run->SetTimeScale(Speed);
		}
		PseudoBotActions = Run->BotActionCount();   // the actions of the skipped time are not shown by the cursor
	}
}

void AShiftPlayerController::TrailerWarp(float SimSeconds)
{
	if (!Run.IsValid())
	{
		return;
	}
	for (float T = 0.0f; T < SimSeconds && !Run->IsOver(); T += 0.2f)
	{
		Run->Tick(0.2);
	}
	// the news and toasts of the skipped time are dropped
	EventScratch.Reset();
	Run->DrainEvents(EventScratch);
	EventScratch.Reset();
	Feed.Reset();
	PseudoBotActions = Run->BotActionCount();
}

void AShiftPlayerController::TrailerQuit()
{
#if WITH_EDITOR
	if (GEditor && GetWorld() && GetWorld()->IsPlayInEditor())
	{
		GEditor->RequestEndPlayMap();   // inside Play In Editor RequestExit would take the whole editor down
		return;
	}
#endif
	FPlatformMisc::RequestExit(false);
}

static FAutoConsoleCommandWithWorld GShiftTrailerCommand(
	TEXT("Shift.Trailer"),
	TEXT("Plays the scripted trailer run from Data/Trailer.csv (editor builds only)."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
		{
			if (World)
			{
				if (AShiftPlayerController* Controller = Cast<AShiftPlayerController>(World->GetFirstPlayerController()))
				{
					Controller->StartTrailer();
				}
			}
		}));

#endif   // !UE_BUILD_SHIPPING
