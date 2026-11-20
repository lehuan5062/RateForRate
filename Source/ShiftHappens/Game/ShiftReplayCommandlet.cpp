// Shift Happens - Game Off 2026

#include "Game/ShiftReplayCommandlet.h"

#include "Behavior/StateTree/ShiftBehavior.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Game/ShiftBuild.h"
#include "Game/ShiftRecording.h"
#include "Game/ShiftRun.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/FileManager.h"
#include "HAL/Platform.h"
#include "Logging/LogMacros.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/CString.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ShiftHappens.h"
#include "Sim/ShiftGraph.h"
#include "Sim/ShiftParams.h"
#include "Sim/ShiftSim.h"
#include "Templates/SharedPointer.h"

UShiftReplayCommandlet::UShiftReplayCommandlet()
{
	IsClient = false;
	IsEditor = true;    // the editor modules the StateTree plugin loads in editor builds need GEditor
	IsServer = false;
	LogToConsole = true;
}

namespace
{
	struct FReplayTotals
	{
		int32 Files = 0;
		int32 Replayed = 0;
		int32 Matched = 0;
		int32 Won = 0;
		int32 Lost = 0;
		int32 Disqualified = 0;
		int32 Abandoned = 0;
		double ShareSum = 0.0;
		double ClicksPerMinSum = 0.0;
	};

	const TCHAR* RoleName(ShiftSim::NodeRole Role)
	{
		switch (Role)
		{
		case ShiftSim::NodeRole::Celebrity: return TEXT("celebrity");
		case ShiftSim::NodeRole::LocalHub: return TEXT("hub");
		case ShiftSim::NodeRole::Bridge: return TEXT("bridge");
		default: return TEXT("member");
		}
	}

	/** The result of a run as it stands: how a finished jam ended, "abandoned" for one that was left unfinished. */
	const TCHAR* ResultOf(const FShiftRun& Run)
	{
		const ShiftSim::Sim& S = Run.GetSim();
		if (!Run.IsOver())
		{
			return TEXT("abandoned");
		}
		return S.Disqualified() ? TEXT("disqualified") : (S.Leader() == ShiftSim::kPlayerId ? TEXT("won") : TEXT("lost"));
	}

	FString HashOf(const ShiftSim::Sim& S)
	{
		return FString::Printf(TEXT("%016llx"), static_cast<unsigned long long>(S.StateHash()));
	}

	/** Replays one recording and reports it. Returns false when the file could not be replayed at all. */
	bool ReplayOne(const FString& File, const TMap<FString, double>& Overrides, bool bNoStateTree, FReplayTotals& Totals)
	{
		++Totals.Files;
		FShiftRecording Rec;
		FString Error;
		if (!FShiftRecording::Load(File, Rec, Error))
		{
			UE_LOG(LogShift, Error, TEXT("Replay: %s: %s"), *File, *Error);
			return false;
		}

		FShiftBuild Build;
		Build.Quality = Rec.Quality;
		Build.Thumbnail = Rec.Thumbnail;
		Build.Network = Rec.Network;
		FShiftRun Run;
		Run.SetOverrides(Overrides);
		if (!Run.Start(Rec.Jam, Rec.Seed, Build, true))
		{
			UE_LOG(LogShift, Error, TEXT("Replay: %s: jam %d cannot start"), *File, Rec.Jam);
			return false;
		}
		Run.SetReplay(Rec);
		if (!bNoStateTree)
		{
			TSharedPtr<FShiftBehavior> Behavior = FShiftBehavior::Create(Run);
			if (Behavior.IsValid())
			{
				Run.SetDriver(Behavior);
			}
			else
			{
				UE_LOG(LogShift, Warning, TEXT("Replay: StateTrees unavailable, the native policies play the rival and the other entries (a recording made with the StateTrees may diverge)"));
			}
		}

		const ShiftSim::Sim& S = Run.GetSim();
		const ShiftSim::Graph& G = S.GetGraph();
		const bool bSameTuning = Run.TuningHash() == Rec.Tuning;
		UE_LOG(LogShift, Display, TEXT("Replay: file=%s"), *FPaths::GetCleanFilename(File));
		UE_LOG(LogShift, Display, TEXT("Replay: jam=%d seed=%u build=%d/%d/%d tag=%s tuning=%s clicks=%d samples=%d"), Rec.Jam + 1, Rec.Seed, Rec.Quality, Rec.Thumbnail, Rec.Network, *Rec.Tag,
			bSameTuning ? TEXT("same") : TEXT("DIFFERENT"), Rec.Events.Num(), Rec.Samples.Num());

		// what the recorded clicks were aimed at (the graph is known from the seed, so no replay is needed for this)
		int32 Plugs = 0, Socks = 0, Rallies = 0, Blames = 0, NightOn = 0;
		int32 PlugRole[4] = { 0, 0, 0, 0 };
		int32 SockRole[4] = { 0, 0, 0, 0 };
		for (const FShiftRecEvent& E : Rec.Events)
		{
			const bool bNode = E.Node >= 0 && E.Node < S.NodeCount();
			const int32 Role = bNode ? static_cast<int32>(G.nodes[static_cast<size_t>(E.Node)].role) : 1;
			switch (E.Type)
			{
			case TEXT('P'): ++Plugs; ++PlugRole[Role]; break;
			case TEXT('S'): ++Socks; ++SockRole[Role]; break;
			case TEXT('R'): ++Rallies; break;
			case TEXT('B'): ++Blames; break;
			case TEXT('N'): ++NightOn; break;
			default: break;
			}
		}
		const double RecMinutes = FMath::Max(1.0e-3, Rec.EndTime / 60.0);
		const double ClicksPerMin = static_cast<double>(Plugs + Socks + Rallies) / RecMinutes;
		UE_LOG(LogShift, Display, TEXT("Replay: clicks plug=%d sock=%d rally=%d blame=%d night_shifts=%d per_min=%.1f   plugs on celebrity=%d hub=%d bridge=%d member=%d   socks on celebrity=%d hub=%d bridge=%d member=%d"),
			Plugs, Socks, Rallies, Blames, NightOn, ClicksPerMin, PlugRole[static_cast<int32>(ShiftSim::NodeRole::Celebrity)], PlugRole[static_cast<int32>(ShiftSim::NodeRole::LocalHub)],
			PlugRole[static_cast<int32>(ShiftSim::NodeRole::Bridge)], PlugRole[static_cast<int32>(ShiftSim::NodeRole::Member)], SockRole[static_cast<int32>(ShiftSim::NodeRole::Celebrity)],
			SockRole[static_cast<int32>(ShiftSim::NodeRole::LocalHub)], SockRole[static_cast<int32>(ShiftSim::NodeRole::Bridge)], SockRole[static_cast<int32>(ShiftSim::NodeRole::Member)]);

		// the replay: one sim step per tick, so an abandoned jam stops at the very step it was left in
		const double Dt = S.GetParams().Dt;
		const double TotalSeconds = S.TotalSeconds();
		int32 Outcome[32] = {};      // by EShiftEventType, for the player's own clicks
		int32 FlippedRole[4] = { 0, 0, 0, 0 };
		double FirstCeleb = -1.0;
		double FirstLead = -1.0;
		double LeadSeconds = 0.0;
		double LastTime = 0.0;
		const double Marks[4] = { 0.25, 0.5, 0.75, 1.0 };
		int32 NextMark = 0;
		FString ShareLine;
		TArray<FShiftEvent> Scratch;
		int32 Stalled = 0;
		int64 PrevSteps = S.StepCount();
		while (!Run.IsOver() && static_cast<int32>(S.StepCount()) < Rec.EndStep && Stalled < 4)
		{
			Run.Tick(Dt);
			Stalled = S.StepCount() == PrevSteps ? Stalled + 1 : 0;
			PrevSteps = S.StepCount();
			Scratch.Reset();
			Run.DrainEvents(Scratch);
			for (const FShiftEvent& E : Scratch)
			{
				const int32 Type = static_cast<int32>(E.Type);
				if (Type < 32)
				{
					++Outcome[Type];
				}
				if (E.Type == EShiftEventType::PlayerFlipped && E.Node >= 0 && E.Node < S.NodeCount())
				{
					++FlippedRole[static_cast<int32>(G.nodes[static_cast<size_t>(E.Node)].role)];
				}
			}
			if (FirstCeleb < 0.0)
			{
				for (const int Celeb : G.celebrities)
				{
					if (S.Opinion(Celeb) == ShiftSim::kPlayerId)
					{
						FirstCeleb = S.Time();
					}
				}
			}
			const bool bLead = !S.Disqualified() && S.Leader() == ShiftSim::kPlayerId;
			if (bLead && FirstLead < 0.0)
			{
				FirstLead = S.Time();
			}
			LeadSeconds += bLead ? S.Time() - LastTime : 0.0;
			LastTime = S.Time();
			while (NextMark < 4 && S.Time() + 1.0e-9 >= Marks[NextMark] * TotalSeconds)
			{
				double BestOther = 0.0;
				for (int32 E = 0; E < S.NumEntries(); ++E)
				{
					if (E != ShiftSim::kPlayerId && E != S.RivalId())
					{
						BestOther = FMath::Max(BestOther, S.Share(E));
					}
				}
				ShareLine += FString::Printf(TEXT("%s%d%%: you %.1f / rival %.1f / best other %.1f"), ShareLine.IsEmpty() ? TEXT("") : TEXT("   "), FMath::RoundToInt(Marks[NextMark] * 100.0),
					S.Share(ShiftSim::kPlayerId) * 100.0, S.Share(S.RivalId()) * 100.0, BestOther * 100.0);
				++NextMark;
			}
		}

		const FString Hash = HashOf(S);
		const FString Result = ResultOf(Run);
		const bool bMatch = Hash == Rec.Hash && Result == Rec.Result;
		UE_LOG(LogShift, Display, TEXT("Replay: recorded result=%s rank=%d strikes=%d end=%.1fs hash=%s"), *Rec.Result, Rec.Rank, Rec.Strikes, Rec.EndTime, *Rec.Hash);
		UE_LOG(LogShift, Display, TEXT("Replay: replayed result=%s rank=%d strikes=%d end=%.1fs hash=%s   %s"), *Result, S.RankOf(ShiftSim::kPlayerId) + 1, S.Strikes(), S.Time(), *Hash,
			bMatch ? TEXT("MATCH") : (bSameTuning ? TEXT("DIVERGED (same tuning: a bug, or a recording from another build)") : TEXT("differs (other tuning)")));
		UE_LOG(LogShift, Display, TEXT("Replay: outcomes flipped=%d (celebrity %d, hub %d, bridge %d, member %d) reinforced=%d parried=%d weakened=%d gated=%d locked=%d no_posts=%d refused=%d overheated=%d rallied=%d rally_blocked=%d"),
			Outcome[static_cast<int32>(EShiftEventType::PlayerFlipped)], FlippedRole[static_cast<int32>(ShiftSim::NodeRole::Celebrity)], FlippedRole[static_cast<int32>(ShiftSim::NodeRole::LocalHub)],
			FlippedRole[static_cast<int32>(ShiftSim::NodeRole::Bridge)], FlippedRole[static_cast<int32>(ShiftSim::NodeRole::Member)], Outcome[static_cast<int32>(EShiftEventType::PlayerReinforced)],
			Outcome[static_cast<int32>(EShiftEventType::PlayerParried)], Outcome[static_cast<int32>(EShiftEventType::PlayerWeakened)], Outcome[static_cast<int32>(EShiftEventType::PlayerGated)],
			Outcome[static_cast<int32>(EShiftEventType::PlayerLocked)], Outcome[static_cast<int32>(EShiftEventType::PlayerNoPosts)], Outcome[static_cast<int32>(EShiftEventType::PlayerRefused)],
			Outcome[static_cast<int32>(EShiftEventType::PlayerOverheated)], Outcome[static_cast<int32>(EShiftEventType::PlayerRallied)], Outcome[static_cast<int32>(EShiftEventType::PlayerRallyBlocked)]);
		int32 HeldEnd = 0;
		int32 RivalEnd = 0;
		for (const int Celeb : G.celebrities)
		{
			HeldEnd += S.Opinion(Celeb) == ShiftSim::kPlayerId ? 1 : 0;
			RivalEnd += S.Opinion(Celeb) == S.RivalId() ? 1 : 0;
		}
		UE_LOG(LogShift, Display, TEXT("Replay: celebrities first_taken=%s held_at_end=%d rival_holds_at_end=%d of %d   led_first=%s led_for=%.0f%% of the replay"),
			FirstCeleb >= 0.0 ? *FString::Printf(TEXT("%.0fs"), FirstCeleb) : TEXT("never"), HeldEnd, RivalEnd, G.celebrities.size() > 0 ? static_cast<int32>(G.celebrities.size()) : 0,
			FirstLead >= 0.0 ? *FString::Printf(TEXT("%.0fs"), FirstLead) : TEXT("never"), 100.0 * LeadSeconds / FMath::Max(1.0, S.Time()));
		UE_LOG(LogShift, Display, TEXT("Replay: shares (percent of the vote) %s"), *ShareLine);
		FString Final;
		for (int32 E = 0; E < S.NumEntries(); ++E)
		{
			Final += FString::Printf(TEXT("%s%s %.1f"), Final.IsEmpty() ? TEXT("") : TEXT(" | "), E == ShiftSim::kPlayerId ? TEXT("you") : (E == S.RivalId() ? TEXT("rival") : TEXT("other")), S.Share(E) * 100.0);
		}
		UE_LOG(LogShift, Display, TEXT("Replay: final shares %s"), *Final);

		++Totals.Replayed;
		Totals.Matched += bMatch ? 1 : 0;
		Totals.Won += Result == TEXT("won") ? 1 : 0;
		Totals.Lost += Result == TEXT("lost") ? 1 : 0;
		Totals.Disqualified += Result == TEXT("disqualified") ? 1 : 0;
		Totals.Abandoned += Result == TEXT("abandoned") ? 1 : 0;
		Totals.ShareSum += S.Share(ShiftSim::kPlayerId);
		Totals.ClicksPerMinSum += ClicksPerMin;
		return true;
	}
}

int32 UShiftReplayCommandlet::Main(const FString& Params)
{
	FString RecArg, DirArg, SetArg;
	FParse::Value(*Params, TEXT("Rec="), RecArg, false);
	FParse::Value(*Params, TEXT("Dir="), DirArg, false);
	const bool bNoStateTree = FParse::Param(*Params, TEXT("NoStateTree"));

	TMap<FString, double> Overrides;
	if (FParse::Value(*Params, TEXT("Set="), SetArg, false))
	{
		TArray<FString> SetParts;
		SetArg.ParseIntoArray(SetParts, TEXT(","));
		for (const FString& Part : SetParts)
		{
			FString Key, Value;
			if (Part.Split(TEXT("="), &Key, &Value))
			{
				Overrides.Add(Key.TrimStartAndEnd(), FCString::Atod(*Value));
			}
		}
	}

	TArray<FString> Files;
	if (!RecArg.IsEmpty())
	{
		Files.Add(RecArg);
	}
	else
	{
		if (DirArg.IsEmpty())
		{
			DirArg = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Recordings"));
		}
		TArray<FString> Names;
		IFileManager::Get().FindFiles(Names, *FPaths::Combine(DirArg, TEXT("*.shiftrec*")), true, false);
		Names.Sort();
		for (const FString& Name : Names)
		{
			Files.Add(FPaths::Combine(DirArg, Name));
		}
	}
	if (Files.Num() == 0)
	{
		UE_LOG(LogShift, Error, TEXT("Replay: no recordings found (pass -Rec=<file.shiftrec.txt> or -Dir=<folder>)"));
		return 1;
	}

	FReplayTotals Totals;
	for (const FString& File : Files)
	{
		ReplayOne(File, Overrides, bNoStateTree, Totals);
	}
	UE_LOG(LogShift, Display, TEXT("Replay: summary files=%d replayed=%d matched=%d won=%d lost=%d disqualified=%d abandoned=%d mean_share=%.1f%% mean_clicks_per_min=%.1f"), Totals.Files, Totals.Replayed, Totals.Matched,
		Totals.Won, Totals.Lost, Totals.Disqualified, Totals.Abandoned, Totals.Replayed > 0 ? 100.0 * Totals.ShareSum / Totals.Replayed : 0.0, Totals.Replayed > 0 ? Totals.ClicksPerMinSum / Totals.Replayed : 0.0);
	return Totals.Replayed == Totals.Files ? 0 : 1;
}
