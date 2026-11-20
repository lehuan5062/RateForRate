// Shift Happens - Game Off 2026

#include "Game/ShiftRecording.h"

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "HAL/FileManager.h"
#include "HAL/Platform.h"
#include "Misc/CString.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FString FShiftRecording::ToText() const
{
	FString Out;
	Out += FString::Printf(TEXT("SHIFTREC %d\n"), Format);
	Out += FString::Printf(TEXT("tag %s\n"), *Tag);
	Out += FString::Printf(TEXT("tuning %s\n"), *Tuning);
	Out += FString::Printf(TEXT("jam %d\n"), Jam);
	Out += FString::Printf(TEXT("seed %u\n"), Seed);
	Out += FString::Printf(TEXT("build %d %d %d\n"), Quality, Thumbnail, Network);
	for (const FShiftRecEvent& E : Events)
	{
		Out += FString::Printf(TEXT("E %d %c %d\n"), E.Step, E.Type, E.Node);
	}
	for (const FShiftRecSample& S : Samples)
	{
		Out += FString::Printf(TEXT("S %d %.2f %.1f"), S.Step, S.Posts, S.Heat);
		for (const float Share : S.Shares)
		{
			Out += FString::Printf(TEXT(" %.4f"), Share);
		}
		Out += TEXT("\n");
	}
	Out += FString::Printf(TEXT("result %s\n"), *Result);
	Out += FString::Printf(TEXT("endstep %d\n"), EndStep);
	Out += FString::Printf(TEXT("endtime %.2f\n"), EndTime);
	Out += FString::Printf(TEXT("rank %d\n"), Rank);
	Out += FString::Printf(TEXT("strikes %d\n"), Strikes);
	Out += FString::Printf(TEXT("hash %s\n"), *Hash);
	Out += TEXT("shares");
	for (const float Share : FinalShares)
	{
		Out += FString::Printf(TEXT(" %.4f"), Share);
	}
	Out += TEXT("\n");
	return Out;
}

bool FShiftRecording::FromText(const FString& Text, FShiftRecording& Out, FString& Error)
{
	Out = FShiftRecording();
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);
	if (Lines.Num() == 0 || !Lines[0].StartsWith(TEXT("SHIFTREC")))
	{
		Error = TEXT("not a Shift Happens recording (the first line is not SHIFTREC)");
		return false;
	}
	for (const FString& Line : Lines)
	{
		TArray<FString> T;
		Line.ParseIntoArray(T, TEXT(" "), true);
		if (T.Num() == 0)
		{
			continue;
		}
		const FString& K = T[0];
		if (K == TEXT("SHIFTREC") && T.Num() >= 2) { Out.Format = FCString::Atoi(*T[1]); }
		else if (K == TEXT("tag") && T.Num() >= 2) { Out.Tag = T[1]; }
		else if (K == TEXT("tuning") && T.Num() >= 2) { Out.Tuning = T[1]; }
		else if (K == TEXT("jam") && T.Num() >= 2) { Out.Jam = FCString::Atoi(*T[1]); }
		else if (K == TEXT("seed") && T.Num() >= 2) { Out.Seed = static_cast<uint32>(FCString::Strtoui64(*T[1], nullptr, 10)); }
		else if (K == TEXT("build") && T.Num() >= 4) { Out.Quality = FCString::Atoi(*T[1]); Out.Thumbnail = FCString::Atoi(*T[2]); Out.Network = FCString::Atoi(*T[3]); }
		else if (K == TEXT("E") && T.Num() >= 4)
		{
			FShiftRecEvent E;
			E.Step = FCString::Atoi(*T[1]);
			E.Type = T[2].Len() > 0 ? T[2][0] : TEXT('P');
			E.Node = FCString::Atoi(*T[3]);
			Out.Events.Add(E);
		}
		else if (K == TEXT("S") && T.Num() >= 4)
		{
			FShiftRecSample S;
			S.Step = FCString::Atoi(*T[1]);
			S.Posts = FCString::Atof(*T[2]);
			S.Heat = FCString::Atof(*T[3]);
			for (int32 I = 4; I < T.Num(); ++I)
			{
				S.Shares.Add(FCString::Atof(*T[I]));
			}
			Out.Samples.Add(S);
		}
		else if (K == TEXT("result") && T.Num() >= 2) { Out.Result = T[1]; }
		else if (K == TEXT("endstep") && T.Num() >= 2) { Out.EndStep = FCString::Atoi(*T[1]); }
		else if (K == TEXT("endtime") && T.Num() >= 2) { Out.EndTime = FCString::Atod(*T[1]); }
		else if (K == TEXT("rank") && T.Num() >= 2) { Out.Rank = FCString::Atoi(*T[1]); }
		else if (K == TEXT("strikes") && T.Num() >= 2) { Out.Strikes = FCString::Atoi(*T[1]); }
		else if (K == TEXT("hash") && T.Num() >= 2) { Out.Hash = T[1]; }
		else if (K == TEXT("shares"))
		{
			for (int32 I = 1; I < T.Num(); ++I)
			{
				Out.FinalShares.Add(FCString::Atof(*T[I]));
			}
		}
	}
	if (Out.Format != ShiftRecordingInfo::FormatVersion)
	{
		Error = FString::Printf(TEXT("recording format %d, this build reads %d"), Out.Format, ShiftRecordingInfo::FormatVersion);
		return false;
	}
	return true;
}

bool FShiftRecording::Save(const FString& Path) const
{
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	return FFileHelper::SaveStringToFile(ToText(), *Path, FFileHelper::EEncodingOptions::ForceAnsi);
}

bool FShiftRecording::Load(const FString& Path, FShiftRecording& Out, FString& Error)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		Error = FString::Printf(TEXT("cannot read %s"), *Path);
		return false;
	}
	return FromText(Text, Out, Error);
}
