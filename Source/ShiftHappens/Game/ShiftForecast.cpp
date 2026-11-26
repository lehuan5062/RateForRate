// Rate for Rate - Game Off 2026

#include "Game/ShiftForecast.h"

#include "Async/ParallelFor.h"
#include <atomic>
#include "Behavior/ShiftPolicies.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Game/ShiftBuild.h"
#include "Game/ShiftRun.h"
#include "HAL/Platform.h"
#include "HAL/UnrealMemory.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/AssertionMacros.h"
#include "Sim/ShiftParams.h"
#include "Sim/ShiftRng.h"
#include "Sim/ShiftSim.h"
#include "Templates/SharedPointer.h"
#include "Templates/UnrealTemplate.h"

namespace
{
	/** "A Plug about every 0.7 s": the steady player of the forecast. */
	constexpr double SteadyInterval = 0.7;
	constexpr int32 NumFeatures = 10;
	constexpr int32 Side = FShiftForecastModel::MaxHours + 1;

	/** The quadratic surface over the three levers' hours (each centred on the even split and scaled to -1..1 at 0 / 8 hours). */
	void Features(int32 Quality, int32 Thumbnail, int32 Network, double* Out)
	{
		const double Q = static_cast<double>(Quality) / 4.0 - 1.0;
		const double T = static_cast<double>(Thumbnail) / 4.0 - 1.0;
		const double N = static_cast<double>(Network) / 4.0 - 1.0;
		Out[0] = 1.0;
		Out[1] = Q; Out[2] = T; Out[3] = N;
		Out[4] = Q * Q; Out[5] = T * T; Out[6] = N * N;
		Out[7] = Q * T; Out[8] = Q * N; Out[9] = T * N;
	}

	/** Solves A x = b for every column of b (Gauss-Jordan, partial pivoting); B is NumFeatures rows of Cols values. False when A is singular. */
	bool Solve(double(&A)[NumFeatures][NumFeatures], TArray<double>& B, int32 Cols)
	{
		for (int32 Col = 0; Col < NumFeatures; ++Col)
		{
			int32 Pivot = Col;
			for (int32 Row = Col + 1; Row < NumFeatures; ++Row)
			{
				if (FMath::Abs(A[Row][Col]) > FMath::Abs(A[Pivot][Col]))
				{
					Pivot = Row;
				}
			}
			if (FMath::Abs(A[Pivot][Col]) < 1.0e-12)
			{
				return false;
			}
			if (Pivot != Col)
			{
				for (int32 K = 0; K < NumFeatures; ++K)
				{
					Swap(A[Pivot][K], A[Col][K]);
				}
				for (int32 K = 0; K < Cols; ++K)
				{
					Swap(B[Pivot * Cols + K], B[Col * Cols + K]);
				}
			}
			const double Inv = 1.0 / A[Col][Col];
			for (int32 K = 0; K < NumFeatures; ++K)
			{
				A[Col][K] *= Inv;
			}
			for (int32 K = 0; K < Cols; ++K)
			{
				B[Col * Cols + K] *= Inv;
			}
			for (int32 Row = 0; Row < NumFeatures; ++Row)
			{
				if (Row == Col)
				{
					continue;
				}
				const double F = A[Row][Col];
				if (F == 0.0)
				{
					continue;
				}
				for (int32 K = 0; K < NumFeatures; ++K)
				{
					A[Row][K] -= F * A[Col][K];
				}
				for (int32 K = 0; K < Cols; ++K)
				{
					B[Row * Cols + K] -= F * B[Col * Cols + K];
				}
			}
		}
		return true;
	}
}

ShiftForecastRun::FVariant ShiftForecastRun::MakeVariant(uint32 Seed, int32 SampleIndex, int32 Variation, double Noise)
{
	FVariant Out;
	Out.DynSeed = Seed;
	if (SampleIndex <= 0)
	{
		return Out;
	}
	if ((Variation & (VaryDynamics | VaryTargets)) != 0)
	{
		Out.DynSeed = ShiftSim::Rng::Derive(Seed, 100u + static_cast<uint32>(SampleIndex));
	}
	if ((Variation & VaryStart) != 0)
	{
		const double Golden = 0.6180339887498949 * static_cast<double>(SampleIndex);
		Out.Start = SteadyInterval * (Golden - FMath::FloorToDouble(Golden));
	}
	if ((Variation & VaryTargets) != 0)
	{
		Out.Noise = Noise;
	}
	return Out;
}

bool ShiftForecastRun::PlaySample(int32 JamIndex, uint32 Seed, const FShiftBuild& Build, const FVariant& Variant, TArray<float>& OutShare)
{
	TMap<FString, double> Overrides;
	Overrides.Add(TEXT("BotInterval"), SteadyInterval);
	Overrides.Add(TEXT("BotStart"), Variant.Start);
	Overrides.Add(TEXT("BotNoise"), Variant.Noise);

	FShiftRun Run;
	Run.SetOverrides(Overrides);
	Run.SetDynamicsSeed(Variant.DynSeed);
	if (!Run.Start(JamIndex, Seed, Build, false))
	{
		return false;
	}
	Run.SetBot(ShiftSim::BotKind::Reactor);   // a careful player: answers announced attacks (since round 13 a player that ignores them loses the circles it takes, and the forecast would say the build hardly matters)
	Run.SetTimeScale(60.0);
	while (!Run.IsOver())
	{
		Run.Tick(0.25);
	}
	const ShiftSim::Sim& S = Run.GetSim();
	OutShare.SetNumUninitialized(S.NumEntries());
	for (int32 E = 0; E < S.NumEntries(); ++E)
	{
		OutShare[E] = static_cast<float>(S.Share(E));
	}
	return true;
}

TArray<FShiftBuild> ShiftForecastRun::DesignBuilds()
{
	TArray<FShiftBuild> Out;
	for (int32 Q = 0; Q <= FShiftForecastModel::MaxHours; Q += 2)
	{
		for (int32 T = 0; T <= FShiftForecastModel::MaxHours; T += 2)
		{
			for (int32 N = 0; N <= FShiftForecastModel::MaxHours; N += 2)
			{
				if (Q + T + N <= FShiftBuild::TotalHours)
				{
					FShiftBuild B;
					B.Quality = Q;
					B.Thumbnail = T;
					B.Network = N;
					Out.Add(B);
				}
			}
		}
	}
	return Out;
}

void ShiftForecastRun::FitModel(FShiftForecastModel& Model, const TArray<float>& Shares)
{
	const TArray<FShiftBuild> Design = DesignBuilds();
	const int32 D = Design.Num();
	const int32 K = SamplesPerBuild;
	const int32 E = Model.NumEntries;
	check(Shares.Num() == D * K * E);

	// the average of every grid build's plays, and how far a single play of the player strays from its build's average
	TArray<double> Mean;
	Mean.SetNumZeroed(D * E);
	double SumSq = 0.0;
	for (int32 B = 0; B < D; ++B)
	{
		for (int32 Entry = 0; Entry < E; ++Entry)
		{
			double Sum = 0.0;
			for (int32 S = 0; S < K; ++S)
			{
				Sum += Shares[(B * K + S) * E + Entry];
			}
			Mean[B * E + Entry] = Sum / K;
		}
		for (int32 S = 0; S < K; ++S)
		{
			const double Dev = static_cast<double>(Shares[(B * K + S) * E + ShiftSim::kPlayerId]) - Mean[B * E + ShiftSim::kPlayerId];
			SumSq += Dev * Dev;
		}
	}
	Model.PlayerSpread = static_cast<float>(FMath::Sqrt(SumSq / FMath::Max(1, D * (K - 1))));

	// least squares of the quadratic surface to the grid averages, one column per entry
	double A[NumFeatures][NumFeatures];
	FMemory::Memzero(A);
	TArray<double> R;
	R.SetNumZeroed(NumFeatures * E);
	for (int32 B = 0; B < D; ++B)
	{
		double F[NumFeatures];
		Features(Design[B].Quality, Design[B].Thumbnail, Design[B].Network, F);
		for (int32 I = 0; I < NumFeatures; ++I)
		{
			for (int32 J = 0; J < NumFeatures; ++J)
			{
				A[I][J] += F[I] * F[J];
			}
			for (int32 Entry = 0; Entry < E; ++Entry)
			{
				R[I * E + Entry] += F[I] * Mean[B * E + Entry];
			}
		}
	}
	const bool bSolved = Solve(A, R, E);
	ensure(bSolved);

	constexpr int32 Cells = Side * Side * Side;
	Model.Table.Reset();
	Model.Table.SetNumZeroed(Cells * E);
	for (int32 Q = 0; Q <= FShiftForecastModel::MaxHours; ++Q)
	{
		for (int32 T = 0; T <= FShiftForecastModel::MaxHours; ++T)
		{
			for (int32 N = 0; N <= FShiftForecastModel::MaxHours; ++N)
			{
				if (!FShiftForecastModel::IsLegal(Q, T, N))
				{
					continue;
				}
				double F[NumFeatures];
				Features(Q, T, N, F);
				const int32 Cell = FShiftForecastModel::CellOf(Q, T, N);
				for (int32 Entry = 0; Entry < E; ++Entry)
				{
					double V = 0.0;
					for (int32 I = 0; I < NumFeatures; ++I)
					{
						V += R[I * E + Entry] * F[I];
					}
					Model.Table[Cell * E + Entry] = static_cast<float>(FMath::Clamp(V, 0.0, 1.0));
				}
			}
		}
	}

	// More hours in a lever never make it worse: the player's surface is made to rise with every lever. The least monotone surface above it and the greatest one below it are
	// both monotone, and so is their average, which stays within a hair of the fit wherever the fit already rose.
	TArray<float> Up, Low;
	Up.SetNumZeroed(Cells);
	Low.SetNumZeroed(Cells);
	const int32 P = ShiftSim::kPlayerId;
	for (int32 Q = 0; Q <= FShiftForecastModel::MaxHours; ++Q)
	{
		for (int32 T = 0; T <= FShiftForecastModel::MaxHours; ++T)
		{
			for (int32 N = 0; N <= FShiftForecastModel::MaxHours; ++N)
			{
				if (!FShiftForecastModel::IsLegal(Q, T, N))
				{
					continue;
				}
				float V = Model.Table[FShiftForecastModel::CellOf(Q, T, N) * E + P];
				if (Q > 0) { V = FMath::Max(V, Up[FShiftForecastModel::CellOf(Q - 1, T, N)]); }
				if (T > 0) { V = FMath::Max(V, Up[FShiftForecastModel::CellOf(Q, T - 1, N)]); }
				if (N > 0) { V = FMath::Max(V, Up[FShiftForecastModel::CellOf(Q, T, N - 1)]); }
				Up[FShiftForecastModel::CellOf(Q, T, N)] = V;
			}
		}
	}
	for (int32 Q = FShiftForecastModel::MaxHours; Q >= 0; --Q)
	{
		for (int32 T = FShiftForecastModel::MaxHours; T >= 0; --T)
		{
			for (int32 N = FShiftForecastModel::MaxHours; N >= 0; --N)
			{
				if (!FShiftForecastModel::IsLegal(Q, T, N))
				{
					continue;
				}
				float V = Model.Table[FShiftForecastModel::CellOf(Q, T, N) * E + P];
				if (FShiftForecastModel::IsLegal(Q + 1, T, N)) { V = FMath::Min(V, Low[FShiftForecastModel::CellOf(Q + 1, T, N)]); }
				if (FShiftForecastModel::IsLegal(Q, T + 1, N)) { V = FMath::Min(V, Low[FShiftForecastModel::CellOf(Q, T + 1, N)]); }
				if (FShiftForecastModel::IsLegal(Q, T, N + 1)) { V = FMath::Min(V, Low[FShiftForecastModel::CellOf(Q, T, N + 1)]); }
				Low[FShiftForecastModel::CellOf(Q, T, N)] = V;
			}
		}
	}
	for (int32 Cell = 0; Cell < Cells; ++Cell)
	{
		const int32 N = Cell % Side;
		const int32 T = (Cell / Side) % Side;
		const int32 Q = Cell / (Side * Side);
		if (FShiftForecastModel::IsLegal(Q, T, N))
		{
			Model.Table[Cell * E + P] = 0.5f * (Up[Cell] + Low[Cell]);
		}
	}
}

TArray<float> ShiftForecastRun::WorldQualities(int32 JamIndex, uint32 Seed)
{
	TArray<float> Out;
	ShiftSim::Params Params;
	ShiftSim::JamDef Jam;
	ShiftSim::SetupJam(FMath::Clamp(JamIndex, 0, ShiftSim::kJamCount - 1), Params, Jam);
	ShiftSim::Sim Base;
	if (Base.Init(Jam, Params, Seed))
	{
		for (int32 E = 0; E < Base.NumEntries(); ++E)
		{
			Out.Add(static_cast<float>(Base.GetEntry(E).quality));
		}
	}
	return Out;
}

void ShiftForecastRun::FillQuality(int32 JamIndex, const TArray<float>& OtherQuality, const FShiftBuild& Build, FShiftForecast& Out)
{
	const int32 E = OtherQuality.Num();
	const float PlayerQuality = static_cast<float>(ShiftBuildRules::Summary(JamIndex, Build).quality);
	Out.Quality = OtherQuality;
	Out.QualityOrder.Reset();
	for (int32 Entry = 0; Entry < E; ++Entry)
	{
		if (Entry == ShiftSim::kPlayerId)
		{
			Out.Quality[Entry] = PlayerQuality;
		}
		Out.QualityOrder.Add(Entry);
	}
	// stable: ties go to the lower entry id, so a row never swaps places for no reason
	Out.QualityOrder.Sort([&Out](int32 A, int32 B) { return Out.Quality[A] != Out.Quality[B] ? Out.Quality[A] > Out.Quality[B] : A < B; });
	Out.QualityRank = Out.QualityOrder.IndexOfByKey(ShiftSim::kPlayerId) + 1;
}

TSharedPtr<const FShiftForecastModel> ShiftForecastRun::BuildModel(int32 JamIndex, uint32 Seed, const std::atomic<bool>& Cancel, std::atomic<int32>& Done)
{
	const TArray<FShiftBuild> Design = DesignBuilds();
	const int32 D = Design.Num();
	const int32 K = SamplesPerBuild;

	TSharedRef<FShiftForecastModel> Model = MakeShared<FShiftForecastModel>();
	Model->JamIndex = FMath::Clamp(JamIndex, 0, ShiftSim::kJamCount - 1);
	Model->Seed = Seed;
	{
		ShiftSim::Params Params;
		ShiftSim::JamDef Jam;
		ShiftSim::SetupJam(Model->JamIndex, Params, Jam);
		ShiftSim::Sim Base;
		if (!Base.Init(Jam, Params, Seed))
		{
			return nullptr;
		}
		Model->NumEntries = Base.NumEntries();
	}
	Model->OtherQuality = WorldQualities(JamIndex, Seed);
	const int32 E = Model->NumEntries;

	TArray<float> Shares;
	Shares.SetNumZeroed(D * K * E);
	TArray<uint8> Played;
	Played.SetNumZeroed(D * K);
	ParallelFor(TEXT("ShiftForecast"), D * K, 1, [&](int32 Index)
		{
			if (Cancel.load(std::memory_order_relaxed))
			{
				return;
			}
			TArray<float> Share;
			if (PlaySample(JamIndex, Seed, Design[Index / K], MakeVariant(Seed, Index % K + 1, ForecastVariation, ForecastNoise), Share) && Share.Num() == E)
			{
				FMemory::Memcpy(&Shares[Index * E], Share.GetData(), sizeof(float) * E);
				Played[Index] = 1;
			}
			Done.fetch_add(1, std::memory_order_relaxed);
		}, EParallelForFlags::BackgroundPriority);

	if (Cancel.load(std::memory_order_relaxed))
	{
		return nullptr;
	}
	for (const uint8 Ok : Played)
	{
		if (Ok == 0)
		{
			return nullptr;
		}
	}
	FitModel(*Model, Shares);
	return Model;
}

FShiftForecast FShiftForecastModel::Evaluate(const FShiftBuild& Build) const
{
	int32 Hours[3] = { FMath::Clamp(Build.Quality, 0, MaxHours), FMath::Clamp(Build.Thumbnail, 0, MaxHours), FMath::Clamp(Build.Network, 0, MaxHours) };
	while (Hours[0] + Hours[1] + Hours[2] > FShiftBuild::TotalHours)
	{
		int32 Largest = 0;
		for (int32 I = 1; I < 3; ++I)
		{
			Largest = Hours[I] > Hours[Largest] ? I : Largest;
		}
		--Hours[Largest];
	}
	FShiftBuild Legal;
	Legal.Quality = Hours[0];
	Legal.Thumbnail = Hours[1];
	Legal.Network = Hours[2];

	FShiftForecast Out;
	const int32 E = NumEntries;
	ShiftForecastRun::FillQuality(JamIndex, OtherQuality, Legal, Out);
	Out.Share.SetNumZeroed(E);
	const int32 Cell = CellOf(Hours[0], Hours[1], Hours[2]);
	for (int32 Entry = 0; Entry < E; ++Entry)
	{
		Out.Share[Entry] = Table.IsValidIndex(Cell * E + Entry) ? Table[Cell * E + Entry] : 0.0f;
		Out.ShareOrder.Add(Entry);
	}
	Out.ShareOrder.Sort([&Out](int32 A, int32 B) { return Out.Share[A] != Out.Share[B] ? Out.Share[A] > Out.Share[B] : A < B; });
	Out.PlayerRank = Out.ShareOrder.IndexOfByKey(ShiftSim::kPlayerId) + 1;
	Out.PlayerShare = E > ShiftSim::kPlayerId ? Out.Share[ShiftSim::kPlayerId] : 0.0f;
	Out.RankLo = 1;
	Out.RankHi = 1;
	for (int32 Entry = 0; Entry < E; ++Entry)
	{
		if (Entry == ShiftSim::kPlayerId)
		{
			continue;
		}
		Out.RankLo += Out.Share[Entry] > Out.PlayerShare + ShiftForecastRun::RankTie ? 1 : 0;
		Out.RankHi += Out.Share[Entry] > Out.PlayerShare - ShiftForecastRun::RankTie ? 1 : 0;
	}
	return Out;
}
