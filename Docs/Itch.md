# itch.io page and hand-off checklist

Everything below is for **you** to do or paste; nothing here has been done on your behalf (no repository was created or pushed, nothing was uploaded or submitted).

## Page copy (paste into the itch.io project)

**Title:** Shift Happens
**Short description / tagline:** A game about winning a game jam by shifting the votes.

**Description:**

> When a jam is decided by public vote, it barely matters how good your game is: whoever is more famous gets the votes and wins anyway.
>
> Nine games enter. Yours is fine. The rival's is fine too. The rival has 1.2 million followers; you have a few. You have 31 days of voting to change that.
>
> Click through the follower network to flip voters one by one: every hub you win drags its followers along. Or hold **SHIFT** and work the night shift: sockpuppets, bots, bribes. Powerful, fragile and risky. The Mod Eye is watching, three strikes and you are disqualified, and the last one can be **blamed on your rival**, once, if all your strikes together took only a few fake votes. A big pile of them, over several strikes, is too much to pin on anyone.
>
> Three jams, a build screen that asks how to spend your last 12 hours, and a results screen that computes, from your own run, how much of the vote was explained by quality and how much by fame.
>
> **Controls:** mouse = Plug (hover for details) · hold SHIFT (or N) = night shift · B = shift the blame (when offered) · Space/Esc = pause · M = mute · R = retry
>
> Windows 10/11 with a DirectX 12 GPU (Shader Model 6). Made with Unreal Engine 5.8 for GitHub Game Off 2026 (theme: SHIFT).

**Tags to consider:** Game Off 2026, strategy, satire, simulation, politics, hacking, 2D, singleplayer, Unreal Engine
**Genre:** Strategy · **Platform:** Windows · **Price:** free

**AI-use disclosure (the jam asks at submission):**

> Yes. This game was developed with AI assistance: the code, the in-game text, the MetaSound graphs that synthesise the audio and the build scripts were written by an AI model (Claude, via Claude Code) under my direction and review. No generative image, audio or video models were used for assets: graphics are procedural shaders and all audio is synthesised in MetaSound graphs (no samples, no .wav). See the README for details.

(Adjust the wording to what you want to say. The README has the same disclosure.)

## Screenshots

`Docs/Screenshots/` holds headless captures of the main screens (1920 x 1080). Re-create them with `Tools\run.ps1 -Shot ...` (see `Docs/QA.md`). itch.io wants at least 3; the GIF of the SHIFT key flipping day to night is made (see Trailer and GIF below).

## Trailer and GIF

The trailer is a scripted run of the game itself (`Data/Trailer.csv`, played by a director that is in every build but the Shipping one; see `Docs/QA.md`), recorded with OBS, with the game's own MetaSound music as the soundtrack. The files are in `Saved/Trailer/Dist/` (git-ignored; copy them where you want them):

* `trailer45.mp4`: the 45 s trailer (an epic-trailer parody: "Every game jam has a winner."), 1080p60, -14 LUFS.
* `trailer45_poster.png`: a poster frame (the end card).
* `shift_flip.gif`: the looping GIF of the SHIFT key flipping the title day to night (about 2 MB, for the itch page).

itch.io's trailer field takes a **YouTube or Vimeo link**, not a file: upload `trailer45.mp4` there as unlisted or public and paste the link; put the GIF and the poster in the page's screenshots. The GitHub Game Off page can take the same link.

## What is left for you

1. **GitHub**: the jam needs a public repository created during the jam. `gh` is logged in as the account used in this session, but I did **not** create or push anything. Typical steps: `git lfs install`, `git lfs track "*.uasset" "*.umap" "*.png"` (there is no `.gitattributes` yet; there are no `.wav` files, and `*.wav` is git-ignored), commit, create the public repo, push (`Saved/`, `Intermediate/`, `Binaries/` are ignored).
2. **License**: none chosen yet (the README says TBD). Pick one before you publish.
3. **Package** a Windows build yourself. The standard command (I only compiled the Shipping target and ran a headless cook as a pre-flight; I did not package):

   ```powershell
   & "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="D:\GameOff111609\ShiftHappens.uproject" -noP4 -platform=Win64 -clientconfig=Shipping -cook -build -stage -pak -archive -archivedirectory="D:\GameOff111609\Saved\Package" -prereqs -utf8output
   ```
   (`Saved/` is git-ignored.) Zip the staged `Windows` folder and upload it as the Windows download. The packaged game needs the DX12 runtime that ships with Windows 10/11.
4. **itch.io**: create the project, upload the zip, add the page copy and screenshots above, then **submit it to the jam** (pushing code is not submitting) before **Dec 1 2026, 13:37 PST (21:37 UTC)**.
5. **Listen to it**: the audio was verified with in-graph meters only (levels, spectra, clock, every lane firing), not by ear. All of it is MetaSound and Harmonix, so it is tweakable in the MetaSound editor, and the levels can be re-trimmed with `Tools\calibrate_audio.ps1`.

## Known limits

* DX12 / Shader Model 6 only (older GPUs will not run it).
* Windows only (no browser export in Unreal Engine 5).
* The pre-flight cook and Shipping compile passed, but a packaged build has not been run.
