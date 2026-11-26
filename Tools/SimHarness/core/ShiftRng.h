// Rate for Rate - engine-independent numeric core.
// Seeded PRNG (xoshiro128**) and helpers. No <random>, no globals, no exceptions.
// Results are bit-reproducible on every platform (only +,-,*,/ and integer ops are used).
#pragma once

#include <cstdint>

namespace ShiftSim
{

	class Rng
	{
	public:
		explicit Rng(uint32_t seed = 1u) { Seed(seed); }

		// splitmix32-style seeding so that nearby seeds give unrelated streams.
		void Seed(uint32_t seed)
		{
			uint32_t z = seed;
			for (int i = 0; i < 4; ++i)
			{
				z += 0x9E3779B9u;
				uint32_t t = z;
				t ^= t >> 16;
				t *= 0x85EBCA6Bu;
				t ^= t >> 13;
				t *= 0xC2B2AE35u;
				t ^= t >> 16;
				State[i] = t;
			}
			if ((State[0] | State[1] | State[2] | State[3]) == 0u)
			{
				State[0] = 1u;
			}
		}

		// Direct state injection (tests / reference vectors only).
		void SetState(uint32_t A, uint32_t B, uint32_t C, uint32_t D)
		{
			State[0] = A;
			State[1] = B;
			State[2] = C;
			State[3] = D;
		}

		// Derives an independent 32-bit seed from (seed, salt). Used to split streams
		// (graph, entries, layout, dynamics, policies) from one run seed.
		static uint32_t Derive(uint32_t seed, uint32_t salt)
		{
			uint32_t z = seed ^ (salt * 0x9E3779B1u);
			z += 0x7F4A7C15u;
			z ^= z >> 16;
			z *= 0x85EBCA6Bu;
			z ^= z >> 13;
			z *= 0xC2B2AE35u;
			z ^= z >> 16;
			return z;
		}

		// xoshiro128** (Blackman & Vigna).
		uint32_t NextU32()
		{
			const uint32_t Result = Rotl(State[1] * 5u, 7) * 9u;
			const uint32_t T = State[1] << 9;
			State[2] ^= State[0];
			State[3] ^= State[1];
			State[1] ^= State[2];
			State[0] ^= State[3];
			State[2] ^= T;
			State[3] = Rotl(State[3], 11);
			return Result;
		}

		// Uniform in [0, 1). 32 bits of resolution, exactly representable.
		double NextDouble() { return static_cast<double>(NextU32()) * (1.0 / 4294967296.0); }

		double Uniform(double Lo, double Hi) { return Lo + (Hi - Lo) * NextDouble(); }

		// Uniform integer in [0, N). Returns 0 for N <= 1.
		int Range(int N)
		{
			if (N <= 1)
			{
				return 0;
			}
			const uint64_t Wide = static_cast<uint64_t>(NextU32()) * static_cast<uint64_t>(N);
			return static_cast<int>(Wide >> 32);
		}

		// Uniform integer in [Lo, Hi] inclusive.
		int RangeInclusive(int Lo, int Hi)
		{
			if (Hi <= Lo)
			{
				return Lo;
			}
			return Lo + Range(Hi - Lo + 1);
		}

		bool Chance(double P) { return NextDouble() < P; }

		// Picks an index in [0, N) proportionally to Weights[i] (>= 0). Returns -1 if all zero.
		int Choice(const double* Weights, int N)
		{
			double Sum = 0.0;
			for (int i = 0; i < N; ++i)
			{
				Sum += Weights[i];
			}
			if (!(Sum > 0.0))
			{
				return -1;
			}
			const double R = NextDouble() * Sum;
			double Acc = 0.0;
			int Last = -1;
			for (int i = 0; i < N; ++i)
			{
				if (Weights[i] <= 0.0)
				{
					continue;
				}
				Acc += Weights[i];
				Last = i;
				if (R < Acc)
				{
					return i;
				}
			}
			return Last;
		}

		// Fisher-Yates shuffle of any random-access container of size N.
		template <typename Container>
		void Shuffle(Container& Items, int N)
		{
			for (int i = N - 1; i > 0; --i)
			{
				const int j = Range(i + 1);
				if (j != i)
				{
					const auto Tmp = Items[i];
					Items[i] = Items[j];
					Items[j] = Tmp;
				}
			}
		}

	private:
		static uint32_t Rotl(uint32_t X, int K) { return (X << K) | (X >> (32 - K)); }

		uint32_t State[4];
	};

} // namespace ShiftSim
