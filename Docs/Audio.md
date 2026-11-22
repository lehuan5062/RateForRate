# Audio: MetaSound and Harmonix only

Every sound in the game is a **MetaSound** graph. There is no `.wav`, no `SoundWave` asset, no `Wave Player` / sampler node and no AI-generated audio: oscillators, noise,
envelopes and filters, driven by triggers. The music is clocked by a **Harmonix** Metronome. `Tools/verify.ps1` fails if a `.wav` or a SoundWave (or a dependency on one) appears.

## Assets (`Content/Shift/Audio`): one node each

Every asset is a MetaSound Source with **a single node** between its inputs and outputs. That node is a *composite*: plain C++ in the game module that builds the same graph of
**existing engine and Harmonix nodes** (the same oscillators, envelopes, filters, mixers, Plate Reverb, Metronome, ...) at the time the asset is loaded. No DSP or clock is
re-implemented; the composite only wires registry nodes together (`INodeClassRegistry::CreateNode`, `FGraph`, `AddDataEdge`) and runs the engine's own graph operator.

| Asset | Node | Inputs / outputs |
|---|---|---|
| `MS_Music` | **Shift Music** | `Play`, floats `DayGain`, `NightGain`, `TensionGain`, `Volume` (and the per-layer trims `Day|Night|Tension Trim dB`); outputs `Left`, `Right`, `MIDI Clock` (Harmonix, bound by the game's clock component) |
| `MS_Sfx_<Name>` x 26 | **Shift Sfx** | `Play`, `Sound` (which of the 26 recipes), `Trim dB`; outputs `Left`, `Right`, `Finished`. One per `EShiftSfx`: Click, Hover, Slider, Upload, Transition, Tip, Plug, Flip, Reinforce, Weaken, Gated, Locked, NoPosts, NightOn, NightOff, Sock, Heartbeat, ModWarn, ModSweep, Strike, Shadowban, Blame, Day, Win, Lose, DQ |

Source (`Source/ShiftHappens/Audio/Nodes/`): `ShiftComposite.*` (the graph builder and the wrapper operator), `ShiftSynth.*` (helpers: noise, oscillator, envelopes, SVF, ladder, mixers,
reverb, ...), `ShiftVoices.*` (reusable voices as C++ functions: marimba, bell, stab, pad, bass, tom, kick, snare, hat, rim, tone, burst, noise sweep), `ShiftSfxNode.cpp` (the 26
recipes), `ShiftMusicNode.cpp` (clock, lanes, layers, mix), `ShiftTapNodes.cpp` (QA taps). The nodes are registered at module start (`ShiftHappens.cpp`, per-module registration list).
Python (`Tools/py/build_audio.py`) only creates the 27 assets and their one node; it needs no editor module.

## How the music works (`ShiftMusicNode.cpp`)

1. **Clock.** `TriggerToTransport` (Play from the `Play` input) -> `Metronome` (112.5 BPM, 4/4, 8-bar simple loop) -> `MidiClockSubdivisionTrigger` (16th notes) -> counter (auto-reset at 128).
   The Metronome's `MIDI Clock` is the composite's output: `UShiftAudio` binds a `UMusicClockComponent` to it (`CreateMetasoundDrivenMusicClock`) for the beat pulse on the board.
2. **Lanes.** Each instrument line is a Float array of 128 steps (0 = rest, otherwise a MIDI note, a frequency or a velocity) read with the stock `Array Get`, compared and sequenced into
   note triggers. A one-voice lane's value follows the pattern on every step, rests included (a note's velocity input is 0 after a rest; that is how the music has always sounded).
3. **Layers.** Day (C major: pad, offbeat stabs, bass, marimba melody, kit), night (A minor: dark pad, pulsing bass, bell, heartbeat kick, toms) and tension (16th hats, rims, open hat,
   snare roll in the last bar, a loop-long noise riser). They always run; one `Audio Mixer (Stereo, 3)` weights them by `DayGain`, `NightGain`, `TensionGain` (`UShiftAudio::Tick` sets them
   from the night blend, heat and the last days). Then `Plate Reverb` and `Volume`.
4. **No seam.** Nothing is a loop of samples, so there is no loop point: the voices keep running into the next pass of the 8 bars (17.067 s).

## What it costs (`-ShiftAudioCpu=all`, editor binary, headless; share of one core)

The music is built from engine graphs that the `Shift Music` node runs itself, and it only runs what is audible:
* **A layer runs only while it is audible** (its gain is above silence, plus 4 s afterwards so its last notes ring out). Day, night and tension are separate graph parts.
* **A voice that is silent between its notes runs only inside its window.** The 28 gated voices (the pads, the snare, the hats and shaker, the toms, the rims, the roll) have a graph part
  of their own, run from a note-on until their envelope has ended. The always-on voices (stabs, bass, marimba, bells, kicks, the riser) ring almost without a gap and stay in the layer.
  The window of each voice is checked against the real sound: in the meter's long music case every gated voice runs all the time while the window is worked out, and the sound a voice
  makes outside its window must stay below -80 dBFS (`28 of 28 gated voices ring out inside their windows`).
* **No limiters.** The music and the effects peak well below -1 dBFS (the meter run holds every case to it, the stacked layers too), so the limiters never acted; each cost CPU and
  delayed the sound by its 10 ms look-ahead.
* **The QA taps are only built for a meter run** that measures sound, so the game pays nothing for them.

| Case | all layers always (first version) | layers gated | now (no limiters, voices gated) |
|---|---|---|---|
| music, day only | 8.1% | 3.7% | 3.1% |
| music, night only | 8.3% | 4.3% | 3.5% |
| music, tension only | 8.3% | 1.4% | 0.95% |
| music, day + tension | 8.4% | 4.5% | 3.7% |
| music, night + tension | 8.4% | 5.3% | 4.1% |
| a click effect | 1.14% | | 0.51% |

What a skipped layer or voice changes in the sound: a layer that wakes up in the middle of a bar (the night crossfade) brings in its pad chord at the next bar line, and a tension layer that
wakes mid-loop has no riser until the next loop (it is triggered at the loop start). The meter run checks that a layer wakes up and plays (`music day->night`). Levels and band shares
are the same as before to about 0.3 dB (oscillator and noise phases differ).

**Where the rest goes** (`-ShiftMusicBench=<kind>:<count>[:loop],...` with `-ShiftAudioMeter=MS_Music -ShiftAudioCpu` times N copies of one engine node in place of the day layer; the
cost of one instance, as a share of one core): Ladder Filter 0.135% (nonlinear, 4 poles), AD Envelope with audio output 0.063% while running (0.006% idle), State Variable Filter 0.058%,
Triangle oscillator 0.040%, sine / saw oscillator 0.019%, noise 0.013%, Pan / Multiply / mixer input about 0.004%, Plate Reverb 0.3%. A node's call overhead does not matter (the
block rate changes nothing). The day layer is about 9 ladder filters (1.2%), 35 oscillators (0.7%) and 15 running envelopes (0.9%); to go much lower the sound has to change (for
example block-rate envelopes for the slow decays, or a cheaper filter), which has not been done.

## Why some numbers look odd

- Float and Time pins (and Float envelopes) update at block rate, about 100 Hz. Anything that moves faster, such as the kick's pitch drop, goes through an oscillator's `Modulation` pin (a
  per-sample Hz offset) driven by an Audio-typed envelope.
- The AD envelope decays as `1 - fraction ^ curve`; `FSynth::ExpEnv` fits the curve so a decay with time constant tau tracks `exp(-t / tau)` over 5 tau.
- The `Noise` node defaults to pink noise; all recipes set white (`Type` = 1). The noise seed of a graph follows the asset name.
- Mixers and sequence nodes top out at 8 inputs, so larger sums are chained.
- Levels: each sound has a **trim in dB** in `Tools/py/ms_trims.json`, written into the asset as the `Trim dB` literal, so it peaks at the level in `Tools/py/ms_targets.py`.
  Do not edit the JSON by hand: `Tools\calibrate_audio.ps1` rebuilds, measures with the taps and rewrites it.

## Rebuild and verify

```powershell
.\Tools\build_audio.ps1            # removes the old MS*.uasset, then builds the 27 one-node assets (headless editor, Python Builder API)
.\Tools\calibrate_audio.ps1        # optional: re-measure and re-trim (add -Reset to start from zero trims)
.\Tools\verify.ps1                 # audio assets + game boot + audio meters + behaviour, one command
```

Source layout (Python): `Tools/py/ms_graph.py` (builder wrapper, node-key table), `ms_sfx.py` (the sound-id table and the trims), `ms_music.py` (the one-node music asset), `build_audio.py`
(orchestration), `ms_targets.py` / `ms_calibrate.py` (levels), `probe_metasound_nodes.py` (writes `Docs/metasound-nodes-5.8.md`, the register of every node key), `verify_audio.py`
(asset-level check). `Graph.node_count` logs the number of nodes of each saved asset (`MS_Music: 1 nodes`).

## How it is verified without listening (and without recording)

The composites contain invisible **QA taps** (`Shift Meter`, `Hit Tap`, `Level Tap`, `Step Tap`, built only for a meter run): they publish the level of the mix before the master volume (`Peak`, `Rms`,
`Low`, `Mid`, `High`), the hits of every pattern lane, the peak of every voice and the step counter to `FShiftAudioStats`. They do nothing unless the meter run switches them on, and they
publish only for the generator instance of the meter's own component (the game plays its own music at the same time). `UShiftAudioMeter` (`-ShiftAudioMeter=all`) plays each source with the
component volume at nearly zero, so nothing is audible while the generator still renders. It checks that every effect finishes and peaks at its target below -1 dBFS, that the music layers
behave (night shifts the spectrum down, tension adds highs, day and night are about equally loud), that the Harmonix clock steps every 16th note across the loop wrap and delivers beat events,
and that every lane (28) fires its pattern and every voice (40) is audible. It needs a real audio device (`-Sound`). This proves levels, structure and timing, **not taste**: the timbres and
the mix balance still need a human ear.

## Known limits

- The first MetaSound generator of a process takes a few hundred milliseconds to come up; a one-shot played at that exact moment may start late.
- `Harmonix` and `MusicEnvironment` are experimental plugins (enabled in the `.uproject`). The composite music builds its Metronome from the engine's own node, so removing them
  would need the core `TriggerRepeat` clock again.
- Rebuilding needs a headless editor session with audio rendering off (`-NoSound`): Unreal logs `PreSaveAsset ... is doing nothing`, which is harmless here.
