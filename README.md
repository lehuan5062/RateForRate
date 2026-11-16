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
| **Click** | **Plug** (day shift): spend 1 Post to chip away at a voter's conviction (the ring). At zero they flip to you. Click your own voters to reinforce them |
| **Hold SHIFT** (or press **N** to toggle) | **Night shift**: the world goes dark. Clicks become **sockpuppets**: 3x stronger, ignore gates, but fragile, and they build **Heat** |
| **B** | **Shift the Blame**: right after a strike, once per jam, hand the strike to your rival |
| **Space / Esc** | Pause and menu |
| **M** | Mute |
| **R** | Retry (when paused or on the results screen) |

* **Posts** are your energy (max 5) and come back over time.
* **Gates**: a famous voter ignores you until enough of their own followers already back you (a padlock ring shows progress). Night shift skips gates.
* **Heat and the Mod Eye**: sockpuppets raise Heat. The Mod Eye sweeps now and then (telegraphed); if you are in the red zone when it passes, or Heat hits the cap, you get a **strike**: your sockpuppets are reverted and you are shadowbanned for a few seconds. Three strikes and you are disqualified.
* **Front page**: the top three entries get a boost, so leads snowball.

Flow: **Title** → **Pick a jam** (Tiny Jam #7, 48h Pixel Rumble, GAME OFF 2026) → **Build your entry** (spend 12 hours on Quality, Thumbnail and Network, with a live forecast) → **Vote** (31 days) → **Results**
(the results screen computes, from your own run, how much of the vote is explained by quality and how much by starting fame).

## Technology

* **Unreal Engine 5.8.4**, C++ module `ShiftHappens`, DirectX 12 / SM6 only, Substrate materials.
* **Simulation**: an engine-independent, deterministic C++17 core (`Source/ShiftHappens/Sim`, 20 Hz fixed step, own PRNG). The thesis emerges from social-proof dynamics on a follower graph; nothing is scripted.
* **Behaviour**: the rival, the other entries, the Mod Eye and the ambient events (algorithm boosts, streamers, drama) are **StateTree** assets run through a custom schema and ticked by hand from the fixed step, with seeded randomness, so runs reproduce exactly.
* **Rendering**: the board is world-space 2D in a real level (orthographic camera): instanced quads with SDF materials (HLSL custom nodes into Substrate unlit BSDFs), bloom, one `MaterialParameterCollection` for the day/night look.
* **Interface**: one custom-painted **Slate** widget (title, jam select, build, HUD, results). No UMG.
* **Audio**: **MetaSound** sources. The music stems and every sound effect are synthesised offline by a small C++ tool (`Tools/AudioRender`, no samples, no audio models) and mixed in MetaSounds (day/night crossfade, tension layer).
* **Content**: text and tuning are **Data Tables** imported from `Data/*.csv`. Each jam is a **Game Feature plugin** (`Plugins/GameFeatures/ShiftJam_*`) carrying its own entries; the jam select screen lists the active packs.
* **Assets are generated**: materials, the level, data tables, audio sources, StateTrees and jam packs are produced by scripts (`Tools/py`, run headless), so the repository's sources are the CSVs, the HLSL, the Python and the C++.

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
.\Tools\build_audio.ps1                                     # render the music and sound effects to Audio\ (C++ tool)
.\Tools\run_py.ps1 -Script Tools\py\build_audio.py          # import them and build the MetaSounds
.\Tools\run_py.ps1 -Script Tools\py\build_statetrees.py     # the behaviour StateTrees
python Tools\make_jam_plugins.py; .\Tools\run_py.ps1 -Script Tools\py\build_jam_packs.py   # the jam packs
```

Testing and balance (also headless; see `Docs/QA.md`):

```powershell
.\Tools\run.ps1 -Shot D:\shots\title.png -Quit -Extra @('-ShiftScreen=title')   # screenshot without opening a window
.\Tools\balance.ps1 -Runs 200                                                 # win/DQ rates per jam and scripted player
```

## Layout

```
Source/ShiftHappens/   Sim/ (numeric core)  Behavior/ (policies, StateTree nodes)  Board/  UI/ (Slate)  Audio/  Game/ (controller, run, content, build rules)  Jams/  Data/
Content/Shift/         generated assets (Git LFS)
Plugins/GameFeatures/  the three jam packs
Data/                  CSV sources of truth (text, tuning, jams)
Tools/                 build / run / balance scripts, Python builders (py/), audio renderer, standalone sim harness
Docs/                  Decisions.md (design and engineering decisions), QA.md, Itch.md, mcp-toolsets.md
Audio/                 rendered WAVs (generated by Tools/AudioRender)
```

## AI disclosure

This game was developed with AI assistance. The code, the data text (jokes, headlines, blurbs), the synthesis recipes that produce the music and sound effects, and the build scripts were written
by an AI model (Claude, through Claude Code) under the direction of the project owner, who reviewed the results. No generative image, audio or video models were used for the assets: all graphics are procedural (shaders) and all audio is synthesised from code.
The names, handles and "entries" in the game are fictional.

## Credits

* Engine: Unreal Engine 5.8 (Epic Games). The HUD uses the engine's Roboto font (Apache License 2.0).
* Design, code and content: the project owner with Claude (Anthropic).
