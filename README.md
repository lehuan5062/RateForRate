# SHIFT HAPPENS

*A game about winning a game jam by shifting the votes.*

An entry for **GitHub Game Off 2026** (theme: **SHIFT**), made with **Unreal Engine 5.8**.

When a jam is decided by public vote, it barely matters how good your game is: whoever is more famous gets the votes and wins anyway.
Nine games enter. Yours is fine. The rival's is fine too. The rival has 1.2 million followers; you have a few. You have 31 days of voting to change that.

> **Platform:** Windows 10/11 with a DirectX 12 capable GPU (Shader Model 6). The game is DX12-only. (Unreal Engine 5 has no browser export, so this is a Windows download.)
> **License:** not decided yet (TBD).

## How to play

The board is the jam's follower network: every face is a voter, every line is "listens to". Each voter backs one of the nine entries (colour = entry).
Hubs and celebrities carry most of the vote weight. Flip them and their followers follow.

| Input | What it does |
|---|---|
| **Hover** a voter | Tooltip: handle, followers, vote weight, who they back and how sure they are, and why you can or cannot flip them |
| **Click** | **Plug** (day shift): spend 1 Post to chip away at a voter's conviction (the ring). At zero they flip to you. Click your own voters to reinforce them, and click a voter a ring is closing in on to **parry** the attack |
| **Right-click** a voter you hold | **Rally**: spend 1 Post to shield it and your voters one hop away from every plug for 6 s (teal dome) and add a little conviction (8 s cooldown) |
| **Hold SHIFT** (or press **N** to toggle) | **Night shift**: the world goes dark. Clicks become **sockpuppets**: 3x stronger, ignore gates, but fragile, and they build **Heat** |
| **B** | **Shift the Blame**: the strike that would disqualify you can be handed to your rival, once per jam, if the fake votes it took were few (under 8% of the vote). It is offered for 4 s as an orange bar across the top of the screen: click it or press B |
| **Space / Esc** | Pause and menu |
| **M** | Mute |
| **E** | Relation lines: faint (default) / off / full |
| **L** | Leaderboard: compact (default) / full / hidden |
| **R** | Retry (when paused or on the results screen) |

* **Posts** are your energy and come back over time. Your build sets the bank (3 to 7, 5 at an even split), how fast they refill, how far each Plug goes and how many voters you start with: the build screen and the pause menu list the numbers.
* **Gates**: a famous voter ignores you until enough of their own followers already back you (a padlock ring shows progress). Night shift skips gates.
* **Heat and the Mod Eye**: each sockpuppet adds 22 Heat and holding SHIFT adds 4 a second; Heat only cools when you let go. The Mod Eye sweeps every 12 to 35 seconds (the countdown is always on screen); if you are above the red tick when it passes, or Heat hits the cap, you get a **strike**: your sockpuppets are reverted and you are shadowbanned for a few seconds. Three strikes and you are disqualified, and strikes do not wear off: **Shift the Blame** is only a last chance at the third one, and only for a small pile of fake votes (a big one is "too much to blame on the rival" and ends the run on the spot, with the reason on screen). A sockpuppet that would take Heat to the cap is refused instead.
* **Sockpuppets are fake until backed**: plug your own sockpuppet (reinforce it three times inside its 3 s immunity) and it becomes a real vote. At the end of the jam a **final audit** purges every sockpuppet vote that is still fake, and every sockpuppet vote bought in the last 15 seconds even if you backed it, so night shift is an opening move, not a last-second trick. **Holding fake votes heats you up** (the **SOCKPUPPET VOTES** card in the right-hand strip shows your fake share against what your cooling can hold, red above it, and explains itself when you hover it), and a strike undoes every sockpuppet vote, backed or not, so cheating is a gamble: careful play gains a little on the hardest jam and costs you a lot of disqualifications.
* **Voters drift back** to where their neighbours are. A voter that shakes is about to flip back, and a line in the pushing entry's colour runs from each neighbour pushing it: reinforce it or win those neighbours.
* **Attacks are announced.** When the rival or another entry picks one of your voters, a ring in *their* colour closes in on it for 1.5 seconds before the plug lands. Click the voter to **parry** it (the ring bounces, 1 Post), or Rally it and its neighbours (the ring bounces off the shield), or let it go. Your fans are loyal (a voter you flip starts at 80% conviction). Sockpuppet votes are flagged and get no warning.
* **Front page**: the top three entries get a boost, so leads snowball.
* **The rival's final push**: in the last three days the rival doubles its attacks. A countdown announces it, and your Posts refill 25% faster for the same days.
* **Nothing floats over the board**: the voters keep left of a right-hand strip that holds the leaderboard, the states you are in (shadowban, final push, final audit, fake votes) and the notification cards. What cannot be missed takes over the middle of the top bar instead (a strike, the Shift the Blame offer, the disqualification, the final push and audit), with a thin frame at the edge of the screen and a warning sound.

Flow: **Title** → **Pick a jam** (Tiny Jam #7, 48h Pixel Rumble, GAME OFF 2026) → **Build your entry** (spend 12 hours on Quality, Thumbnail and Network, with a forecast of this very jam) → **Vote** (31 days) → **Results**
(the results screen computes, from your own run, how much of the vote is explained by quality and how much by starting fame).

## Technology

* **Unreal Engine 5.8.4**, C++ module `ShiftHappens`, DirectX 12 / SM6 only, Substrate materials.
* **Simulation**: an engine-independent, deterministic C++17 core (`Source/ShiftHappens/Sim`, 20 Hz fixed step, own PRNG). The thesis emerges from social-proof dynamics on a follower graph; nothing is scripted.
* **Behaviour**: the rival, the other entries, the Mod Eye and the ambient events (algorithm boosts, streamers, drama) are **StateTree** assets run through a custom schema and ticked by hand from the fixed step, with seeded randomness, so runs reproduce exactly. The rival's gates read **Gameplay Tags** (stock `Has Tags` condition fed by a small evaluator).
* **Rendering**: the board is world-space 2D in a real level (orthographic camera): instanced quads with SDF materials (HLSL custom nodes into Substrate unlit BSDFs), bloom, one `MaterialParameterCollection` for the day/night look.
* **Interface**: one custom-painted **Slate** widget (title, jam select, build, HUD, results). No UMG.
* **Audio**: all **MetaSound** and **Harmonix**, no `.wav` and no sampled or AI-generated sound. The music is one MetaSound source clocked by a Harmonix Metronome (day / night / tension layers crossfaded by float parameters); each of the 26 effects is a MetaSound one-shot built from oscillators, noise, envelopes and filters, and the reusable instrument voices are MetaSound patches. See `Docs/Audio.md`.
* **Content**: text and tuning are **Data Tables** imported from `Data/*.csv`. Each jam is a **Game Feature plugin** (`Plugins/GameFeatures/ShiftJam_*`) carrying its own entries; the jam select screen lists the active packs.
* **Assets are generated**: materials, the level, data tables, MetaSounds, StateTrees and jam packs are produced by scripts (`Tools/py`, run headless), so the repository's sources are the CSVs, the HLSL, the Python and the C++.

## Building and running

Requirements: Unreal Engine 5.8 (default install path `C:\Program Files\Epic Games\UE_5.8`, or set `SHIFT_UE_ROOT`), Visual Studio 2022 with the *Game development with C++* workload, Git LFS.

```powershell
.\Tools\build.ps1                              # build the editor target
.\Tools\run.ps1 -Visible -Sound                # play it in a window
```

Regenerating the generated assets (all headless, each logs to `Saved/py_<script>.log`):

```powershell
.\Tools\run_py.ps1 -Script Tools\py\build_materials.py      # materials + theme collection
.\Tools\run_py.ps1 -Script Tools\py\build_level.py          # the level
.\Tools\run_py.ps1 -Script Tools\py\build_data.py           # data tables from Data\*.csv
.\Tools\build_audio.ps1                                     # all MetaSound patches, sound effects and the music (Python builder API)
.\Tools\calibrate_audio.ps1                                 # optional: re-measure and re-trim levels with the in-graph meters
.\Tools\run_py.ps1 -Script Tools\py\build_statetrees.py     # the behaviour StateTrees
.\Tools\run_py.ps1 -Script Tools\py\build_jam_packs.py       # the jam packs (Game Feature plugins, via the PluginToolset)
```

Testing and balance (also headless; see `Docs/QA.md`):

```powershell
.\Tools\run.ps1 -Shot D:\shots\title.png -Quit -Extra @('-ShiftScreen=title')   # screenshot without opening a window
.\Tools\balance.ps1 -Runs 200                                                 # win/DQ rates per jam and scripted player
.\Tools\forecast_check.ps1                                                    # the build screen's forecast: monotone in the levers, no jumps, the same every time
.\Tools\verify.ps1                                                             # one command: audio assets and meters, StateTree activity, determinism, game boot
```

## Layout

```
Source/ShiftHappens/   Sim/ (numeric core)  Behavior/ (policies, StateTree nodes)  Board/  UI/ (Slate)  Audio/  Game/ (controller, run, content, build rules)  Jams/  Data/
Content/Shift/         generated assets (Git LFS)
Plugins/GameFeatures/  the three jam packs
Data/                  CSV sources of truth (text, tuning, jams)
Tools/                 build / run / balance / verify scripts, Python builders (py/), standalone sim harness
Docs/                  Decisions.md (design, engineering decisions, claim register), Audio.md, QA.md, Itch.md, mcp-toolsets.md, metasound-nodes-5.8.md
```

## AI disclosure

This game was developed with AI assistance. The code, the data text (jokes, headlines, blurbs), the MetaSound graphs that synthesise the music and sound effects, and the build scripts were written
by an AI model (Claude, through Claude Code) under the direction of the project owner, who reviewed the results. No generative image, audio or video models were used for the assets: all graphics are procedural (shaders) and all audio is synthesised in MetaSound graphs from code.
The names, handles and "entries" in the game are fictional.

## Credits

* Engine: Unreal Engine 5.8 (Epic Games). The HUD uses the engine's Roboto font (Apache License 2.0).
* Design, code and content: the project owner with Claude (Anthropic).
