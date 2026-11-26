// Rate for Rate - Game Off 2026

#include "Audio/Nodes/ShiftVoices.h"

#include "Containers/Array.h"
#include "HAL/Platform.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/CString.h"
#include "ShiftSynth.h"

namespace ShiftAudio::Voices
{
	FArg Fk(FSynth& G, const FArg& Freq, float K)
	{
		if (K == 1.0f)
		{
			return Freq;
		}
		if (Freq.bSig)
		{
			return FArg(G.FScale(Freq.Sig, K));
		}
		return FArg(Freq.Num * K);
	}

	FSig Amp(FSynth& G, const FSig& Sig, const FArg& Level)
	{
		if (!Level.bSig && Level.Num == 1.0f)
		{
			return Sig;
		}
		return G.Mul(Sig, Level);
	}

	FSig Marimba(FSynth& G, const FSig& Trig, const FArg& Freq, const FArg& Vel)
	{
		TArray<FMono> Parts;
		const float Ratios[3] = { 1.0f, 4.0f, 9.9f };
		const float Taus[3] = { 0.50f, 0.12f, 0.04f };
		const float Gains[3] = { 1.0f, 0.35f, 0.10f };
		for (int32 i = 0; i < 3; ++i)
		{
			const FSig Osc = G.Osc(TEXT("sine"), Fk(G, Freq, Ratios[i]), FArg(), Trig);
			Parts.Add(Part(G.Mul(Osc, G.ExpEnv(Trig, Taus[i], 0.002f)), Gains[i]));
		}
		Parts.Add(Part(G.Mul(G.Noise(), G.ExpEnv(Trig, 0.004f, 0.0005f)), 0.12f));
		return Amp(G, G.Mix(Parts), Vel);
	}

	FSig Bell(FSynth& G, const FSig& Trig, const FArg& Freq, const FArg& Vel, float Decay)
	{
		// two-operator FM bell: carrier at F, modulator at 3.5 F (index 2.2 decaying), plus the octave partial
		const FSig Modulator = G.Osc(TEXT("sine"), Fk(G, Freq, 3.5f), FArg(), Trig);
		const FArg Deviation = Fk(G, Freq, 2.2f * 3.5f);
		const FSig Fm = G.Mul(G.Mul(Modulator, G.ExpEnv(Trig, Decay * 0.5f, 0.003f)), Deviation);
		const FSig Carrier = G.Mul(G.Osc(TEXT("sine"), Freq, FArg(Fm), Trig), G.ExpEnv(Trig, Decay, 0.003f));
		const FSig Octave = G.Mul(G.Osc(TEXT("sine"), Fk(G, Freq, 2.0f), FArg(), Trig), G.ExpEnv(Trig, Decay * 0.5f, 0.003f));
		return Amp(G, G.Mix({ Part(Carrier, 1.0f), Part(Octave, 0.25f) }), Vel);
	}

	FSig Stab(FSynth& G, const FSig& Trig, const FArg& Freq, const FArg& Vel, float Cutoff)
	{
		const FSig Source = G.Mix({ Part(G.Osc(TEXT("tri"), Freq, FArg(), Trig), 0.7f), Part(G.Osc(TEXT("sine"), Fk(G, Freq, 2.0f), FArg(), Trig), 0.3f) });
		return Amp(G, G.Mul(G.Ladder(Source, Cutoff, 1.0f), G.ExpEnv(Trig, 0.20f, 0.004f)), Vel);
	}

	FSig Pad(FSynth& G, const FSig& TrigOn, const FSig& TrigOff, const FArg& Freq, float Cutoff, float Attack, float Release, float Level)
	{
		// two detuned saws plus a sub sine through a low-pass, gated by an ADSR (note-on / note-off triggers)
		const FSig Source = G.Mix({ Part(G.Osc(TEXT("saw"), Fk(G, Freq, 1.0035f)), 0.5f), Part(G.Osc(TEXT("saw"), Fk(G, Freq, 0.9965f)), 0.5f),
			Part(G.Osc(TEXT("sine"), Fk(G, Freq, 0.5f)), 0.6f) });
		const FSig Env = G.Adsr(TrigOn, TrigOff, Attack, 0.05f, 1.0f, Release);
		return Amp(G, G.Mul(G.Ladder(Source, Cutoff, 1.0f), Env), Level);
	}

	FSig BassDay(FSynth& G, const FSig& Trig, const FArg& Freq, const FArg& Vel)
	{
		const FSig Source = G.Mix({ Part(G.Osc(TEXT("sine"), Freq, FArg(), Trig), 1.0f), Part(G.Osc(TEXT("sine"), Fk(G, Freq, 2.0f), FArg(), Trig), 0.35f),
			Part(G.Osc(TEXT("sine"), Fk(G, Freq, 3.0f), FArg(), Trig), 0.12f) });
		return Amp(G, Amp(G, G.Mul(Source, G.ExpEnv(Trig, 0.30f, 0.004f, true, 0.6f)), 0.8f), Vel);
	}

	FSig BassNight(FSynth& G, const FSig& Trig, const FArg& Freq, const FArg& Cutoff, const FArg& Vel)
	{
		const FSig Source = G.Mix({ Part(G.Osc(TEXT("saw"), Freq, FArg(), Trig), 0.8f), Part(G.Osc(TEXT("sine"), Fk(G, Freq, 0.5f), FArg(), Trig), 0.7f) });
		return Amp(G, G.Mul(G.Ladder(Source, Cutoff, 1.3f), G.ExpEnv(Trig, 0.10f, 0.006f, true, 0.26f)), Vel);
	}

	FSig Tom(FSynth& G, const FSig& Trig, const FArg& Freq, const FArg& Vel)
	{
		const FSig Fm = G.Mul(G.ExpEnv(Trig, 0.05f, 0.0005f), 30.0f);
		return Amp(G, G.Mul(G.Osc(TEXT("sine"), Freq, FArg(Fm), Trig), G.ExpEnv(Trig, 0.18f, 0.0005f)), Vel);
	}

	FSig Kick(FSynth& G, const FSig& Trig, const FArg& Vel, float Click, float Sub)
	{
		// a sine whose pitch falls from sub + 90 Hz (audio-rate FM from an AD envelope), plus a noise click
		const FSig Fm = G.Mul(G.ExpEnv(Trig, 0.035f, 0.0005f), 90.0f);
		const FSig Body = G.Mul(G.Osc(TEXT("sine"), Sub, FArg(Fm), Trig), G.ExpEnv(Trig, 0.16f, 0.0005f));
		const FSig Tick = G.Mul(G.Mul(G.Noise(), G.ExpEnv(Trig, 0.002f, 0.0003f)), Click);
		return Amp(G, G.Add(Body, Tick), Vel);
	}

	FSig Snare(FSynth& G, const FSig& Trig, const FArg& Vel)
	{
		const FSig NoisePart = G.Mul(G.Svf(G.Noise(), 1400.0f, 0.7f).Hp, G.ExpEnv(Trig, 0.07f, 0.0005f));
		const FSig TonePart = G.Mul(G.Osc(TEXT("sine"), 190.0f, FArg(), Trig), G.ExpEnv(Trig, 0.05f, 0.0005f));
		return Amp(G, G.Mix({ Part(NoisePart, 0.7f), Part(TonePart, 0.5f) }), Vel);
	}

	FSig Hat(FSynth& G, const FSig& Trig, const FArg& Vel, float Decay)
	{
		return Amp(G, G.Mul(G.Svf(G.Noise(), 7000.0f, 0.8f).Hp, G.ExpEnv(Trig, Decay, 0.0005f)), Vel);
	}

	FSig Rim(FSynth& G, const FSig& Trig, const FArg& Vel)
	{
		const FSig Body = G.Mix({ Part(G.Svf(G.Noise(), 1800.0f, 3.0f).Bp, 2.0f), Part(G.Osc(TEXT("sine"), 820.0f, FArg(), Trig), 0.4f) });
		return Amp(G, G.Mul(Body, G.ExpEnv(Trig, 0.012f, 0.0005f)), Vel);
	}

	FSig Tone(FSynth& G, const FSig& Trig, const TCHAR* Kind, float F0, float F1, float Dur, float Attack, float Tau, float VibHz, float VibSemi, float Level)
	{
		// a pitch glide F0 -> F1 over Dur seconds, amplitude attack + exp decay(tau) ending at Dur
		FSig Fm;
		if (FMath::Abs(F1 - F0) > 1.0e-6f)
		{
			Fm = G.Mul(G.Glide(Trig, F0, F1, Dur), F1 - F0);
		}
		if (VibHz > 0.0f)
		{
			const FSig Lfo = G.Mul(G.Osc(TEXT("sine"), VibHz), 0.0578f * VibSemi * F0);   // F * ln2 / 12 * semitones, in Hz
			Fm = Fm.IsValid() ? G.Add(Fm, Lfo) : Lfo;
		}
		const FSig Osc = G.Osc(Kind, F0, Fm.IsValid() ? FArg(Fm) : FArg(), Trig);
		return Amp(G, G.Mul(Osc, G.ExpEnv(Trig, Tau, Attack, true, Dur)), Level);
	}

	FSig Burst(FSynth& G, const FSig& Trig, float Dur, float Tau, const TCHAR* Band, float Freq, float Q, float Attack, float Level)
	{
		const FSynth::FSvf Filter = G.Svf(G.Noise(), Freq, Q);
		const FSig Source = FCString::Strcmp(Band, TEXT("lp")) == 0 ? Filter.Lp : (FCString::Strcmp(Band, TEXT("hp")) == 0 ? Filter.Hp : Filter.Bp);
		return Amp(G, G.Mul(Source, G.ExpEnv(Trig, Tau, Attack, true, Dur)), Level);
	}

	FSig NoiseSweep(FSynth& G, const FSig& Trig, float Dur, float F0, float F1, float Q, float PeakAt, float Level)
	{
		// noise through a band-pass whose centre moves exponentially F0 -> F1 while the amplitude rises to PeakAt (0..1) and falls back
		const FSig U = G.Ad(Trig, Dur, 0.001f, 1.0f, 1.0f, false);
		const FSig Cutoff = G.LogSweep(U, F0, F1);
		const FSig Band = G.Svf(G.Noise(), FArg(Cutoff), Q).Bp;
		const float Rise = FMath::Max(0.01f, PeakAt * Dur);
		const FSig Shape = G.Ad(Trig, Rise, FMath::Max(0.01f, Dur - Rise), 1.0f, 1.0f, true);
		return Amp(G, G.Mul(G.Mul(Band, Shape), 2.0f), Level);
	}
}
