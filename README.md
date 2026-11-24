# SHIFT HAPPENS

*A game about winning a game jam by shifting the votes.*

An entry for **GitHub Game Off 2026** (theme: **SHIFT**), made with **Unreal Engine 5.8**.

When a jam is decided by public vote, it barely matters how good your game is: whoever is more famous gets the votes and wins anyway.
Nine games enter. Yours is fine. The rival's is fine too. The rival has 1.2 million followers; you have a few. You have 31 days of voting to change that.

> **Platform:** Windows 10/11 with a DirectX 12 capable GPU (Shader Model 6). The game is DX12-only. (Unreal Engine 5 has no browser export, so this is a Windows download.)
> **License:** not decided yet (TBD).

## How to play

**New here? Press *Learn to play* on the title screen.** Two short guided lessons on a small practice board (about three minutes each): the voter to use is spotlighted from the moment a card appears (a card with nothing to do waits for Continue and freezes the board while you read), and a step is done only when you really do it (plug, reinforce, open a gate, parry, Rally; then the night shift, a sockpuppet, backing it, the Mod Eye). Nothing in a lesson counts for your ranks or recordings, and you can leave at any time. The one-page *How to play* reference is on the title screen, in the pause menu and on **H**.

The board is the jam's follower network: every face is a voter, every line is "listens to". Each voter backs one of the nine entries (colour = entry).
Hubs and celebrities carry most of the vote weight. Flip them and their followers follow.

| Input | What it does |
|---|---|
| **Hover** a voter | Tooltip: handle, followers, vote weight, who they back and how sure they are, why you can or cannot flip them, and every action you can take on it with its key |
| **Click** | **Plug** (day shift): spend 1 Post to chip away at a voter's conviction (the ring). At zero they flip to you. Click your own voters to reinforce them, and click a voter a ring is closing in on to **parry** the attack |
| **Right-click** a voter you hold | **Rally**: spend 1 Post to shield it and your voters one hop away from every plug for 6 s (teal dome) and add a little conviction (8 s cooldown) |
| **Hold SHIFT** (or press **N** to toggle) | **Night shift**: the world goes dark. Clicks become **sockpuppets**: 3x stronger, ignore gates, but fragile, and they build **Heat** |
| **B** | **Shift the Blame**: the strike that would disqualify you can be handed to your rival, once per jam, if the fake votes of all your strikes together were few (under 20% of the vote). It is offered for 5 s as an orange bar in the middle of the bottom bar: click it or press B |
| **Space / Esc** | Pause and menu |
| **H** | The *How to play* reference page |
| **M** | Mute: effects and music both off, or both back on |
| **Gear button** (top right) | Settings menu: **Effects** on / off, **Music** on / off, and the window mode (Fullscreen, Borderless = a window the size of the desktop, Windowed). Esc or a click outside closes it. The choices are saved; the game starts in Fullscreen. **Alt+Enter** / **F11** also toggle fullscreen. A red dot on the gear means some sound is off |
| **Cursor** | The game draws its own cursor: an arrow, a hand over buttons, a teal target over a voter you can flip, a "+" over one of yours, a visor over a voter at night, and a slashed circle over one you cannot act on (immune, gated, overheated). A ring marks each click: teal for a plug, violet for a sockpuppet, amber for a Rally |
| **E** | Relation lines: faint (default) / off / full |
| **L** | Leaderboard: compact (default) / full / hidden |
| **R** | Retry (when paused or on the results screen) |
| **O** | Results screen: open the folder with the recording of the jam you just played |
| **A** / **C** (title) | The **Achievements** page (14 achievements, a career strip per jam) and the **Credits** |

* **Posts** are your energy and come back over time. Your build sets the bank (3 to 7, 5 at an even split), how fast they refill, how far each Plug goes and how many voters you start with: the build screen and the pause menu list the numbers.
* **Gates**: a famous voter ignores you until enough of their own followers already back you (a padlock ring shows progress). Night shift skips gates. The rule binds the rival and the other entries too: a hub or a celebrity whose followers have come around to you is out of their reach (hover it to see).
* **Heat and the Mod Eye**: each sockpuppet adds 22 Heat and holding SHIFT adds 4 a second; Heat only cools when you let go. The Mod Eye sweeps every 12 to 35 seconds (the countdown is always on screen); if you are above the red tick when it passes, or Heat hits the cap, you get a **strike**: your sockpuppets are reverted and you are shadowbanned for a few seconds. Three strikes and you are disqualified, and strikes do not wear off: **Shift the Blame** is only a last chance at the third one, and only for a small pile of fake votes (a big one is "too much to blame on the rival" and ends the run on the spot, with the reason on screen). A sockpuppet that would take Heat to the cap is refused instead.
* **Sockpuppets are fake until backed**: plug your own sockpuppet (reinforce it three times inside its 3 s immunity) and it becomes a real vote. At the end of the jam a **final audit** purges every sockpuppet vote that is still fake, and every sockpuppet vote bought in the last 15 seconds even if you backed it, so night shift is an opening move, not a last-second trick. **Holding fake votes heats you up** (the **SOCKPUPPET VOTES** card in the right-hand strip shows your fake share against what your cooling can hold, red above it, and explains itself when you hover it), and a strike undoes every sockpuppet vote, backed or not, so cheating is a gamble: careful play gains a little on the hardest jam and costs you a lot of disqualifications.
* **Voters drift back** to where their neighbours are. A voter that shakes is about to flip back, and a line in the pushing entry's colour runs from each neighbour pushing it: reinforce it or win those neighbours.
* **Attacks are announced.** When the rival or another entry picks one of your voters, a ring in *their* colour closes in on it for 1.5 seconds before the plug lands. Click the voter to **parry** it (the ring bounces, 1 Post), or Rally it and its neighbours (the ring bounces off the shield), or let it go. Your fans are loyal (a voter you flip starts at 80% conviction). Sockpuppet votes are flagged and get no warning. The rival does not hammer one voter: it leaves a voter it just attacked alone for 30 seconds, so its attacks spread over many voters.
* **Spotlights**: three times a jam, in the middle of it, a streamer looks at one voter (a card says who, the voter pulses). Flip or back that voter within 9 seconds and you get a Post back. Only a person can take it; bots never do.
* **Chatter**: voters, mods and bots comment in the notification strip as things happen (a gate opens, a strike lands, a sweep starts, an algorithm boost, a streamer, drama, a long quiet). It is only flavour.
* **Accessibility** (gear button): *Shapes beside colours* puts a distinct shape next to each entry's colour on every voter and in the leaderboard and results lists, *Large text* sets a floor for the text size, *Calm* stops the pulsing. All three are saved.
* **Front page**: the top three entries get a boost, so leads snowball.
* **Three celebrities**, each with an audience of its own (a voter follows at most one) and each owned by a different entry at the start: the rival's, and two other colours'. Taking one opens nothing at the gate of the next. A big account does not mind a small one's posts: your Plugs take a tenth less off a celebrity's conviction than off anyone else's, so a fully convinced circle takes three Plugs once its gate is open, not two.
* **Grudges, not hunting**: the rival and the other entries go after your voters only for 30 seconds after you took one of theirs, and then they go for that very voter, the rival first of all (a ring in their colour closes in on it as soon as the voter is no longer immune), and an other colour's ring is hard enough to take even a fully convinced voter in one plug, so parry it or lose the voter (the circle you just took, say); otherwise yours are like anyone's. They also hold their own shaky big voters.
* **The pack**: the other entries act every 8 seconds, go for voters up to a hub's size and pile onto whoever leads the board (you too, when you lead: the front-runner draws everyone's rings, so a lead has to be held); they can win a jam and can take a hub from the front-runner under the same gates as everyone; a note says who took what. It keeps a runaway leader from running away, and makes leading cost you rings in every colour.
* **Underdogs rally**: when one entry leads the runner-up by more than 25 points of the vote, voters start rooting for the underdog: the leader's voters drift towards the runner-up, harder the bigger the lead, and a parry does not stop it (reinforcing and Rallying slow it). A lead cannot snowball without limit any more: the winner ends around half of the vote instead of three quarters, and a chip under the leaderboard says when it is on. The small +3 / -2 beside a share in the leaderboard is that entry's change over the last 20 seconds.
* **The rival's final push**: in the last three days the rival attacks twice as often, but it pays Posts like you (a bank of 3, refilling 0.3 a second, shown on the push card): the burst runs the bank dry and it falls back to its normal pace. A countdown announces it, and your Posts refill 10% faster for the same days.
* **Nothing floats over the board**: the voters keep left of a right-hand strip that holds the leaderboard, the states you are in (shadowban, final push, final audit, fake votes) and the notification cards. What cannot be missed takes over the middle of the bottom bar instead, where the eyes already are for Posts, Heat and strikes (a strike, the Shift the Blame offer, the disqualification, the final push and audit), with a thin frame at the edge of the screen and a warning sound.

Flow: **Title** → **Pick a jam** (Tiny Jam #7, 48h Pixel Rumble, GAME OFF 2026) → **Build your entry** (spend 12 hours on Quality, Thumbnail and Network, with a forecast of this very jam) → **Vote** (31 days) → **Results**
(the results screen computes, from your own run, how much of the vote is explained by quality and how much by starting fame).

**Recordings.** Every jam you play is recorded: a small text file (`Saved/Recordings/shift_jam<n>_<seed>_<time>_<result>.shiftrec.txt`, a few kilobytes) with the jam, its seed, and every click, Rally and night shift with the moment it was made, plus how the shares went. The game is deterministic, so that is enough to play the jam back exactly; the results screen names the file and **O** (or the *Files* button) opens its folder. If you are a playtester, send the files: they show how you really play, which is what the balance is tuned from. `-ShiftReplay=<file>` plays one back in the game and `Tools\replay.ps1` replays and reports them headlessly.

## Technology

* **Unreal Engine 5.8.4**, C++ module `ShiftHappens`, DirectX 12 / SM6 only, Substrate materials.
* **Simulation**: an engine-independent, deterministic C++17 core (`Source/ShiftHappens/Sim`, 20 Hz fixed step, own PRNG). The thesis emerges from social-proof dynamics on a follower graph; nothing is scripted.
* **Behaviour**: the rival, the other entries, the Mod Eye and the ambient events (algorithm boosts, streamers, drama) are **StateTree** assets run through a custom schema and ticked by hand from the fixed step, with seeded randomness, so runs reproduce exactly. The rival's gates read **Gameplay Tags** (stock `Has Tags` condition fed by a small evaluator).
* **Rendering**: the board is world-space 2D in a real level (orthographic camera): instanced quads with SDF materials (HLSL custom nodes into Substrate unlit BSDFs), bloom, one `MaterialParameterCollection` for the day/night look.
* **Interface**: one custom-painted **Slate** widget (title, jam select, build, HUD, results). No UMG.
* **Audio**: all **MetaSound** and **Harmonix**, no `.wav` and no sampled or AI-generated sound. The music is one MetaSound source clocked by a Harmonix Metronome (day / night / tension layers crossfaded by float parameters); each of the 26 effects is a MetaSound one-shot built from oscillators, noise, envelopes and filters. Every audio asset is a single composite node (C++ that wires the engine's own and Harmonix nodes). See `Docs/Audio.md`.
* **Content**: text and tuning are **Data Tables** imported from `Data/*.csv`. Each jam is a **Game Feature plugin** (`Plugins/GameFeatures/ShiftJam_*`) carrying its own entries; the jam select screen lists the active packs.
* **Assets are generated**: materials, the level, data tables, MetaSounds, StateTrees and jam packs are produced by scripts (`Tools/py`, run headless), so the repository's sources are the CSVs, the HLSL, the Python and the C++.

## Building and running

Requirements: Unreal Engine 5.8 (default install path `C:\Program Files\Epic Games\UE_5.8`, or set `SHIFT_UE_ROOT`), Visual Studio 2022 with the *Game development with C++* workload.

```powershell
.\Tools\build.ps1                              # build the editor target
.\Tools\run.ps1 -Visible -Sound                # play it in a window
```

Regenerating the generated assets (all headless, each logs to `Saved/py_<script>.log`):

```powershell
.\Tools\run_py.ps1 -Script Tools\py\build_materials.py      # materials + theme collection
.\Tools\run_py.ps1 -Script Tools\py\build_level.py          # the level
.\Tools\run_py.ps1 -Script Tools\py\build_data.py           # data tables from Data\*.csv
.\Tools\build_audio.ps1                                     # the 27 one-node MetaSound assets (Python builder API)
.\Tools\calibrate_audio.ps1                                 # optional: re-measure and re-trim levels with the QA taps
.\Tools\run_py.ps1 -Script Tools\py\build_statetrees.py     # the behaviour StateTrees
.\Tools\run_py.ps1 -Script Tools\py\build_jam_packs.py       # the jam packs (Game Feature plugins, via the PluginToolset)
```

Testing and balance (also headless; see `Docs/QA.md`):

```powershell
.\Tools\run.ps1 -Shot D:\shots\title.png -Quit -Extra @('-ShiftScreen=title')   # screenshot without opening a window
.\Tools\balance.ps1 -Runs 200                                                 # win/DQ rates per jam and scripted player
.\Tools\forecast_check.ps1                                                    # the build screen's forecast: monotone in the levers, no jumps, the same every time
.\Tools\replay.ps1 -Dir D:\from_the_tester                                    # replay recorded jams: result, hash, clicks and shares of each
.\Tools\verify.ps1                                                             # one command: audio assets and meters, StateTree activity, determinism, game boot
```

## Layout

```
Source/ShiftHappens/   Sim/ (numeric core)  Behavior/ (policies, StateTree nodes)  Board/  UI/ (Slate)  Audio/  Game/ (controller, run, content, build rules)  Jams/  Data/
Content/Shift/         generated assets
Plugins/GameFeatures/  the three jam packs
Data/                  CSV sources of truth (text, tuning, jams)
Tools/                 build / run / balance / verify scripts, Python builders (py/), standalone sim harness
Docs/                  Audio.md, QA.md, Itch.md, mcp-toolsets.md, metasound-nodes-5.8.md
```

## AI disclosure

This game was developed with AI assistance. The code, the data text (jokes, headlines, blurbs), the MetaSound graphs that synthesise the music and sound effects, and the build scripts were written
by an AI model (Claude, through Claude Code) under the direction of the project owner, who reviewed the results. No generative image, audio or video models were used for the assets: all graphics are procedural (shaders) and all audio is synthesised in MetaSound graphs from code.
The names, handles and "entries" in the game are fictional.

## Credits

* Engine: Unreal Engine 5.8 (Epic Games). The HUD uses the engine's Roboto font (Apache License 2.0).
* Project owner: Huân Riêu. Tester: Phuong-Fang.
* Design, code and content: the project owner with Claude (Anthropic).
