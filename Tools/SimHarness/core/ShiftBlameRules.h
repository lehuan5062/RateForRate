// Shift Happens - Game Off 2026
//
// Shift the Blame: the one answer to the strike that would disqualify the player. Engine independent and header only, so the standalone harness tests the very same code the game
// runs (Tools\sync_sim_core.ps1 copies it). Every number is a Params field (Blame*), so -Set can tune it without a rebuild.
//
// A strike is fatal when it takes the player to MaxStrikes. The strikes before it keep every consequence (the sockpuppet votes revert, the player is shadowbanned): Shift the Blame is
// never offered for them, so ignoring or missing the prompt costs something every time. At the fatal strike the player gets Params::BlameWindow seconds to hand it to the rival,
// but only when the fake votes the mods just took away came to no more than Params::BlameMaxFake of all vote weight: a small slip can be pinned on the rival, a landslide of fake
// votes cannot, and then the run ends at once and says why. Once per run.

#pragma once

#include <cstdint>
#include "ShiftParams.h"
#include "ShiftSim.h"

namespace ShiftSim
{
	enum class BlameVerdict : uint8_t
	{
		NotOffered = 0,     // not a fatal strike, Shift the Blame is off (BlameWindow 0) or already used: the strike stands as it is
		Offered,            // a fatal strike with few enough fake votes: the player has the window to blame the rival
		Refused,            // a fatal strike with too many fake votes to blame on anyone: the run ends now
	};

	// What the strike that was just applied (Sim::ApplyStrike) comes to. FakeShare: the share of all vote weight the sockpuppet votes it reverted were, measured before the revert.
	inline BlameVerdict JudgeBlame(const Sim& S, double FakeShare, bool bAlreadyUsed)
	{
		const Params& P = S.GetParams();
		if (bAlreadyUsed || !(P.BlameWindow > 0.0) || !S.Disqualified())
		{
			return BlameVerdict::NotOffered;
		}
		return FakeShare <= P.BlameMaxFake + 1.0e-12 ? BlameVerdict::Offered : BlameVerdict::Refused;
	}

	// How long the offer stays open: the window, or what is left of the jam if that is less.
	inline double BlameWindowFor(const Sim& S)
	{
		const double Left = S.TotalSeconds() - S.Time();
		const double Window = S.GetParams().BlameWindow;
		if (Left < Window)
		{
			return Left > 0.0 ? Left : 0.0;
		}
		return Window;
	}

	// The blame lands: the strike and its shadowban are taken back (the reverted sockpuppet votes stay reverted) and every voter of the rival's loses a little conviction.
	// Returns the seconds the rival stands still afterwards (the behaviour layer applies it).
	inline double ApplyBlame(Sim& S)
	{
		S.ForgiveStrike();
		S.AddConvictionToEntry(S.RivalId(), -S.GetParams().BlameRivalConv);
		return S.GetParams().BlameRivalFreeze;
	}
}
