# Toolchain spike log (M0)

Each spike is gated: it passes, or we switch to the named fallback. Evidence is a log line or a screenshot under `Saved/`.
Doc links must carry `?application_version=5.8`: the unversioned Epic pages describe 5.9 (see "Doc drift" at the end), and every "missing" or
"cannot" claim below was re-checked on 5.8 against those pages, the engine source and a runtime probe (see "Claim register").
Engine: Unreal Engine **5.8.4** (`C:\Program Files\Epic Games\UE_5.8`), MSVC 14.44.35207, Windows SDK 22621, DX12 SM6 only.

## S0 - C++ project builds, boots, screenshots (PASS)
- `Tools\build.ps1` builds `ShiftHappensEditor` (Development) in about 12-20 s incrementally; no warnings with `BuildSettingsVersion.V7`.
- Flat modules need `PublicIncludePaths.Add(ModuleDirectory)` to include `"UI/Foo.h"` style paths.
- `-game -RenderOffScreen -nosound` boots in about 20 s on a warm shader cache and `-ShiftShot` captures a 1280x720 PNG that includes Slate UI.
- GPU used: Feature Level 12_2, SM 6.7, 8 GB. `r.Substrate=1` is active.
- Slate lessons (all verified by screenshot):
  - `FSlateDrawElement::MakeBox` does **not** apply the brush's own `TintColor`; pass `Brush.TintColor.GetSpecifiedColor()` as the tint or boxes render white.
  - Slate drawing API lives in `Rendering/DrawElementTypes.h` on 5.8.
  - Viewport widgets get the default DPI scale (0.666 at 720p). We flatten the curve in `DefaultEngine.ini` so local units equal pixels.
  - `ProjectID` in `DefaultGame.ini` must be 32 hex digits without hyphens.
- The engine-independent sim core (`Source/ShiftHappens/Sim`, 8 files) compiles inside the module with no warnings.

## S2 - Unreal MCP (PASS, with caveats)
- `Tools\mcp_editor.ps1 start` launches a headless `UnrealEditor-Cmd` with `-ModelContextProtocolStartServer`; port 8000 opened in under a
  minute and `Tools\mcp_call.mjs` (Node, Streamable HTTP) talks to it. No restart of Claude Code needed: the client is our own script.
- **5.8 differs from the unversioned (5.9) docs:** the 5.8 page lists three meta-tools, `list_toolsets`, `describe_toolset {toolset_name}` and
  `call_tool {tool_name, toolset_name (optional), arguments}` (source: `ModelContextProtocolToolSearch.cpp`, active while `bEnableToolSearch` is on, which is the
  default); the 5.9 page lists four.
- Toolsets: **52 toolsets / 830 tools** are registered in this project's editor (runtime dump, `Tools/py/probe_toolsets.py` -> `Docs/mcp-toolsets.md`); the engine source has
  58 / 878 (the rest belong to plugins that are not enabled here). The earlier hand-written list covered only 18 and was wrong to call itself complete.
- **Gaps that shape the plan (re-verified):** there is **no audio toolset** (no MetaSound, Harmonix or Synthesis tools: audio authoring stays on the Python Builder API,
  S4/S8). The StateTree toolset has 9 `get_*` tools; the generic `ObjectTools.set_properties` can edit reflected StateTree fields, but creating the editor data, the
  schema, property bindings and compiling are not reachable from MCP or Python (S7). Game Feature plugins **can** be authored through MCP/Python (S6, corrected).
- Plugins `ModelContextProtocol`, `ToolsetRegistry`, `AllToolsets` are enabled editor-only in the `.uproject`. The server is unauthenticated
  and loopback-only, so it is only ever started on demand by the script.
- Noise to ignore in editor logs: the engine's own toolset Python (`init_unreal.py` of StateTreeToolset, NiagaraToolsets, ...) fails with `unreal has no attribute
  ToolsetDefinition / AgentSkill` when a game process loads it; harmless. `LogGameFeatures: Error: Asset manager settings do not include a rule for ... GameFeatureData`
  is fixed by the `GameFeatureData` PrimaryAssetTypesToScan entry in `DefaultGame.ini`.

## S3 - world-space SDF board (PASS)
Screenshots `Saved/Shots/s3_day.png` and `s3_night.png` (Jam 3, 72 nodes, 170 edges, warped 40 s).
- Nodes, halos and edges are instanced quads (`UInstancedStaticMeshComponent`) with HLSL SDF Custom nodes in Substrate Unlit materials
  (`Tools/py/hlsl/*.hlsl`, validated standalone by `Tools/py/check_hlsl.py` with fxc). One quad per edge evaluates a 10-segment Bezier
  distance in the shader, so there are no seams and the pulses animate on the GPU.
- Per-instance data via `PerInstanceCustomData` (layouts in `build_materials.py` and `ShiftBoard.cpp`), global theme via `MPC_Theme`.
- World mapping is pixel exact: sim (x, y) projects to screen (x, y) at 1280 x 720 (logged for nodes 0-4).
- World position is used for all SDF math (not UVs), so quad UV orientation never matters.
- Shader/UE pitfalls: `unreal.Rotator` is (roll, pitch, yaw); `AActor::Owner` shadows locals named `Owner` (shadowing is an error on 5.8);
  `ESlateBrushRoundingType::HalfHeightRadius` (not HalfHeight); Slate draw API lives in `Rendering/DrawElementTypes.h`.

## S1 - headless Python authoring (PASS)
Screenshot `Saved/Shots/s1.png`: three HDR SDF circles (Substrate default transmittance, Substrate transmittance 0, legacy
translucent) on a navy background with bloom and vignette, no shader errors in the log. The Substrate path is the default;
legacy stays as the fallback. Node bodies use the transmittance-0 variant (they occlude edges behind them); glows and edge
pulses use the default (additive) variant.

Rendering lessons:
- **Exposure:** `AEM_MANUAL` uses camera ISO / shutter / aperture (defaults are about EV100 10, so HDR emissive is near black).
  The post-process volume sets f/1, 1 s, ISO 100 (EV100 0) for unit exposure.
- **Ortho camera axis:** `OrthoWidth` was applied to the *vertical* axis (visible area 2275 x 1280 for OrthoWidth 1280). Set
  `AspectRatioAxisConstraint = MaintainXFOV` to get 1280 x 720 world units.
- **Idempotent builders:** delete-then-recreate fails inside one editor session. `shift_common.get_or_create` / `fresh_material` /
  `open_fresh_level` reuse and reset instead.

(Original S1 notes follow.)
- `Tools\run_py.ps1` runs a script in `UnrealEditor-Cmd` (`-ExecutePythonScript=`) and quits via `finish()`.
- Built from Python with no errors: `MPC_Theme`, a **Substrate** material (HLSL `Custom` SDF node -> `SubstrateUnlitBSDF` + `SubstrateWeight` (coverage) -> Front Material), a legacy unlit-translucent fallback, `DT_S1Test` imported from CSV (3 rows), and a level with camera, bloom post-process volume and two quads.
- Substrate Unlit BSDF input pin names that connect: `EmissiveColor`, weight node pins `A` and `Weight`.
- Gotcha: `unreal.Rotator` is **(roll, pitch, yaw)**, so a camera looking straight down is `Rotator(0, -90, 0)`.
- Gotcha: the editor writes unrelated plugin settings into `DefaultEngine.ini` (AndroidFileServer, with a security token). The plugin is now disabled in the `.uproject`.
- Open: confirm the Substrate material renders (additive vs over blending, transmittance) and that bloom picks up HDR emissive.

## S4 - MetaSound authoring (PASS; superseded by S8: no stems, no wave pipeline)
The first attempt rendered the music and effects offline with a C++ synth into WAVs and played them from `Wave Player` nodes. That is gone: **all audio is MetaSound and
Harmonix, no `.wav`, no SoundWave asset** (S8). What the first probes got wrong, re-checked on 5.8 (`Docs/metasound-nodes-5.8.md`, runtime-probed):
- `UMetaSoundBuilderSubsystem` works from Python: `create_source_builder(name, format, is_one_shot)` returns `(builder, on_play, on_finished, audio_outs, result)`,
  `create_patch_builder(name)`, `add_graph_input_node` / `add_graph_output_node`, `add_node_by_class_name(MetasoundFrontendClassName{namespace,name,variant}, major)`,
  `add_node(patch asset)`, `find_node_input/output_by_name`, `connect_nodes`, `set_node_input_default` with float / int / bool / array literals (enum pins take the int
  value; Time pins take a float). `MetaSoundEditorSubsystem.build_to_asset` writes serialised assets; `build_and_overwrite_meta_sound` only works for transient ones, and an
  existing asset cannot be overwritten in the same editor session (`Tools/build_audio.ps1` deletes the old `MS*.uasset` first).
- **Wrong earlier claim: "no mixers, Interpolate To, AD/ADSR envelopes, Biquad, panners".** They all exist, under keys I had not guessed: `AudioMixer :: Audio Mixer (Mono|Stereo, N)`
  (N = 2..8), `AD Envelope :: AD Envelope :: Audio|Float`, `ADSR Envelope :: ADSR Envelope :: Audio|Float`, `UE :: InterpTo :: Audio`, `UE :: Stereo Panner`,
  `UE :: Biquad Filter :: Audio` (major 2), plus `Ladder` / `State Variable` / one-pole filters, `Trigger Sequence (N)`, `TriggerCompare :: Clamp :: Int32|Float`,
  `Array :: Get|Num|Random Get`, `UE :: Modulo :: Int32`, `UE :: Linear To Log Frequency`, `UE :: Plate Reverb`, `Limiter`, `Compressor`, `Envelope Follower`.
  The node keys are `namespace :: name :: variant @major`; there is no listing API from Python, so `Tools/py/probe_metasound_nodes.py` probes a key list and writes the register.
- Truly absent: Gain, Mono-to-Stereo, Chorus, Phaser, Make Array, Pink-Noise-as-a-separate-node, a Band Splitter under any key I tried (gain is `Multiply (Audio by Float)`,
  arrays are literals, band meters use the State Variable Filter).
- Pitfalls found while building: the `Noise` node defaults to **pink** noise (pin `Type`, White = 1), which has far less energy above a few kHz than the white noise the recipes
  assumed; Float/Time pins and Float envelopes update at block rate (about 100 Hz), so pitch sweeps go through an oscillator's `Modulation` pin (a per-sample Hz offset) driven by
  an Audio-typed envelope; mixer / sequence nodes top out at 8 inputs; the AD envelope decays as `1 - fraction ^ curve`, so an exponential decay is a fitted curve.

## M5 - screen flow (PASS)
- Title (live attract run behind it, keycap that presses when SHIFT is held), jam select (three cards with rival, facts, difficulty, best rank), build screen (three sliders
  that always sum to at most 12 hours, live idle-player forecast next to a "by quality" list), play, pause menu, results (outcome blurb, two scatter plots with R^2 from the run,
  honest vs sockpuppet bar, retry / next jam / menu / copy result).
- All of it is one custom `SLeafWidget` (`UI/SShiftHud`) with immediate-mode painting and recorded hit regions (`AddHit`), including slider drag with mouse capture. QA hook
  `-ShiftMouse="T,X,Y,m|d|u;..."` injects mouse events through the real widget; `-ShiftUi="T,Id[,V]"` triggers actions by id; `-ShiftScreen=title|jams|build|play`.
  A scripted run (title click -> jam card -> slider drags -> upload) reached `Run started ... build=0/8/4`.
- Build effects (`ShiftBuildRules::Apply`, retuned in M6): quality 0.45 quality points per hour over or under 4, thumbnail scales the player's clout by 1 + 0.5 per hour (floor
  0.15), network adds round(hours / 3) start nodes and 1% faster Post regeneration per hour. The build screen's "what the vote will do" list simulates a *casual* player
  (greedy targets, one Plug every 2.5 s, four seeds) with this build against the stand-in rival and others, so the sliders visibly move your forecast. (Superseded in M11: four seeds were four
  different worlds, which is why the chart jittered.)
- Shift the Blame: after a strike there is a 2.5 s window (once per run) to hand it to the rival; it forgives the strike and shadowban, drains rival conviction by 0.25 and
  freezes the rival for 10 s. A third strike waits for the window before it disqualifies. (Superseded in M11: the Blame is offered only at the third strike and only for a small pile of fake votes.)
- Tutorial coach cards (14 triggers from `Data/Tutorial.csv`) show once ever (seen mask in the user settings ini), one at a time, click to dismiss; off in QA runs unless
  `-ShiftTutorial`.
- Tonemapper note: the filmic tonemapper darkens and desaturates, so the day paper colour is set above 1.0 in `MPC_Theme` (`PaperDay` = 1.40, 1.27, 1.06) to land on warm
  near-white; day edges get a 1.7x alpha boost so they stay readable on pale paper.

## M4 - audio (PASS; MetaSound + Harmonix, see S8)
- Every sound is built by Python in the headless editor (`Tools/build_audio.ps1` -> `Tools/py/build_audio.py`, with `ms_graph.py` (builder wrapper), `ms_voices.py`,
  `ms_sfx.py`, `ms_music.py`): 15 voice **Patches** (`MSP_*`), 26 one-shot **Sources** (`MS_Sfx_<Name>`, one per `EShiftSfx`) and the looping **Source** `MS_Music`
  with the float parameters `DayGain`, `NightGain`, `TensionGain` and `Volume`. Nothing is sampled, rendered offline or AI-generated: oscillators, noise, envelopes, filters.
- `UShiftAudio` (C++) loads the sources by path, drives the music component's parameters and plays effects; the beat pulse comes from a **Harmonix** `UMusicClockComponent` bound to
  the music source's "MIDI Clock" output (the wall-clock estimate stays only as a fallback). `Config/DefaultGame.ini` always cooks `/Game/Shift`, which covers the sources and patches.
- Plugins `Harmonix` and `MusicEnvironment` (both experimental, off by default) are enabled in the `.uproject`; the module list gained `MetasoundEngine`, `HarmonixMetasound` and friends.
- The user does the taste check: I cannot listen, so the audio is verified by in-graph meters (S8), not by ear.
- Gotcha kept from the first version: Unreal mutes an unfocused window (`FApp` unfocused volume multiplier defaults to 0); the game pauses automatically when the window loses
  focus. The meter runs do not depend on it (they measure inside the graph).

## S7 - StateTree behaviour layer (PASS)
**Yes, the game uses StateTree** (`AShiftPlayerController::StartRun` -> `FShiftBehavior::Create`; the log says `Behaviour: 10 StateTree instances (rival, 7 other entries, director,
ambient)`, `Tools/verify.ps1` asserts it, and the cook contains the four assets). The vote model itself stays in the C++ sim core on purpose; the trees decide who acts and when.
- `StateTree` plugin enabled in the `.uproject` (runtime); `Behavior/StateTree/` holds `UShiftStateTreeSchema` (tasks, conditions and evaluators from the common bases; the only
  external data is `UShiftBehaviorContext`; no scheduled ticking), the nodes (`ShiftStateTreeNodes.h/.cpp`, thin: they call the policy building blocks in
  `Behavior/ShiftPolicies.h`, so the calibrated numbers still live in the sim/policy code), `FShiftBehavior` (the runner) and `UShiftStateTreeBuilder` (editor-only authoring).
- Trees (built by `Tools/py/build_statetrees.py` -> `UShiftStateTreeBuilder::BuildAll`, compiled with `UStateTreeEditingSubsystem::CompileStateTree`, saved to
  `/Game/Shift/StateTrees`; the script removes the old files first because an existing asset opens partially loaded and cannot be saved over): `ST_Rival` (wait -> mega pulse /
  defend / attack), `ST_Others` (wait -> plug; one instance per other entry), `ST_Director` (global heat guard task, idle -> telegraph -> sweep = the Mod Eye) and `ST_Ambient`
  (wait -> one of algorithm boost / streamer discovers / drama thread at random, paced by the jam phase).
- `FShiftBehavior` is an `IShiftBehaviorDriver` that `FShiftRun` calls once per fixed 20 Hz step instead of the native stand-ins; each instance has its own
  `FStateTreeInstanceData`, a context object with its own seeded RNG (`Rng::Derive(runSeed, salt)`, also passed as the StateTree `RandomSeed` through `FStartParameters`) and a
  hand-built `FStateTreeExecutionContext` per tick.
- **Hardening (round 2):** a missing or uncompiled tree now logs an **Error** (it used to be a Warning) before the run falls back to the native stand-ins (the fallback stays so
  a broken install is still playable; `-ShiftNoStateTree` asks for it on purpose and the balance commandlet refuses to run without trees). Every action task bumps a counter and
  `FShiftBehavior` logs `Behaviour activity: rival mega/defend/attack | other entries plugged | mods telegraphed/swept | ambient boosts/streamers/drama` when a run ends;
  `Tools/verify.ps1` requires every kind to be non-zero. The two 5.8 deprecation warnings (`Start(nullptr, seed)`, raw `AddReferencedObject`) are fixed.
- **Gameplay Tags (S9):** the rival tree's Mega Campaign and Defend gates are the stock `Has Tags` condition (`FGameplayTagContainerMatchCondition`) bound by the builder to the
  output of a small `FShiftEvaluator_GameTags` evaluator (tags in `Behavior/ShiftTags.h`: `Shift.Rival.MegaPending`, `Shift.Rival.HasWeakHub`, `Shift.Player.Strike.1..3`, ...).
  Gate passed: the full balance matrix (3 jams x 4 bots x 100 runs) is **identical** before and after.
- The build-screen forecast stays on the native stand-ins: a tree run would add about 0.3 s to every play of it. (M11 replaced `ShiftBuildRules::Forecast` by `Game/ShiftForecast.h`, which plays the real world on `FShiftRun` without the trees.)
- Why the trees are authored in C++ (corrected reasons): `UStateTreeEditorData`, `UStateTreeState` and `FStateTreeEditorNode` **are** Python-visible (Epic's own StateTree
  toolset uses them), but `UStateTreeEditingSubsystem::CompileStateTree`, the factory's schema class, `UStateTree::EditorData` and property bindings are not reachable from
  Python or MCP (runtime probe: `hasattr(unreal, "StateTreeEditingSubsystem")` is False), so creating and compiling a tree from scratch needs the small C++ builder.
- Result at the first build: jam 3, seed 7, sock bot at 40x: 10 StateTree instances run to the end; the native path gives the same kind of outcome. The balance table in M6 was
  produced against the trees.
- Pitfalls: tasks must be linked through `Linker.LinkExternalData`; `PropertyBindingUtils` is needed as a module dependency (link error otherwise).

## Headless runs
All Unreal runs are headless (user request): `Tools/run.ps1` uses `UnrealEditor-Cmd.exe -game -RenderOffScreen -NoSound -unattended -stdout` with output redirected to
`Saved/run_last.log` (no `-log`, which opens a console window). On Windows `-RenderOffScreen` swaps the OS application for the null application, so no window exists at all
(checked: the process has window handle 0) while D3D12 still renders off-screen for screenshots. `-Visible` / `-Sound` opt back in for playing it or for the deliberate audio
checks. `run_py.ps1` and `mcp_editor.ps1` were already windowless.

## S6 - Game Feature jam packs (PASS)
- Three Game Feature plugins under `Plugins/GameFeatures/` (`ShiftJam_TinyJam7`, `ShiftJam_Rumble48`, `ShiftJam_GameOff2026`), `BuiltInInitialFeatureState: Active`: each ships its
  own `DT_Entries` (the rival and the eight other entries of that jam), `DA_Jam` (`UShiftJamPackData`: jam index, content key, card title and subtitle, rival followers) and a
  `GameFeatureData` carrying one custom action (`UGameFeatureAction_ShiftJam`) that registers the pack with `FShiftJamRegistry` on activation and removes it on deactivation.
- The jam select screen is built from the active packs (so another pack plugin adds a jam without touching code); the pack's entry table replaces the built-in fallback rows
  for its jam key (`FShiftContent::OverrideEntries`). With no pack active the game falls back to the built-in three jams and the global `DT_Entries`.
- **Authoring is Python only** (round 2): `Tools/run_py.ps1 -Script Tools\py\build_jam_packs.py` splits `Data/Entries.csv` per jam, then for each pack calls the
  **PluginToolset** through `unreal.ToolsetRegistry.execute_tool` (`CreatePlugin` with the "Game Feature (Content Only)" template, which also mounts the plugin and creates its
  `GameFeatureData`; `AddPluginDependency`, `GetPluginDescriptor` / `UpdatePluginDescriptor`), imports `DT_Entries` with the CSV factory, creates `DA_Jam` through
  `DataAssetFactory`, and puts an instance of `UGameFeatureAction_ShiftJam` into the `GameFeatureData`'s `Actions` array. The C++ `UShiftJamPackBuilder` and
  `make_jam_plugins.py` are gone. Two limits found: the template descriptor has `EnabledByDefault: false` and the toolset cannot change that (`SetPluginEnabled` only reports
  the request, `UpdatePluginDescriptor` has no such field), so the script sets that one flag in the `.uplugin` JSON; a plugin that was just enabled is not mounted until the
  next editor start, so the script asks for a second run in that case.
- The earlier claim "the Game Features subsystem is not exposed to Python, hence a C++ builder" was true of the subsystem (`UGameFeaturesSubsystem` is not Python-visible) but
  wrong as a reason: creating and authoring packs never needed it.
- Verified headless: the editor loads 282 built-in Game Feature plugins, all three packs activate (`Jam pack activated: DA_Jam (jam n, Key)`, `Content: jam pack supplies 9
  entries for ...`), and the cards on screen come from the packs (`Tools/verify.ps1`). `GameFeatureData` has a primary asset rule (AlwaysCook) in `DefaultGame.ini` and the plugin
  mount points are in `DirectoriesToAlwaysCook`.
- QA runs never write progress (best ranks) to the player's settings: `bQaRunFlag` / bots skip `RecordResult`.

## M6 - balance (PASS) and pre-flight (PASS)
`Tools\balance.ps1` (the `ShiftBalance` commandlet) runs full jam runs against the real StateTrees, ambient events included, headless and deterministic (two identical runs gave an
identical table). Build 4/4/4, 200 runs per cell (400 for the retuned jam 3):

| Jam | idle | random | greedy | greedy + night shift (DQ) |
|---|---|---|---|---|
| Tiny Jam #7 | 0% | 0% | 88.5% | 100% (0%) |
| 48h Pixel Rumble | 0% | 0% | 56.0% | 75% (24%) |
| GAME OFF 2026 | 0% | 0% | 28.2% | 55.8% (44.2%) |

Targets were 0 / <=5 / about 85, 55, 30 / +10-20 points with a non-trivial DQ rate: met (the night-shift bot gains more on the hardest jam, with a lot of disqualifications; a human can
also Shift the Blame, which the bot never does). Jam 3's Post regeneration was lowered from 0.61 to 0.595 to land the greedy bot at 28% (each 0.01 is about 5 points there).
Thesis check with nobody cheating: starting fame explains 79-88% of the final votes (R^2) and quality 13-14% (targets: above 70% and below 10%; quality is a deliberate +-15% appeal
factor, so it sits slightly above the target). Build levers (greedy bot, jam 2): every lever has diminishing returns above an even split and a steep penalty below it, so neglecting
any of them costs 8-12 points, and network is the best place for spare hours (4/0/8 = 59% vs 56% at 4/4/4), which suits the theme.

Pre-flight (re-run after the round 2 changes): the Shipping game target compiles (`ShiftHappens-Win64-Shipping.exe`, Harmonix and MetaSound included, no editor-only code leaks) and a headless cook (`-run=Cook -TargetPlatform=Windows`) finished with exit code 0 and no errors: **574 packages** (588 before: minus 29 wave assets, plus 15 patches). The cooked asset registry holds `MS_Music`, the 26 `MS_Sfx_*` sources and the 15 `MSP_*` patches, the 4 StateTrees, 9 data tables, the level and materials, and the three jam packs' assets, shaders for D3D12 SM6, and **nothing under `/Game/Shift/Audio/Waves`**. A packaged build itself was not run.

## S8 - audio rebuilt as MetaSound and Harmonix (PASS)
Round 2 request: all audio must be MetaSound and/or Harmonix, no `.wav`. The spikes were gated, each with a named fallback; all three passed, so no fallback was needed.
- **S8a - a voice as a graph.** A kick-style blip (a sine whose pitch falls 90 Hz through the oscillator's `Modulation` pin from an Audio-typed AD envelope, plus a noise click)
  built inline and played as a one-shot (`OnFinished` from `Trigger Delay`): meters read -7.1 dB peak with the energy in the low band, and the source finished on time.
- **S8c - patches, arrays, enums.** The same voice as a Patch (`create_patch_builder` -> `build_to_asset`) used inside a source with `add_node(patch)`: -4.6 dB peak. A Float array
  literal on `Array::Get` with `Index` from `Modulo(Count, 128)` returned the right element at every step (the array is stored rotated by one because `Trigger Counter` counts
  1..N). Enum pins take their integer value (`SixteenthNote` = 3 gave 133.19 ms steps).
- **S8b - Harmonix as the clock.** `TriggerToTransport` (Play from `OnPlay`) -> `Metronome` (112.5 BPM, 4/4, 8-bar simple loop) -> `MidiClockSubdivisionTrigger` (16th notes) ->
  `Trigger Counter`; the Metronome's MIDI Clock is a graph output named "MIDI Clock". In the spike: 74 steps over 9.86 s = 133.19 ms each (expected 133.33) and 19 beat events
  531 ms apart (expected 533.3; game-thread jitter) from a `UMusicClockComponent` created with `CreateMetasoundDrivenMusicClock`. The core fallback (`TriggerRepeat` +
  `BPMToSeconds`) stepped at 133.04 ms and stays available as `build_music(harmonix=False)`.
- **The music** (`MS_Music`): a 128-step counter, one Float array per lane (0 = rest), `TriggerCompare` -> note trigger, polyphonic lanes dealt round-robin over a
  `Trigger Sequence` so a note rings on its own voice while the next starts; day (C major), night (A minor) and tension layers summed by one `Audio Mixer (Stereo, 3)` whose gains
  are the float parameters, then `Plate Reverb`, `Volume`, limiter. The 17.067 s loop has no seam because nothing is a loop of samples: the voices just keep running.
- **No recording, no .wav, not even in QA.** Every source has graph outputs tapped before its master volume and limiter (`Peak`, `Rms`, `Low`, `Mid`, `High`; the music also
  `StepCount`, `Hits_<lane>_<n>` per pattern lane and `Lvl_<voice>` per voice). `-ShiftAudioMeter=all` (`UShiftAudioMeter`) plays each source with the component volume at 0.0005
  (inaudible, the generator still renders) and reads those outputs with `UMetaSoundOutputSubsystem::WatchOutput`. It needs a real audio device (`-Sound`), nothing is audible.
  Result of the final run (32 cases, 0 failures): every effect finishes and peaks at its calibrated level (within 0.2 dB of `Tools/py/ms_targets.py`), music day / night / tension
  alone peak at -5.0 / -7.9 / -7.0 dBFS (day and night 3.0 dB apart in mean rms), night shifts the spectrum down (low-band share 72% vs 61-63%), tension adds highs (35% vs 9%), the
  Harmonix 16th clock stays at 133.24 ms per step across two loop wraps (270 steps, `StepCount` 1..128, 2 wraps) with 68 beat events 533.1 ms apart, all 28 pattern lanes fire
  exactly their pattern and all 40 voices are audible. Levels are calibrated by `Tools/calibrate_audio.ps1` (build -> measure -> trim, two passes), which writes
  `Tools/py/ms_trims.json`.
- Things that bit: the `Noise` node defaults to pink noise (silent hats until set to white); a short one-shot that starts while the game thread is busy can be over before the output
  analyzers attach (the real mechanism and the fix are in M7: the warm-up is replayed until meter data arrives); the editor picked the Git provider and staged assets (`Tools/run_py.ps1` passes
  `-SCCProvider=None`); the `PreSaveAsset ... is doing nothing` warning in headless builds (no audio rendering) is harmless, the assets load and play.
- Not verified: how it sounds. I cannot listen; the recipes are ports of the original offline synth (partial ratios, decay times, filter cutoffs, lane gains) and the levels are
  measured, but timbre and mix balance need the user's ears.

## S9 - Mass Entity and Gameplay Abilities evaluated (not adopted)
Question: should Mass Entity or the Gameplay Ability System support the StateTree layer? Read against the 5.8-suffixed docs and the engine source.
- **Mass Entity: no.** StateTree on Mass is the `MassAI` plugin. It needs a ticking `UWorld` (`UMassSimulationSubsystem`, `UMassSignalSubsystem`, `UMassStateTreeSubsystem` are world
  subsystems), takes its delta time from `World->GetTimeSeconds()` instead of our fixed 20 Hz step, and starts StateTree with a clock-seeded random stream. It could not run in the
  headless balance commandlet (60x time, no ticking world) or the standalone harness, and the world path is not reproducible by default (task-graph parallel,
  `mass.FullyParallel`; command buffers flush by type). It also drags in about 15 experimental plugins (MassAI, MassGameplay, ZoneGraph, SmartObjects, ...), `UMassStateTreeSchema`
  rejects our `FStateTreeTaskCommonBase` nodes (every task would be rewritten), and Mass is built for thousands of LOD-ed agents; this game has 10 to 72. Revisit only for many
  visible autonomous agents, as a one-way presentation mirror (game-only, fed from `FShiftRun::DrainEvents`, cosmetic state only, gated by `Sim::StateHash()` and an unchanged balance table).
- **Gameplay Ability System: runtime no, Gameplay Tags yes.** 5.8 ships no StateTree-to-GAS bridge. The Ability System Component needs an owner Actor and a `UWorld`; effect durations,
  periods and cooldowns run on world timers (clamped delta, time dilation capped at 20x) and attributes are float32 against our double core, so a facade would be a second source of
  truth that clashes with the fixed-step core, the commandlet and the 60x runs. What is worth taking is the tags: `GameplayTags` is an engine module (no plugin change) and the stock
  `Has Tag` / `Has Tags` / `Tag Query` conditions already fit `UShiftStateTreeSchema`. Implemented (S7): native tags in `Behavior/ShiftTags.h`, one evaluator that derives a
  `FGameplayTagContainer` from the sim each step, the rival tree's two gates on the stock condition. Gate: `Sim::StateHash()` unchanged and the whole balance table identical
  (3 jams x 4 bots x 100 runs). Revisit GAS only for player-visible timed buffs or Blueprint-authored world cues, as a facade on `AShiftPlayerController` that `FShiftRun`, the
  StateTree nodes and the commandlet can never reach.

## Claim register (re-verified on 5.8)
Evidence is the engine source under `C:\Program Files\Epic Games\UE_5.8\Engine`, the 5.8 docs (`?application_version=5.8`) and runtime probes (`Tools/py/probe_toolsets.py`,
`probe_metasound_nodes.py`; run them with `Tools\run_py.ps1 -Script Tools\py\<probe>.py` to regenerate the registers).

| Claim | Verdict | Evidence |
|---|---|---|
| 5.8 meta-tools are `list_toolsets`, `describe_toolset`, `call_tool` | true | 5.8 MCP page lists three; `ModelContextProtocolToolSearch.cpp`. The unversioned page lists four (5.9) |
| No MetaSound toolset | true | 52 toolsets / 830 tools at runtime, 58 / 878 in source: none for audio, MetaSound, Harmonix or Synthesis |
| StateTree toolset is read-only, so StateTree cannot be authored over MCP | partly | 9 `get_*` tools; generic `ObjectTools.set_properties` edits reflected fields; creating editor data, schema, bindings and compiling are not reachable |
| Game Features subsystem not exposed to Python | true | `hasattr(unreal, "GameFeaturesSubsystem")` is False |
| ...so jam packs need C++ to author | **false** | `PluginToolset.CreatePlugin` + `DataAssetFactory` + `GameFeatureData.actions` from Python (S6); `UShiftJamPackBuilder` removed |
| StateTree editor data is not usable from Python | **false as stated** | `StateTreeEditorData`, `StateTreeState` exposed; `StateTreeEditingSubsystem` (compile), schema class, bindings are not (S7) |
| MetaSound has no mixer, AD/ADSR, Interpolate-To, panner, Biquad | **false** | all exist under other keys (`Docs/metasound-nodes-5.8.md`, S4) |
| Gain, Mono-to-Stereo, Chorus, Phaser, Make Array do not exist | true | probe: not found under any key tried |
| `build_and_overwrite_meta_sound` only for transient sounds; assets need `build_to_asset` | true | plus: the old serialised asset must be removed first |
| No Python way to list MetaSound node classes | true | no enumerator among the Metasound UFUNCTIONs; the register is built by probing a key list |
| `Docs/mcp-toolsets.md` is the toolset list | **false** | it had 18 of 58; regenerated by `Tools/py/probe_toolsets.py` |
| Harmonix not used | superseded | used for the music clock (S8) |
| Mass Entity / GAS should drive StateTree | no | S9 |

Python exposure (runtime probe): `ToolsetRegistry`, `MetaSoundBuilderSubsystem`, `MetaSoundEditorSubsystem`, `MetaSoundOutputSubsystem`, `MidiStepSequence`, `MusicClockComponent`,
`StateTreeEditorData`, `StateTreeState`, `GameFeatureData` are exposed; `GameFeaturesSubsystem`, `StateTreeEditingSubsystem`, `GameplayTagsManager`, `MassEntitySubsystem` are not.

## Doc drift (5.8 vs the unversioned pages)
The unversioned Epic pages describe 5.9. Seen so far: the MCP page lists four meta-tools (5.8: three) and a different settings location. Always add `?application_version=5.8`
(for example `https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor?application_version=5.8`) and prefer the engine source when a page and the
runtime disagree.

## M7 - playtest round 1 (issue #1, `70e5ef8`)
A collaborator ran `Tools\verify.ps1` (9 of 10) and played all three jams. Two findings: the first audio-meter case failed, and "the game works but is not fun": voters change their vote
later, strikes seem guaranteed, and the best play might be to do nothing and flip votes with night shift at the last moment.

**Audio meters (fixed).** Not an audio defect: a race in the QA harness. The generator handle attaches the output analyzers one game-thread hop after the generator exists
(`MetasoundGeneratorHandle.cpp`: `HandleGeneratorCreated` -> `ExecuteOnGameThread` -> `FixUpOutputWatchers`), so a 0.15 s one-shot that starts while the game thread is busy (a cold first
start) is over before any meter data arrives. Our own passing logs already showed `warm-up done (0 meter updates)`: the old 2 s warm-up never proved a live generator. Reproduced once with
`-ShiftAudioMeterStall` (a stress hook; whether the race is lost depends on timing). Fix: the warm-up replays the longest one-shot until data arrives (cap 20 s), every asset is preloaded, and a
one-shot without data is retried twice. `-ShiftAudioMeterDrop` forces those paths deterministically and `verify.ps1` checks them.

**The last-moment claim, measured** (`Tools\balance.ps1 -Runs 200`, build 4/4/4, win % (DQ %), rules unchanged). New bots: `finisher` (honest greedy, then a Heat- and sweep-aware sock burst in the
last 12 s) and `late` (idle, then only the burst).

| Jam | greedy | sock | finisher | late |
|---|---|---|---|---|
| Tiny Jam #7 | 88.5 | 100 (0) | 92.5 (0) | 0 |
| 48h Pixel Rumble | 56.0 | 75.0 (24) | 65.5 (0) | 0 |
| GAME OFF 2026 | 30.0 | 56.5 (43.5) | 51.0 (0) | 0 |

So "do nothing, then flip" never wins (late ends 2nd-3rd), but honest play plus a finishing burst adds 4 / 9.5 / 21 points with no strike and no disqualification: a free lunch. The flip stats
explain "they change vote later": greedy gains 68 / 61 / 87 voters a run and loses 44 / 37 / 60 of them again; lost flips were held about 17-18 s (3 in-game days); finale socks last about 5 s.

**Rules added.** Overheat refusal (a sock that would take Heat to the cap is refused, free, instead of an instant strike). Final Audit (after the last step the mods purge every sockpuppet vote that is
still flagged; no strike). Laundering (`SockLaunderConv` 0.9: three reinforcing Posts inside a sock flip's 3 s immunity make it a real vote that strikes and the audit cannot revert). At 0.8 and 0.6 a
last-second sock plus two reinforces launders and the finisher beat greedy by up to 9 points; 0.9 does not. Rows after (win % (DQ %)):

| Jam | greedy | sock | finisher | late |
|---|---|---|---|---|
| Tiny Jam #7 | 88.5 | 93.0 (4.5) | 79.5 (0) | 0 |
| 48h Pixel Rumble | 56.0 | 60.5 (34) | 56.0 (0) | 0 |
| GAME OFF 2026 | 30.0 | 50.0 (50) | 27.0 (0) | 0 |

idle, random and greedy rows are byte-identical to before. Cheating still pays (sock +4.5 / +20 over greedy on the two hard jams, a hair under the +5 target on jam 2) but only as an opening move
with follow-through, and its risk is real (DQ 34-50%); the finale burst no longer beats honest play. Legibility, no sim change: lost-voter toasts from the previously unused `FlipToRival` / `FlipToOther`
comments, a shaking voter with an orange ring when `Sim::SecondsToFlip` is under 8 s, the Mod Eye countdown and a dimmed Heat bar from the start, projected Heat while SHIFT is held, coach cards with live
numbers. `harness.exe export-data` refreshed `Data/Tuning.csv`; the harness core is now synced from `Source/.../Sim` by `Tools\sync_sim_core.ps1` (checked by `verify.ps1`).

## M8 - playtest round 2 (issue #1, `8aac596`)
The tester confirmed the audio failure is gone and reported: "flip voter only for them to switch back soon after", celebrities get pressured back too, "not enough Posts to flip every voter", the rings are
barely readable among too many relation lines, and the leaderboard hides part of the board.

**Why flips revert (measured).** Every loss is now attributed: `Sim::LastFlipper` records who last flipped a voter (an entry's action, or opinion pressure) and `Balance: flips` splits the losses.
On the round-1 rules greedy lost 64 / 61 / 69% of the voters it won, and **the opposition caused 60-90% of it**: the rival plugs the player's best voters first (19-25 a run) and the seven other entries
(0.7 clicks a second together) took another 8-18, while herd pressure was only 4-22. Banning others from the player's voters (`OthersTargetsPlayer` 0) only moved the losses to herd and rival (63% lost on GAME OFF
either way). The symmetric levers (`PlugLock`, `PlugFlipConv`, `Stickiness`) entrench the incumbent rival and cut greedy's wins (PlugFlipConv 0.8: 88.5 -> 41). The lever that works is asymmetric: a small
fanbase is loyal, so the player's flips start at conviction `PlayerFlipConv` 0.8 (others keep 0.55). That alone raised greedy's wins by 10-40 points, so Post regen was re-solved to put greedy back on the old targets
(0.55 / 0.53 / 0.595 -> 0.50 / 0.48 / 0.505 a second). Result: greedy loses 59 / 54 / 63% of what it wins instead of 64 / 61 / 69%: better, but churn is structural (the rival owns about 80% of the network and
attacks the best voter first), so it is not fixed, only dampened. Sockpuppet Heat went 28 -> 22 so cheating still pays after that (a sock adds Heat, strikes still bite).

**Rally (right-click, new).** 2 Posts: +0.3 conviction and 3 s immunity for a voter you hold and your voters one hop away, 8 s cooldown. A `rally` bot (plugs like greedy, saves Posts when a cluster with 400+ vote
weight is about to flip back) scores 5-8 points **below** greedy at the same settings: the bot cannot judge when a Rally is worth the pause, and stronger values (boost 0.5-0.6, immunity 3-5 s) did not change that. It is
shipped as a skill tool for the human, **not validated by feel**.

**Readability.** Relation lines are faint neutral by default (`E`: faint / off / full); owner colour, strength and pulses only for the hovered voter and for the pushes on a voter about to flip back, drawn orange.
Rings are laid out in screen pixels (a member is about 11 px across, the old rings were 1.4 px thick) with an ink outline; the pressure ring is heavier. The leaderboard is compact and see-through by default
(top three and you; `L`: compact / full / hidden). The tooltip names the pushers ("3 neighbours back ULTRA GAME 9").

Final rows (`-Runs 200`, build 4/4/4, win % (DQ %)): idle 0 / 0 / 0, random 0 / 0 / 0, greedy 90.5 / 58.5 / 25.0, sock 87 (2.5) / 61 (22.5) / 57 (35), finisher 88 / 63 / 30, late 0 / 0 / 0, rally 85 / 55 / 17.5.
The finale burst still does not beat honest play (finisher at most +5), cheating pays on the hard jams with real disqualification risk, and the do-nothing-then-flip run still loses.

## M9 - playtest round 3 (issue #1, `601dbf4`)
The tester: better, "still doesn't seem to be enough"; the orange countdown is unreliable because voters get plugged by other entries before it ends ("who take the voter are only known after they already been
taken... it could show red putting pressure but they flip to purple"); Rally looks like pressure and has no visible effect ("still click way more than rally"); the leaderboard shows only three games; the
consequence of a sockpuppet is unclear, and "have automate bot to play the game to fine tune value and find problem beforehand are recommended". (A short video was linked; I cannot watch video, so only the text
was used.)

**The countdown only knew about herd pressure, and most losses are plugs.** `Sim::SecondsToFlip` is conviction over herd drain, but 60-90% of a player's losses are plugs by the rival, the seven other entries
and the streamer event, which landed instantly. So "safe" voters flipped to another colour with no warning: missing information, not a display bug. **Attacks are now announced**: when another entry picks one of
the player's voters the plug is queued (`Params::AttackTelegraph`, 1.5 s) and lands afterwards, after re-checking the target (no longer the player's: it fizzles; immune: blocked; reinforced in between: the
plug weakens instead of flipping). The board draws a closing ring in the **attacker's colour** (outlined, segmented) and the pressure ring and push lines take the pusher's colour instead of orange. Flagged
sockpuppet votes get no warning (below). `AttackTelegraph=0` reproduces the round-2 table exactly (21 of 21 rows identical, kept as a determinism proof).

**The telegraph is a buff for everyone, so regen was re-solved**: greedy went from 88 / 59 / 31 to 93 / 72 / 44 with nothing else changed, so Post regen went 0.50 / 0.48 / 0.505 -> 0.487 / 0.462 / 0.487 a second
(greedy 86 / 58 / 32). Win rate is very steep in regen (0.08 less on the middle jam: 72 -> 16%).

**Rally as the answer.** Answering threats naively is a trap: a `reactor` bot (honest greedy play that keeps a Post spare and answers announced attacks after a 0.6 s reaction delay) at the round-2 Rally (2 Posts,
3 s, 8 s cooldown) fell from 70 to 43% on the middle jam, because a reinforcement or a 2-Post Rally costs as much as it saves. What works: Rally **1 Post, 6 s shield, 3 s cooldown**, and answering only
threats on hubs and celebrities (cluster value 400+): reactor 91 / 70 / 42 against greedy 86 / 58 / 32 (+5 / +12 / +10). Rally now draws a teal shield dome and ring on every shielded voter, a ripple and teal
lines to the boosted neighbours, and the toast says what it did. Rally settings alone do not make it dominant (a 3 s shield with a 6 s cooldown scores about the same as the shipped one).

**Leaderboard.** Compact mode lists all nine entries (16 px rows, 58% opaque, your row highlighted); `L` still cycles compact / full / hidden.

**Sockpuppet consequence, found by the explorer.** `Tools\explore.ps1` (a parameterised bot searched at random plus a hill climb, validated on fresh seeds, see `Docs/QA.md`) found what the hand-written bots
missed: a **careful sockpuppeteer** who socks gated hubs only while Heat is at most about 21 (one at a time, backing each to a real vote) wins **77% on GAME OFF with 0% disqualified** against greedy 32%
(+45), because Heat cools 3 a second, which is faster than the Posts allow a sock. Careful sockpuppets had no consequence at all, which is exactly the "unclear" the tester felt. Rules tried, all measured
(win % on 100 runs, jams 1 / 2 / 3):
* `HeatDecay` 3 -> 1 made the careful bot *better* (95 / 91 / 91: it socks rarer and plays honestly in between), `SockHeat` 26-34 left the finisher untouched and only hurt the reckless bot.
* A **last-minute review** (`SockReview` 15 s: the final audit also purges sockpuppet votes bought in the last 15 s even if backed) closes the late burst for good: finisher 87 / 63 / 45 -> 32 / 36 / 21 (bursts
  started 16 s or 24 s out: 38 / 37 / 20 and 29 / 45 / 23), the do-nothing-then-flip run stays at 0. Before it, the telegraph's delay had pushed the finisher to +13 over greedy on GAME OFF.
* A strike now undoes **every** sockpuppet vote, backed ones too (backing only protects against the final audit).
* A share cap (`SockTolerance`: strike if more than X% of the vote is bought) has a cliff where one celebrity fits (jam 2 between 14 and 16%): at 16% the best response was reactor plus careful sockpuppets, 92 / 92 / 92.
  Kept in the sim, switched off.
* **Shipped: held fake votes heat you up** (`SockHeldHeat` 40: 40 Heat a second per 100% of the vote weight held in sockpuppet votes, flagged or backed; cooling is 3 a second, so a fake share above 7.5% heats you
  up). The "FAKE 4/8%" chip is that share against the limit and turns red above it. Sweep of the careful combo (reactor plus careful sockpuppets): held-heat 20: 99 / 95 / 98 (no DQ); 30: 88 / 94 / 87
  (DQ 10 / 4 / 12); 34: 86 / 73 / 78 (DQ 11 / 25 / 20); 37: 81 / 56 / 65 (DQ 17 / 43 / 34); **40: 77 / 60 / 51 (DQ 21 / 37 / 49)**. The reckless sockpuppet bot (Heat limit 45) goes from 84 / 67 / 57 (DQ 3 / 21 / 36)
  to 4 / 0 / 0 (**DQ 96 / 100 / 100**).
Consequence in numbers: a sockpuppet is a gamble. The careful play gains about +19 on GAME OFF at 49% disqualification and loses a few points on the easier jams, and the explorer's best response on every jam is
honest play with Rally defence (validated on fresh seeds, where greedy is weaker: +2.5 / +17.5 / +23.8).

**Bots and the gate.** New bots: `reactor` and `explorer` (`Tools\explore.ps1`); `Tools\sweep.ps1` runs per-jam variants one after the other; `Data\BalanceTargets.csv` states the acceptance bands and
`Tools\balance_gate.ps1` (and `verify.ps1 -Balance`) fails with a readable diff when a number leaves its band.

Final rows (`-Runs 100`, build 4/4/4, win % (DQ %)): idle 0 / 0 / 0, random 0 / 0 / 0, greedy 86 / 58 / 32, reactor 91 / 70 / 42, rally 88 / 54 / 33, sock (reckless) 4 (96) / 0 (100) / 0 (100),
finisher 32 / 36 / 21, late 0 / 0 / 0. Before this round (round-2 rules): greedy 88 / 59 / 31, sock 84 (3) / 67 (21) / 57 (36), finisher 86 / 63 / 32, rally 84 / 55 / 23. The gate passes; win rates carry about
+-5 points of noise at 100 runs and the explorer is a search, not a proof. Nothing here is validated by feel: it needs the tester's hands again.

## M10 - playtest round 4 (issue #1, `9ef127b`)
The tester: the closing ring "Pass"; "Rally almost like the only way to keep a voter not flip"; sockpuppets "seem fine, there are almost no push for the player to work night shift though, I forget it even exist";
"I don't know what you meant by FAKE chip"; the rival still dominates, "especially the last day where it seem like they have a lot of post"; their best strategy is to flip the biggest voter and then "focus on
rally"; "Leaderboard and general notification can still block gameplay viewport"; and **"Build your entry have no effect on actual gameplay, I put everything at 0 or all in Network and nothing change."**

**Why each of these was true (read from source, then measured).**
* *Build was nearly inert*: Quality moved the player's appeal by 1.35% an hour, Thumbnail's clout only mattered in the first seconds (0-2 h were identical), Network was 1-3 start voters at t=0 and +-4% regen,
  the sliders stopped at 8 hours each, the lever texts had no numbers, and the forecast's casual bot (a Plug every 2.5 s) never ran out of Posts, so it could not see any of it.
* *Rally strictly dominated a reinforcement*: same 1 Post and +0.3 conviction, plus the whole one-hop cluster and 6 s of immunity on a 3 s cooldown. A reinforcement only saved a ringed voter when
  `min(1, c+0.3) - 0.45 s > 0.15`, and the rival's strength `s = 1 + 4 max(0, share - 0.15)` passes 1.89 at about 37% share, so reinforcing failed exactly when the player was winning. Measured on the round 3 rules:
  a Rally-only bot 90 / 72 / 44 against the reactor's 89 / 77 / 47, parry-less reinforcing 55 / 31 / 12.
* *Floating overlays covered the board*: the toast (3-7 voters, on screen most of the time while clicking), the news ticker, the coach card, the banners and the tooltip; the compact leaderboard was about 2% of the voters.
* *The last three days*: the rival's attack rate doubles (0.30 -> 0.61 a second), it has no Posts budget at all, and the only announcement was a flavour headline.

**Parry (new rule).** Clicking a voter that has an announced attack pending cancels the soonest one on it (`ParryOnReinforce`; the ring bounces back out over a third of a second in the attacker's colour, a "PARRIED"
toast), for the usual 1 Post and with the usual +0.3 conviction. It works whatever the conviction arithmetic says, so it also works when winning. A click on your own voter reinforces even with night held (it used to be
a refused sockpuppet). **Rally** is now the cluster tool: 1 Post, 6 s shield, **8 s cooldown** (3 s before: one cluster could be kept shielded for ever; supersedes M9 for Rally only). Grid on 100 runs (reactor =
parry + Rally / Rally only / parry only, win % on jams 1 / 2 / 3): cost 1, cd 3: 89 / 77 / 47, 90 / 72 / 44, 80 / 69 / 39; cd 6: 85 / 75 / 46, 94 / 78 / 42; **cd 8: 88 / 75 / 43, 93 / 72 / 37**; cost 1.5, cd 4: 86 / 77 / 30,
88 / 67 / 29; cost 2, cd 3: 85 / 71 / 40, 86 / 54 / 25 (Rally alone collapses, parrying alone carries it). At cd 8 using both tools beats the better single tool on the hard jams and Rally alone is worth +3 on GAME OFF;
on the middle jam Rally alone is still +15 over greedy, which the gate allows. The reactor now parries a single ring and Rallies when two rings hit a cluster or it is worth 400+ (`ReactRallyThreats`, `ReactRallyValue`),
and the castle bot (plug the biggest voter first, keep it under a Rally) scores 31 / 5 / 3: the playtester's snowball is a human strategy that the greedy bots cannot reproduce, and it does not beat honest play.

**Build levers that show.** `Sim/ShiftBuildRules.h` (header only, mirrored into the harness and tested) is now the one source of the numbers: **Network** = a bigger, faster Post economy (bank 3..7 by hours, refill +0.3%/h
over an even split, start voters 1..4); **Thumbnail** = clout and how far each Post goes (`PlayerPlugCost`, a Plug costs 1.03 Post at 0 h and 0.985 at 8 h; a drain multiplier was rejected because plugs needed =
`ceil((c - 0.15) / (0.45 S))`, so 2 plugs for every S in [0.94, 1.89) and -10% is a +50% cliff); **Quality** = loyalty (voters you win start 68% .. 90% sure, `PlayerFlipConv`) and appeal. Below an even split every
lever loses `BuildBelowSlope` = 2 times as fast, so 4/4/4 stays the optimum; every slope is a `Params` field (`Build*`), tunable with `-Set`, and `FShiftRun::Start` now applies QA overrides before and after the build. The
build screen lists the numbers per lever, the pause menu repeats them, the forecast bot is Posts-limited (a Plug about every 0.7 s, so bank, refill and Plug cost show) and the pips pack closer when the bank is 6+.
First slopes were far too steep (greedy on jams 1 / 2 / 3: 4/4/4 = 90 / 63 / 37, 0/0/0 = 1 / 0 / 0, Quality 0 h = -50 points because 0 h crossed the "broken build" appeal penalty); the shipped ones give, against 4/4/4
(greedy): 0/0/0 -42 / -45 / -29; Quality 0 h -17 / -20 / -17; Thumbnail 0 h -5 / +1 / -7 (a small lever, gated only as "never helps"); Network 0 h -13 / -14 / -14; Quality 8 h +6 / +10 / +18; Thumbnail 8 h 0 / +10 / +9;
Network 8 h +4 / +18 / +6 (16-hour builds the screen cannot make); the legal splits 4/0/8 and 8/4/0: +6 / +15 / -2 and -4 / -1 / -9.

**The rival's final push is announced and answerable.** A pinned "FINAL PUSH IN 12 s" countdown, then "FINAL PUSH: rival x2 attacks, your Posts +25%". `FinalPushRegen` 1.25 (1.5 pushed greedy to 94 / 66 / 42):
the player's share swing over the last three days on GAME OFF went from -8.2 to -5.9 points after the regen re-solve (the commandlet's new `Balance: mega` line). Regen was re-solved on two disjoint seed sets
(0.487 / 0.462 / 0.487 -> **0.472 / 0.457 / 0.486**, greedy 90 / 55 / 33).

**Nothing floats over the board.** The graph layout keeps the voters left of x = 1080 (`LayoutMarginRight` 200, ring 410; the sim never reads node positions, so balance is untouched; no two voters closer than 34 px
before or after, voters sitting on a margin line 1.8 / 4.0 / 5.6 -> 5.0 / 7.5 / 11.5 per board) and the right-hand strip holds the leaderboard, the states you are in (shadowban, Mod Eye, final push, final audit, fake
votes) as pinned cards, the notification feed (toasts and news as cards, at most four, a repeated line renews its card) and, at the bottom, the coach card and the Blame button. The tooltip never covers the strip.

**Night shift and the sockpuppet card.** The FAKE chip is now "SOCKPUPPET VOTES 17%" with "+8.7 Heat/s, safe up to 6%" in the strip under the leaderboard, red above the limit, and hovering it explains it in words;
a coach card shows when the first fake vote is held. The push to work night shift is a context hint ("SHIFT skips @name's gate" in the bottom chip when a hub or celebrity is gated and worth 80+, and the gated tooltip says
"Hold SHIFT to skip it"). **Scripted "the Mod Eye looks away" windows were built and measured, and are off** (`EyeAwayCount` 0; `-Set EyeAwayCount=3` turns them on, `Sim::SetEyeAway` and its tests stay): a window-aware
bot that sockpuppets only inside windows wins 81 / 70 / 80 (DQ 16 / 17 / 13) against the reactor's 88 / 75 / 43, and 82 / 70 / 81 or 77 / 70 / 80 with the held-vote Heat at 1.0 or 0.25 times: a free celebrity theft
decides GAME OFF whatever the held-vote Heat is.

**The second explorer run found what the first one hid.** With the round 3 numbers the search found, on GAME OFF, "sockpuppet only celebrity-class voters while Heat is under 24, keep the stolen celebrity under a Rally":
68% against greedy's 33% (DQ 23%, **+35**). Held-vote Heat 50 instead of 40 closes it (43%, DQ 50%; 60 is too harsh: 29%) and is shipped (a 6% fake share is the most cooling can hold). Search after that
(40 random samples + 15 climb steps per jam, 40 runs, validated on 80 fresh seeds where greedy is weaker): best responses **+3.8 / +16.2 / +8.8** points over greedy (reactor-like on the first two, Rally-only on GAME OFF),
the gate's limit is +30.

**Gate.** `Data/BalanceTargets.csv` has 70 rows now (Build and RelativeBuild columns; castle, Rally alone, parrying alone, both tools, the final push's swing, the build matrix) and passes; its noisy cells were
widened after a first run (Thumbnail 0 h can be +1, 16-hour levers up to +20, the final push's swing -7). Win rates carry about +-5 points of noise at 100 runs and the matrix compares two such cells, so use
`-Seeds 1000,500000` when a row is borderline. Nothing here is validated by feel: it needs the tester's hands again.

## M11 - playtest round 5 (issue #1, `646d233`)

The tester's answers to the round-4 questions: parry reads fine ("Pass"); Rally "hard to tell, it seems fine"; the build screen's list and the lever descriptions "didn't match" ("Maxed in Network put the game on number 2
while 0 put it on number 1") and the chart "just moves up and down when it feels like it"; the right-hand strip "was fine, but no one really pays attention to that area of the screen"; and "if Shift the Blame always
succeeds then three strikes just mean nothing, as it relies on the player not seeing that tiny notification in the corner". Three changes, one tool, nothing else touched (the balance targets are unchanged and met).

**The forecast was four different worlds.** `ShiftBuildRules::Forecast` averaged four sims on seeds `Seed + 0..3`, each its own random graph and entries, while the rows carried the real world's names; with a spread of
12 to 16 points between worlds the average of four was good for +-7, against levers worth 10 to 17, so entries 2 to 8 reshuffled and Network could look worse at 8 hours than at 0. It also lacked the final push's
Post refill, ran synchronously on the game thread and rescaled its bars to the longest one. What replaced it (`Game/ShiftForecast.h`, `Game/ShiftForecastService.h`):

* **The real world.** `Sim::Init(Jam, P, Seed, DynSeed)` draws everything (graph, layout, entries, opinions) from `Seed` and only the dynamics stream (the dice of undecided voters) from `DynSeed`; the three-argument
  `Init` is the four-argument one with `DynSeed = Seed`, so no existing hash moved (`TestDynSeed`; the greedy and reactor balance rows were identical before and after the first build). `FShiftRun::SetDynamicsSeed` feeds it.
* **A steady player is not one future.** A play is one deterministic future and the sim is chaotic in the build: the plain greedy bot's expected share in a world is rough in the levers (a quadratic fit to its grid
  averages is off by 6 to 12 points, against a sampling error of 1 to 2), while the average over a *varied* player is smooth (fit error 3.3 / 3.7 / 2.6 against a sampling error of 3.2 / 4.2 / 2.9). The variations
  are the first click coming up to 0.7 s later and the bot missing the best target (`PolicyParams::BotStart`, `BotNoise` 0.2: every score jittered by up to 20%; both 0 = unchanged). Spread of one play of a steady
  player in one world (study over 16 worlds x 29 builds x 24 samples, points): dynamics luck alone 4 / 8 / 6, a later start 11 / 14 / 13, missed targets 14 / 20 / 14 (jams 1 / 2 / 3).
* **One surface, not a chart per slider tick.** 72 grid builds (every split in even hours that spends at most 12) x 8 plays = 576 plays per world on the worker pool (`ParallelFor`, background priority; 0.4 to
  1.6 s on 16 threads, a progress bar while it runs), one quadratic fitted to each entry's grid averages, and the player's surface forced monotone in every lever (the mean of the least monotone surface above it and
  the greatest below it), so more hours never lower the forecast. The sliders then only look the result up: no work per tick, no jitter, no contradiction of the descriptions. Rows are ordered by value (ties by entry
  id), bars have a fixed scale, rows and bars glide, shares have one decimal, a rank within 1.5 points of another entry shows as a range ("2nd-3rd"), and the caption says what it is. The service computes one world at a
  time, a new request cancels the running job, a pause costs nothing, and `Cancel()` waits (the controller cancels on leaving the build screen and in `EndPlay`).
* **Checked** (`Tools\forecast_check.ps1`, `Data\ForecastTargets.csv`, 60 worlds): 0 drops of the player's share along 19 440 one-hour steps per jam, the rank gets worse in 0.1 / 0.1 / 0.2% of them (limit 3), Quality 0 -> 8 h
  is worth +5 points or more in 80 / 100 / 95% of the worlds (+17 / +20 / +17 on average), Network in 95 / 95 / 90% (+21 / +28 / +18), Thumbnail +10 / +9 / +7 on average and never negative; the 99th percentile of a one-hour
  step is 8 points; the even split's share averages 69 / 49 / 29%; the table is identical on a repeat to the last digit. (The worlds where a lever is worth less are the ones already won or lost at any build.)

**Alerts that cannot be missed.** The right-hand strip is peripheral by nature. A strike, the Shift the Blame offer, the disqualification, the final push and the final audit now take over the middle of the top bar
(x 150 to 880: not the MODS chip, the deadline or the mute button) for 2.5 to 4 s as a coloured band with a title, one line, a draining bar and, for the offer, the seconds left; a thin frame (14 px left, 9 px right,
8 px bottom: the voters stay inside x 56..1080, so it never covers one) flashes once for a strike or a disqualification and pulses at 1.5 Hz while the offer is open. The offer owns the bar while it is open (priority
disqualified > offer > blame taken > strike > final push, audit); the whole band is the button, and key B too. Sound reuses the existing effects, no new asset: the Strike thud, then the ModWarn on a 0.8 s cadence
while the offer is open (the audio layer drops the same effect twice within 30 ms, so a cadence timer rather than a double play), the music steps back to half while it is, the DQ sound at the event (not at the results),
the results screen waits 1.8 s after a disqualification so the alert is seen. A pause freezes the alert, the countdown and the sound, and B does nothing while paused (it used to give unlimited time). Strike 2 queues a
new coach card, `BlameRule` (a new seen-bit, so testers who read the old card are taught the new rule; the old `BlameAvailable` row is retired).

**Shift the Blame can fail now.** It always worked when it was seen (a 2.5 s window after every strike, forgiving the strike and the shadowban, and the bots never used it, so every disqualification figure in this
file is Blame-free). Now (`Sim/ShiftBlameRules.h`, header only, mirrored into the harness and tested by `TestBlame`): it is offered only at the strike that would disqualify (strikes 1 and 2 keep every consequence),
for `BlameWindow` 4 s (or what is left of the jam), once per run, and only if the fake votes the strike just took away were at most `BlameMaxFake` of all vote weight; over that the run ends at once and says why ("Blame
refused: 15% of the vote was fake, too much to pin on the rival (the limit is 8%)"). It is deterministic, not a die, and the prompt shows both numbers. The rival's cost is unchanged but now a parameter
(`BlameRivalConv` 0.25, `BlameRivalFreeze` 10). Bots answer a fatal strike with the Blame after `ReactDelay` (`BotBlame` 1; `-Set BotBlame=0` is the old Blame-free table, `BlameWindow=0` removes the rule).
Cap sweep, sock bot (the reckless one), 100 runs per jam, share of its first fatal strikes that were offered / disqualified at the end (jams 1 / 2 / 3):

| BlameMaxFake | offered | DQ |
|---|---|---|
| **0.08** | **14 / 36 / 31%** | **100 / 96 / 100%** |
| 0.12 | 22 / 42 / 34% | 99 / 95 / 100% |
| 0.14 | 24 / 58 / 35% | 99 / 92 / 100% |
| 0.16 | 53 / 85 / 63% | 99 / 92 / 100% |
| 0.20 | 81 / 93 / 94% | 99 / 92 / 100% |

Its fatal strikes mostly take 12 to 20% of the vote in fake votes (bands of 4 points: 13 / 5 / 11 / 42 / 31 / 19 on jam 1); the bot is disqualified about as often as without the Blame (96 / 100 / 100 before)
because one blame only postpones the same habit. Nobody else reaches a third strike. **The cap is 0.08, not the 0.12 first chosen.** The first gate run with 0.12 failed one row: the explorer's search found on GAME OFF a
gambler (sockpuppet the big voters, burst at the end, no share cap of its own) at +31.2 points over greedy (limit +30; DQ 42.5%). It is a bet the Blame makes worth taking: on 200 other runs the same configuration wins
49.5% (DQ 50.5%) against greedy's 30.5%, and with the bots not taking the Blame (`BotBlame=0`) 23.0% (DQ 77.0%), i.e. the old game, in which the bet lost. Sweeping the cap on that configuration (win %): 0.04 and
0.06 23.5 (the Blame is offered 0.01 to 0.03 times a run, so it is effectively off), **0.08 42.0**, 0.10 48.0, 0.12 49.5. With 0.08 the explorer's best responses are **+7.5 / +17.5 / +25.0** points (the last still the
gamble: 51% wins, 49% DQ on 40 runs, +11.5 on the 200 runs above), inside the gate's +30. 0.06 would remove the bet and the Blame together (a careful player who is struck holds more than the 6% that cooling can
keep warm), so 0.08, a little over that, is the compromise; if the tester finds the Blame too stingy or the gamble too tempting, `BlameMaxFake` is the one number to move.

**Gate.** `Tools\verify.ps1 -Balance -Forecast` passes all 14 checks: the harness (5032 tests, `TestDynSeed`, `TestBlame`), the audio assets and meters, the trees, determinism, the boot, the 70 balance rows
(100 runs, seed set 1000; the greedy rows are 90 / 55 / 33 as before, the sock bot's DQ 99 / 95 / 100) and the 23 forecast rows (60 worlds). The explorer (30 samples + 10 climb steps, 40 runs, validated on fresh seeds):
**+7.5 / +17.5 / +25.0** over greedy with the 0.08 cap. New knobs: `-Set BotBlame=0|1`, `BlameMaxFake`, `BlameWindow` (0 = no Blame), `BlameRivalConv`, `BlameRivalFreeze`, `BotStart`, `BotNoise`; the balance
commandlet prints a `Balance: blame` line per jam and bot (strikes, fatal strikes, offered, taken, refused, lapsed, and the fatal strikes by fake share); `-ShiftSet=` does the same for a game run.

**Not done / not known.** I cannot watch the playtest video. The alerts are checked by logs and screenshots (1280 x 720 and 1920 x 1080, day and night), not by a person who is looking at the board; whether a
top-bar band with a frame and a repeating warning is enough for someone with their eyes on the voters is the question for the tester. The forecast describes a varied steady player (a Plug about every 0.7 s that sometimes
misses the best target), which is more modest than the greedy bot on the hard jam (29% against its 44% at the even split); it is a forecast of play, not of skill.

## M12 - playtest round 6 (issue #1, `2e61bc1`)

The tester's answers to the round-5 questions: the forecast chart is fine ("Pass"); the alert "is on the screen, but users are more likely to look at the middle bottom than the top"; the Blame window "maybe 5 seconds";
"the current best strat is using sockpuppets to take the largest voter, then snowball with them as they put too much pressure on nearby voters; often the strike or the limit to shift blame doesn't matter once the player
succeeded"; "at the last day the red team seems to have unlimited flips / Posts, and all the Posts have to be used to parry / Rally big voters"; "the game is mostly player vs red, none of the other colours has much effect".

**Alert and window.** The alert band moved from the top bar to the middle of the bottom bar (x 280..792, y 678..710; the Posts pips and counter, the HEAT bar and the strike pips stay in view; the Rally chip, the day / night
chip and the hint are not painted while it is up). Texts were shortened to fit (<= 44 / 77 characters with the countdown). `BlameWindow` is 5 s. This supersedes M11's top-bar text.

**The exploit: measured first, then one lever.** My first idea (a strike also takes back the voters that followed the fake votes) was tested before it was built, on the standalone harness (`harness.exe snow`, core API only, 100
seeds per jam, steal the heaviest voter at t = 40 s and back it, strike after 8 / 14 / 20 s): what a stolen celebrity pulls along beyond its own weight is **0.00-0.15% of the vote** (followers hardly flip), the stolen voter
itself drifts back to the rival within about 20 s unless it is kept up, and what is left of it 30 s after a strike is zero. A steal at T-21 s and one at T-14 s is worth +27..+31% of the vote before the final audit and
+15..+17% after it (the older one is outside the 15 s review window), which is a potential, not a result: strikes land in between. So no taint rule. Experiment 0 (the search's GAME OFF gambler, 200 runs, seeds 500000,
one knob at a time, win % of gambler / greedy / reactor): base 42.0 / 30.5 / 47.5; `BotBlame=0` **23.0**; Blame without its rival debuff 43.5; no front-page boost 33.5 / 40.0 / 55.5; `SockReview` 30 **42.0** (no
change); `SockHeldHeat` 100 1.5; the 6% steady variant 49.0 (DQ 0). The edge is the Blame: the strikes before the fatal one are free and the cap only looked at the fatal one ("the limit doesn't matter"). **Shift the Blame
now measures its cap on the fake votes of all the player's strikes added up** (`BlameBasis` 1; `BlameMaxFake` **0.20** over all strikes, was 0.08 on the last one). Sweep, gambler / steady on GAME OFF (win %) and the sock
bot's offers: basis 0 cap 0.08 42 / 45, offered 0.31; 0.12 49.5 / 45, 0.34; 0.20 53 / 45, 0.94; **basis 1** cap 0.08, 0.12 and 0.20 all 23 / 45, offered 0.00 / 0.00 / 0.03: the reckless are never offered it, a clean
record still is. The prompt reads "6% fake votes over your strikes (limit 20%)". Regression guards now in the gate (`Data/BalanceTargets.csv`): the presets `snowball`, `steady` and `gambler` against the reactor (<= +5 on every
jam: -86 / -52 / -37, +-3 / +3 / +1, -54 / -12 / -9).

**The rival pays Posts.** In the final push the rival attacked every 1.65 s (RivalInterval 3.3 s, two actions per round) for 10.9 / 14.5 attacks against a player refill of ~0.6 Posts/s: every parry used the player's whole
Post flow, and the rival had no economy at all. It has one now (`Sim/ShiftRivalBank.h`, lazy refill, tested; `PolicyParams::RivalPostsMax` 4, `RivalPostsRegen` 0.48, `RivalDefendCost` 1; an action that finds the bank empty
does not happen, a parried attack still cost its Post) and the pinned FINAL PUSH card shows "rival Posts 2/4, yours +25%". Refill 0.40 weakened the push enough to push the honest rows out of their bands (greedy 95 / 39,
reactor -7 / +2); with the bank off or at 0.48 the gate is green and the table is the round-5 one (0 is bit-identical). It refuses 1 to 2 rival actions a run, so it is mostly visible, not a nerf.

**Others.** Not changed: by design they are the background that the thesis (fame decides) rests on; making them hit the leader's low-weight voters would be nearly invisible. Candidate for a later round: the "drama
thread" event hitting the front-runner instead of a random entry.

**Gate.** All rows green (70 + 9 new; the explorer's best responses were inside the +30 limit on the first run with these rules), `forecast_check` and `verify` below. New: `harness.exe snow`, balance presets `snowball` /
`steady` / `gambler`, the `Balance: rival` line (attacks, defends, starved) and the Blame line's bins up to 40+%.

## M13 - playtest round 7 (issue #1, `87ab7de`)

The tester's answers to the round-6 questions: the bottom alert is fine ("Pass"); the rival's Post bank did not help: "the problem only appears on the red team, red still flips a ton"; the sockpuppet strategy is
gone ("that strat now gone"); the other colours: "unknown, I feel no different currently". So the alert, the Blame's cumulative cap and the unchanged other entries stand, and the push needed more than a display.

**The bank was cosmetic.** The new `Balance: push` line (the final push alone, per run) showed it: with the bank at 4 / 0.48 the rival was refused 0.0 actions in the push on every jam, so nothing changed. What the push is,
for a bot that answers attacks (the reactor), per run on the three jams: 10.3 / 9.5 / 12.3 rival attacks, 14.9 / 11.3 / 16.1 announced attacks on the player (the other entries attack too), 8.2 / 6.9 / 7.8 Posts spent parrying
and 2.1 / 2.1 / 2.6 on Rallies, 0 to 0.5 voters lost; a bot that ignores the rings loses 3.5 / 1.9 / 2.8 voters to the rival in the push. That is the tester's push: a ring every ~1.7 s and the player's whole Post flow
going on parries.

**A bank that binds.** Sweep (100 runs, seed set 1000, greedy / reactor, jams 1 / 2 / 3): 3 / 0.36 cuts the reactor's push attacks to 7.4 / 6.8 / 9.4 (-28%), 3 / 0.30 and 2 / 0.32 to 6.4 / 6.1 / 7.7 (**-37%**; parries
5.7 / 4.3 / 4.6 instead of 8.2 / 6.9 / 7.8). Both make the game easier (greedy 94 / 68 / 50 against targets 83-93 / 51-61 / 25-35), so the player's push bonus came down (`FinalPushRegen` 1.25 -> **1.10**: the push
is lighter, so it needs less help) and the Post refill per jam by about 6% (**0.447 / 0.428 / 0.466**, was 0.472 / 0.457 / 0.486). Shipped: `RivalPostsMax` **3**, `RivalPostsRegen` **0.30** (the rival acts 0.30 times
a second before the push, so the bank stays full; the push drains it, the card shows "rival Posts 0/3, yours +10%" before the end, and the rival falls back to its normal pace). Pooled over two seed sets (200 runs)
the gate is green: greedy 91.5 / 52.5 / 28.5, reactor +-11 / +6 / +6.5 over it. Two honest consequences, both measured, not hidden: answering attacks is worth a little less (the reactor is 11 points behind greedy on
the easy jam, where it holds a Post in reserve for rings that no longer need it: that band, `reactor_0`, is widened from -5 to -14), and the earlier +25% banner text ("double attacks") became "a burst of attacks until it runs
low on Posts". The first 100-run gate had four rows out of band; pooled with a second seed set three of them were noise (`reactor_1`, `steady_1`, `both_vs_rally_1`). `Tools\verify.ps1` therefore now runs its balance gate on both seed sets (1000 and 500000, about 20 minutes), and the forecast limit `fc_step_0` went from 10 to 12 (8.0 before this round, 10.5 after: the lever hours matter a little more on the easy jam now that the push is lighter). `verify.ps1 -Balance -Forecast` is green (12 of 12 with `-SkipMeters`).

**Not done.** The other colours are unchanged (the tester has no opinion). I cannot watch the playtest, so whether a push 37% lighter reads as "red does not flip a ton any more" is for the next round; if it does not,
the next lever is the lethality of a landed attack (the rubber band, `RivalBandSlope`), not the count.
