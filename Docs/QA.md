# QA and testing notes

Everything here runs **headless** (no window, no console, no sound) unless stated otherwise. See `Docs/Decisions.md` ("Headless runs").

## Runners

| Script | What it does |
|---|---|
| `Tools\build.ps1 [-Target ShiftHappens -Config Shipping]` | Build with UBT (default: `ShiftHappensEditor` Development). The Shipping game target also compiles clean |
| `Tools\run.ps1 -Shot <png> [-ShotDelay s] -Quit [-ResX -ResY] [-Extra @('-Flag',...)]` | Game run through `UnrealEditor-Cmd.exe -game -RenderOffScreen -ForceRes -NoSound`; log in `Saved\run_last.log`. `-Visible` and `-Sound` opt in to a window and sound |
| `Tools\run_py.ps1 -Script Tools\py\<x>.py` | Python builders and probes in the headless editor; log in `Saved\py_<x>.log` |
| `Tools\balance.ps1 -Runs N [-Jam 0\|1\|2\|all] [-Bot idle,random,greedy,sock,finisher,late,rally,reactor\|all] [-Build Q,T,N] [-Set Name=Value,...] [-Tag name] [-NoStateTree] [-NoMods]` | `-run=ShiftBalance` commandlet: full runs against the real StateTrees; table in the log and `Saved\Balance\*.csv`, plus one `Balance: flips ...` line per cell (voters won, lost, reverted, purged, and how long a flip lasted). `-Bot`, `-Build` and `-Set` are comma lists (string arrays in the script, so PowerShell does not flatten them); `-Set` overrides any sim `Params`, jam or policy field by name without a rebuild (for example `-Set SockLaunderConv=0.8,LateSeconds=24`); `-Tag` gives the run its own log so experiments can run side by side |
| `Tools\sweep.ps1 -Variants 'label=Name=Value,...','label2=@0:Regen=0.46 @1:Regen=0.40 ...' [-Bot greedy,reactor] [-Runs 100]` | Tuning sweep: one balance run per variant and jam (sequential), one line per variant with each bot's win % (and DQ %). `@0:` / `@1:` / `@2:` limit a set to a jam. The jam's Posts per second is spelled `Regen` |
| `Tools\explore.ps1 [-Samples 30 -Climb 10 -Runs 40 -Jam all -Set ...]` | `-run=ShiftExplore`: looks for dominant strategies. One parameterised **explorer** bot (switches and thresholds for every habit of the other bots) is searched at random plus a hill climb, seeded with the hand-written strategies, inside one process; the best configuration per jam is validated on fresh seeds against greedy (`Explore: jam=N best-response ... = +X points`). Table in `Saved\Balance\explore_*.csv` |
| `Tools\balance_gate.ps1 [-Runs 100] [-Log file] [-Explore] [-Set ...]` | The balance gate: runs the table and checks every row of `Data\BalanceTargets.csv` (idle, random, greedy bands, finisher, late, sock, reactor, optionally the explorer's best response); prints PASS / FAIL per target and exits 1 on any miss. `verify.ps1 -Balance` runs it |
| `Tools\build_harness.ps1 [-Run test]` | The standalone C++ sim harness (determinism and invariants of the numeric core, no Unreal) |
| `Tools\verify.ps1 [-SkipMeters] [-Balance]` | **One command**: the harness core is a verbatim copy of `Source\ShiftHappens\Sim`, audio assets (MetaSound only, no .wav), StateTree activity and determinism, game boot (clock, packs), the audio meters, and the slow-first-start meter check; `-Balance` adds the balance gate (about 8 more minutes) |
| `Tools\sync_sim_core.ps1 [-Check]` | Copies `Source\ShiftHappens\Sim` to `Tools\SimHarness\core` (the harness builds the very same files); `-Check` only compares |
| `Tools\build_audio.ps1` | Rebuilds every MetaSound patch, effect and the music with the Python Builder API (removes the old `MS*.uasset` first) |
| `Tools\calibrate_audio.ps1 [-Reset]` | Build -> measure with the in-graph meters -> adjust `Tools\py\ms_trims.json`, until every sound is within 0.5 dB of its target |
| `Tools\mcp_editor.ps1 start\|stop` | Headless editor hosting the Unreal MCP server (loopback) |
| `Tools\run_py.ps1 -Script Tools\py\probe_toolsets.py` / `probe_metasound_nodes.py` / `verify_audio.py` | Regenerate `Docs/mcp-toolsets.md`, `Docs/metasound-nodes-5.8.md`; check the audio assets |

## Game command-line flags (all optional)

| Flag | Meaning |
|---|---|
| `-ShiftScreen=title\|jams\|build\|play` | Start on a screen (default: `play` if any run flag below is given, otherwise `title`) |
| `-ShiftJam=0..2`, `-ShiftSeed=N`, `-ShiftBuild=Q,T,N` | Jam, seed (fixes all seeds, including the "random" ones), build hours |
| `-ShiftBot=idle\|random\|greedy\|sock\|finisher\|late\|rally\|reactor\|explorer`, `-ShiftSpeed=N`, `-ShiftWarp=S` | Scripted player, fast-forward factor, sim seconds to run before the first frame. `finisher` plays honestly and bursts sockpuppets in the last `LateSeconds`; `late` does nothing until then; `rally` plays like greedy and Rallies the cluster most at risk; `reactor` plays like a careful human (keeps a Post spare, answers announced attacks on hubs and celebrities after a 0.6 s reaction delay with a Rally or a reinforcement); `explorer` is the parameterised bot of `explore.ps1` (set its `X*` knobs with `-Set`) |
| `-ShiftNight=0..1` | Force the night blend (also counts as SHIFT held) |
| `-ShiftCursor=X,Y` | Fake cursor in design pixels (1280 x 720) |
| `-ShiftClicks="T,X,Y[,n];..."` | Scripted board clicks at game time T (`n` = sockpuppet) |
| `-ShiftUi="T,Id[,V];..."` | Scripted UI actions by id (`EShiftUi` in `UI/ShiftHudModel.h`; V = slider position 0..1) |
| `-ShiftMouse="T,X,Y,m\|d\|u;..."` | Mouse move / left down / left up injected through the real widget (tests hit regions and slider drags) |
| `-ShiftShot=<png> -ShiftShotDelay=S -ShiftQuit` | Screenshot (with UI) after S seconds, then exit |
| `-ShiftTutorial` / `-ShiftNoTutorial` | Force or skip the coach cards (QA runs skip them by default) |
| `-ShiftNoStateTree` | Native stand-in policies instead of the StateTrees |
| `-ShiftNoAudio`, `-ShiftMasterVolume=0..1` | Skip audio; scale all sound |
| `-ShiftAudioMeter=all\|sfx\|music\|<asset names>` | Audio QA: plays MetaSound sources inaudibly and logs their in-graph meters (needs `-Sound`; nothing is recorded, no .wav); exit code 3 on failure. The run starts with a warm-up that is replayed until meter data really arrives, and a one-shot that ends without any data is retried twice before it fails |
| `-ShiftAudioMeterDrop=<starts>`, `-ShiftAudioMeterStall=<ms>@<starts>` | Audio QA hooks, start numbers count every `Play()` of the run: `Drop` ignores the meter data of those starts (forces the retry paths deterministically; `verify.ps1` uses it), `Stall` stalls the game thread as the generator of those starts appears (a stress test for the cold-start race; whether it is lost depends on timing) |
| `-ShiftNoUI`, `-ShiftNoAutoPause` | No HUD; do not pause on focus loss (QA runs never auto-pause) |

QA runs never write progress to the player's settings (best ranks, tutorial flags are skipped).

## What has been checked (and how)

* **Sim core**: determinism and invariants in the standalone harness; the same results from the in-engine balance commandlet (identical table on two runs with the same seeds).
* **Screens**: screenshots of every screen at 1280 x 720 and 1920 x 1080 (title, jam select, build, day, night with heat and a shadowban, blame offer, results); a scripted run through the real widget from title to jam card to slider drags to upload (`-ShiftMouse`).
* **StateTrees**: all four trees compile and run in-engine for the full run; the log prints what the trees did (`Behaviour activity: ...`), a missing tree is an Error and falls back to the native stand-ins; results are deterministic (`Tools\verify.ps1` runs the balance table twice and compares). The rival's Mega / Defend gates use Gameplay Tags; the full balance matrix was identical before and after.
* **Game Features**: the three jam packs (authored from Python with the PluginToolset) activate in the game process and supply the entries; the jam cards come from them.
* **Audio** (all MetaSound / Harmonix, nothing recorded): `-ShiftAudioMeter=all` plays 26 effects and the music inaudibly and reads in-graph meters: every effect finishes and peaks at its target level (within 0.2 dB), the music stems are below -1 dBFS, night shifts the spectrum down, tension adds highs, day and night are about equally loud, the Harmonix clock steps every 16th note (133.3 ms) across the loop wrap and delivers beat events, all 28 pattern lanes fire exactly their patterns and all 40 voices are audible. **I could not listen**, so the mix needs an ear.
* **Build targets**: Development Editor and Shipping game targets compile; a pre-flight cook is described in `Docs/Decisions.md`.
* **Balance** (`Tools\balance.ps1 -Runs 100`, build 4/4/4, current rules; the before and after tables are in `Docs/Decisions.md` M7, M8 and M9): idle 0% wins, random 0%, greedy 86% / 58% / 32% on jams 1 / 2 / 3, the reactor (greedy that answers announced attacks) 91% / 70% / 42%, the reckless sockpuppet bot 4% / 0% / 0% with 96 / 100 / 100% disqualified, the honest-then-burst bot 32% / 36% / 21% and the do-nothing-then-burst bot at 0%. `Tools\balance_gate.ps1` checks all of it against `Data\BalanceTargets.csv`; `Tools\explore.ps1` searches for strategies that beat honest play (best response today: honest play with Rally defence, +2.5 / +17.5 / +23.8 points on fresh seeds). With nobody cheating, starting fame explains about 80-88% of the final votes and quality about 13%.
