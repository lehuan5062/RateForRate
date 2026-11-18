// Shift Happens - Game Off 2026
//
// The rival's Post bank: like the player, the rival pays one Post for every action (a plug on a voter, or a re-plug of a weak hub of its own) from a bank that refills over time, so
// its final push is not unlimited. Engine independent and header only, so the standalone harness tests the very same arithmetic the behaviour layer uses (Tools\sync_sim_core.ps1
// copies it). The refill is lazy: nothing ticks it, the bank catches up from the sim clock whenever it is read or spent, so the StateTree tasks and the native policy share it unchanged.

#pragma once

namespace ShiftSim
{
	struct RivalBank
	{
		double Posts = 0.0;     // what the bank held at At
		double At = 0.0;        // sim time of the last catch-up

		// Fills the bank.
		void Reset(double Max, double Now)
		{
			Posts = Max;
			At = Now;
		}

		// What the bank holds at a sim time (0 when the rule is off, Max <= 0).
		double PostsAt(double Now, double Max, double RegenPerSecond) const
		{
			if (!(Max > 0.0))
			{
				return 0.0;
			}
			const double Elapsed = Now > At ? Now - At : 0.0;
			const double Value = Posts + RegenPerSecond * Elapsed;
			return Value < Max ? Value : Max;
		}

		// Pays Cost Posts if the bank has them. Refused (and free) otherwise; always succeeds when the rule is off.
		bool Spend(double Now, double Max, double RegenPerSecond, double Cost)
		{
			if (!(Max > 0.0))
			{
				return true;
			}
			const double Have = PostsAt(Now, Max, RegenPerSecond);
			Posts = Have;       // caught up, even when the spend is refused, so a later read does not count the same seconds twice
			At = Now > At ? Now : At;
			if (Have + 1.0e-9 < Cost)
			{
				return false;
			}
			Posts = Have - Cost;
			return true;
		}
	};
}
