# QA and testing notes

Everything here runs **headless** (no window, no console, no sound) unless stated otherwise. See `Docs/Decisions.md` ("Headless runs").

## Runners

| Script | What it does |
|---|---|
| `Tools\build.ps1 [-Target ShiftHappens -Config Shipping]` | Build with UBT (default: `ShiftHappensEditor` Development). The Shipping game target also compiles clean |
| `Tools\run.ps1 -Shot <png> [-ShotDelay s] -Quit [-ResX -ResY] [-Extra @('-Flag',...)]` | Game run through `UnrealEditor-Cmd.exe -game -RenderOffScreen -ForceRes -NoSound`; log in `Saved\run_last.log`. `-Visible` and `-Sound` opt in to a window and sound |
| `Tools\run_py.ps1 -Script Tools\py\<x>.py` | Python builders and probes in the headless editor; log in `Saved\py_<x>.log` |
| `Tools\balance.ps1 -Runs N [-Jam 0\|1\|2\|all] [-Bot idle\|random\|greedy\|sock\|all] [-Build Q,T,N] [-NoStateTree] [-NoMods]` | `-run=ShiftBalance` commandlet: full runs against the real StateTrees; table in the log and `Saved\Balance\*.csv` |
| `Tools\build_harness.ps1 [-Run test]` | The standalone C++ sim harness (determinism and invariants of the numeric core, no Unreal) |
| `Tools\build_audio.ps1` | Renders the music and sound effects and prints an analysis table (peak, RMS, clipping, DC, centroid, loop seam) |
| `Tools\mcp_editor.ps1 start\|stop` | Headless editor hosting the Unreal MCP server (loopback) |
| `python Tools\py\analyze_wav.py <wav> [--scale k]`, `compare_spectra.py <recorded> <offline>` | Analyse a recorded playback |

## Game command-line flags (all optional)

| Flag | Meaning |
|---|---|
| `-ShiftScreen=title\|jams\|build\|play` | Start on a screen (default: `play` if any run flag below is given, otherwise `title`) |
| `-ShiftJam=0..2`, `-ShiftSeed=N`, `-ShiftBuild=Q,T,N` | Jam, seed (fixes all seeds, including the "random" ones), build hours |
| `-ShiftBot=idle\|random\|greedy\|sock`, `-ShiftSpeed=N`, `-ShiftWarp=S` | Scripted player, fast-forward factor, sim seconds to run before the first frame |
| `-ShiftNight=0..1` | Force the night blend (also counts as SHIFT held) |
| `-ShiftCursor=X,Y` | Fake cursor in design pixels (1280 x 720) |
| `-ShiftClicks="T,X,Y[,n];..."` | Scripted board clicks at game time T (`n` = sockpuppet) |
| `-ShiftUi="T,Id[,V];..."` | Scripted UI actions by id (`EShiftUi` in `UI/ShiftHudModel.h`; V = slider position 0..1) |
| `-ShiftMouse="T,X,Y,m\|d\|u;..."` | Mouse move / left down / left up injected through the real widget (tests hit regions and slider drags) |
| `-ShiftShot=<png> -ShiftShotDelay=S -ShiftQuit` | Screenshot (with UI) after S seconds, then exit |
| `-ShiftTutorial` / `-ShiftNoTutorial` | Force or skip the coach cards (QA runs skip them by default) |
| `-ShiftNoStateTree` | Native stand-in policies instead of the StateTrees |
| `-ShiftNoAudio`, `-ShiftMasterVolume=0..1` | Skip audio; scale all sound |
| `-ShiftRecordAudio=<wav> [-ShiftRecordStart=S -ShiftRecordSeconds=S]`, `-ShiftSfxCycle`, `-ShiftRawAt=S` | Record the master submix; play every effect in turn; play a plain SoundWave as a control (needs `-Sound`; QA audio runs force the unfocused-volume multiplier to 1) |
| `-ShiftNoUI`, `-ShiftNoAutoPause` | No HUD; do not pause on focus loss (QA runs never auto-pause) |

QA runs never write progress to the player's settings (best ranks, tutorial flags are skipped).

## What has been checked (and how)

* **Sim core**: determinism and invariants in the standalone harness; the same results from the in-engine balance commandlet (identical table on two runs with the same seeds).
* **Screens**: screenshots of every screen at 1280 x 720 and 1920 x 1080 (title, jam select, build, day, night with heat and a shadowban, blame offer, results); a scripted run through the real widget from title to jam card to slider drags to upload (`-ShiftMouse`).
* **StateTrees**: all four trees compile and run in-engine for the full run, the native fallback works, results are deterministic.
* **Game Features**: the three jam packs activate in the game process and supply the entries; the jam cards come from them.
* **Audio**: the recorded master submix matches the offline stems band by band, night shifts the spectrum down, and all 26 effects are audible above the music. **I could not listen**, so the mix needs an ear.
* **Build targets**: Development Editor and Shipping game targets compile; a pre-flight cook is described in `Docs/Decisions.md`.
* **Balance** (`Tools\balance.ps1 -Runs 200`, build 4/4/4): idle 0% wins, random about 0%, greedy 88% / 56% / 28% on jams 1 / 2 / 3, greedy plus night shift 100% / 75% / 56% with a growing disqualification rate; with nobody cheating, starting fame explains about 80-88% of the final votes and quality about 13%.
