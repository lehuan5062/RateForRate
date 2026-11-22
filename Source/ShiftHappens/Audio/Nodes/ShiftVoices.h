// Shift Happens - Game Off 2026

#pragma once

#include "Audio/Nodes/ShiftSynth.h"
#include "HAL/Platform.h"

/**
 * The instrument voices of the music and the effects (the C++ counterpart of Tools/py/ms_voices.py): each function adds the engine nodes of one voice
 * to the graph it is given and returns its mono audio. They are reused by the music and by the effect recipes, in C++: a graph that uses a voice twice
 * just has its nodes twice, as the patch instances did, but the graph is one node in the asset.
 */
namespace ShiftAudio::Voices
{
	/** Frequency (a number or a signal) times a constant. */
	FArg Fk(FSynth& G, const FArg& Freq, float K);
	/** Scales an audio signal by a number or a float signal; nothing is added for 1.0. */
	FSig Amp(FSynth& G, const FSig& Sig, const FArg& Level);

	FSig Marimba(FSynth& G, const FSig& Trig, const FArg& Freq, const FArg& Vel = 1.0f);
	FSig Bell(FSynth& G, const FSig& Trig, const FArg& Freq, const FArg& Vel = 1.0f, float Decay = 1.1f);
	FSig Stab(FSynth& G, const FSig& Trig, const FArg& Freq, const FArg& Vel = 0.5f, float Cutoff = 3200.0f);
	FSig Pad(FSynth& G, const FSig& TrigOn, const FSig& TrigOff, const FArg& Freq, float Cutoff = 1300.0f, float Attack = 0.35f, float Release = 0.6f, float Level = 0.5f);
	FSig BassDay(FSynth& G, const FSig& Trig, const FArg& Freq, const FArg& Vel = 1.0f);
	FSig BassNight(FSynth& G, const FSig& Trig, const FArg& Freq, const FArg& Cutoff = 300.0f, const FArg& Vel = 1.0f);
	FSig Tom(FSynth& G, const FSig& Trig, const FArg& Freq, const FArg& Vel = 1.0f);
	FSig Kick(FSynth& G, const FSig& Trig, const FArg& Vel = 1.0f, float Click = 0.25f, float Sub = 46.0f);
	FSig Snare(FSynth& G, const FSig& Trig, const FArg& Vel = 1.0f);
	FSig Hat(FSynth& G, const FSig& Trig, const FArg& Vel = 1.0f, float Decay = 0.025f);
	FSig Rim(FSynth& G, const FSig& Trig, const FArg& Vel = 1.0f);

	// generic pieces the effects use
	FSig Tone(FSynth& G, const FSig& Trig, const TCHAR* Kind, float F0, float F1, float Dur, float Attack, float Tau, float VibHz = 0.0f, float VibSemi = 0.0f, float Level = 1.0f);
	/** Filtered noise burst; Band is "lp", "hp" or "bp". */
	FSig Burst(FSynth& G, const FSig& Trig, float Dur, float Tau, const TCHAR* Band, float Freq, float Q, float Attack = 0.001f, float Level = 1.0f);
	FSig NoiseSweep(FSynth& G, const FSig& Trig, float Dur, float F0, float F1, float Q, float PeakAt, float Level = 1.0f);
}
