# Audio: MetaSound and Harmonix only

Every sound in the game is a **MetaSound** graph. There is no `.wav`, no `SoundWave` asset, no `Wave Player` / sampler node and no AI-generated audio: oscillators, noise,
envelopes and filters, driven by triggers. The music is clocked by a **Harmonix** Metronome. `Tools/verify.ps1` fails if a `.wav` or a SoundWave (or a dependency on one) appears.

## Assets (`Content/Shift/Audio`)

| Asset | What it is |
|---|---|
| `MS_Music` | MetaSound **Source**, looping. Parameters (floats): `DayGain`, `NightGain`, `TensionGain`, `Volume`. Graph output `MIDI Clock` (Harmonix) |
| `MS_Sfx_<Name>` x 26 | MetaSound **Sources**, one-shot (`OnFinished` from a `Trigger Delay`). One per `EShiftSfx`: Click, Hover, Slider, Upload, Transition, Tip, Plug, Flip, Reinforce, Weaken, Gated, Locked, NoPosts, NightOn, NightOff, Sock, Heartbeat, ModWarn, ModSweep, Strike, Shadowban, Blame, Day, Win, Lose, DQ |
| `MSP_*` x 15 | MetaSound **Patches** (reusable voices), instantiated as nodes inside `MS_Music`: Marimba, Bell, Stab, PadDay, PadNightLow, PadNightHigh, BassDay, BassNight, Tom, KickDay, KickNight, Snare, Hat, HatOpen, Rim |

## How the music works (`Tools/py/ms_music.py`)

1. **Clock.** `TriggerToTransport` (Play from `OnPlay`) -> `Metronome` (112.5 BPM, 4/4, 8-bar simple loop) -> `MidiClockSubdivisionTrigger` (16th notes) -> `Trigger Counter` (auto-reset
   at 128). The Metronome's `MIDI Clock` is also a graph output: `UShiftAudio` binds a `UMusicClockComponent` to it (`CreateMetasoundDrivenMusicClock`) for the beat pulse on the board.
2. **Lanes.** Each instrument line is a Float array literal of 128 steps (0 = rest, otherwise a MIDI note, a frequency, or a velocity). `Array::Get` reads it with `Index = Count mod 128`,
   `TriggerCompare` turns every non-zero entry into a trigger. Lines with overlapping notes (pads, marimba, bells) use two voices dealt round-robin by `Trigger Sequence (2)`, each reading
   its own note when its trigger fires.
3. **Layers.** Day (C major: pad, offbeat stabs, bass, marimba melody, kit), night (A minor: dark pad, pulsing bass, bell, heartbeat kick, toms) and tension (16th hats, rims, open hat,
   snare roll in the last bar, a loop-long noise riser). They always run; one `Audio Mixer (Stereo, 3)` weights them by `DayGain`, `NightGain`, `TensionGain` (`UShiftAudio::Tick` sets them
   from the night blend, heat and the last days). Then `Plate Reverb`, `Volume`, a limiter at -1 dB.
4. **No seam.** Nothing is a loop of samples, so there is no loop point: the voices simply keep running into the next pass of the 8 bars (17.067 s).

## Why some numbers look odd

- Float and Time pins (and Float envelopes) update at block rate, about 100 Hz. Anything that moves faster, such as the kick's pitch drop, goes through an oscillator's `Modulation` pin (a
  per-sample Hz offset) driven by an Audio-typed envelope.
- The AD envelope decays as `1 - fraction ^ curve`; `Graph.exp_env` fits the curve so a decay with time constant tau tracks `exp(-t / tau)` over 5 tau.
- The `Noise` node defaults to pink noise; all recipes set white (`Type` = 1).
- Mixers and sequence nodes top out at 8 inputs, so larger sums are chained.
- Levels: each sound has a **trim in dB** in `Tools/py/ms_trims.json` so it peaks at the level in `Tools/py/ms_targets.py` (the levels of the original mix). Do not edit the JSON by hand:
  `Tools\calibrate_audio.ps1` rebuilds, measures with the in-graph meters and rewrites it.

## Rebuild and verify

```powershell
.\Tools\build_audio.ps1            # removes the old MS*.uasset, then builds patches, effects and music (headless editor, Python Builder API)
.\Tools\calibrate_audio.ps1        # optional: re-measure and re-trim (add -Reset to start from zero trims)
.\Tools\verify.ps1                 # audio assets + game boot + audio meters + behaviour, one command
```

Source layout: `Tools/py/ms_graph.py` (builder wrapper, node-key table, `add_meters`), `ms_voices.py` (instrument bodies and the patch list), `ms_sfx.py` (the 26 recipes), `ms_music.py`
(clock, lanes, layers, master), `build_audio.py` (orchestration), `ms_targets.py` / `ms_calibrate.py` (levels), `probe_metasound_nodes.py` (writes `Docs/metasound-nodes-5.8.md`, the register of
every node key a script may use), `verify_audio.py` (asset-level check).

## How it is verified without listening (and without recording)

Every source has graph outputs tapped before its master volume and limiter: `Peak`, `Rms`, `Low`, `Mid`, `High` (the music also `StepCount`, `Hits_<lane>_<n>` per pattern lane and `Lvl_<voice>` per
voice). `UShiftAudioMeter` (`-ShiftAudioMeter=all`) plays each source with the component volume at 0.0005, so nothing is audible while the generator still renders, and reads the outputs with
`UMetaSoundOutputSubsystem::WatchOutput`. It checks that every effect finishes and peaks at its target below -1 dBFS, that the music layers behave (night shifts the spectrum down, tension adds
highs, day and night are about equally loud), that the Harmonix clock steps every 16th note across the loop wrap and delivers beat events, and that every lane fires its pattern and every voice is
audible. It needs a real audio device (`-Sound`). This proves levels, structure and timing, **not taste**: the timbres and the mix balance still need a human ear.

## Known limits

- The first MetaSound generator of a process takes a few hundred milliseconds to come up; a one-shot played at that exact moment may start late.
- `Harmonix` and `MusicEnvironment` are experimental plugins (enabled in the `.uproject`; the Shipping target and the cook were checked). The core `TriggerRepeat` clock
  (`build_music(harmonix=False)`) removes the dependency if they ever give trouble; the beat pulse then falls back to the wall clock.
- Rebuilding needs a headless editor session with audio rendering off (`-NoSound`): Unreal logs `PreSaveAsset ... is doing nothing`, which is harmless here.
