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

## Images (every slot of the itch.io editor)

`Tools\itch\build.ps1` makes all of them, headless (about 8 minutes, mostly editor starts): the scenes are scripted runs of the game (cue files in `Tools/itch/cues/`, the same director as the trailer) shot frame by frame at a fixed 60 fps time step, with no window, no sound, no OBS and no injected input. The files land in `Saved/Itch/upload/` (git-ignored: copy them out), the sizes and checks in `Saved/Itch/report.md`. `-SkipShoot` re-makes from the frames it already has, `-MaxMB n` re-encodes with a lower cap. The background needs no frames: `python Tools\itch\make_images.py bg` makes it alone (about a minute). Every picture is the game's own rendering plus typography drawn in code (Roboto, the HUD font), so no generative image model is involved, as the AI disclosure above says.

GIF wherever the slot may take one; the PNG beside each is the fallback if the uploader refuses a GIF there (frame 1 of the GIF equals it, so a social crawler that shows the first frame shows the finished picture).

| itch.io slot | file | notes |
|---|---|---|
| Cover image | `01_cover_630x500.gif` (+ `.png`) | 315:250, the recommended 630x500; a slow day/night wipe, the keycap of the logo presses on the night side; the name is on two lines (keycap and SHIFT, then HAPPENS) to fill the near-square frame |
| Screenshot 1 | `02_screenshot_shift_flip.gif` | title screen, SHIFT held: day flips to night and back |
| Screenshot 2 | `03_screenshot_plug.gif` | hover card, click, the voter flips, the leaderboard moves |
| Screenshot 3 | `04_screenshot_night_shift.gif` | hold SHIFT: four sockpuppets on a celebrity, Heat rises |
| Screenshot 4 | `05_screenshot_mod_eye.gif` | the Mod Eye countdown, a strike, disqualification |
| Screenshot 5 | `06_screenshot_results.png` | the results card (a still: nothing on it moves) |
| Promotional: Social media image | `07_social_1200x630.gif` (+ `.png`) | 1.91:1, the logo with one caption line under it, "Winning game jams by shifting votes": white Roboto Bold with an ink outline like the logo's letters, no box, narrower than the logo so the logo stays the focus |
| Promotional: Favicon | `08_favicon_512.png` | square, transparent: the shift arrow alone, teal with a thick outline split in paper and night on the icon's slanted line, filling the canvas so it stays legible in a 16 px browser tab (checked at 128, 64, 32 and 16 px on light and dark tabs). PNG on purpose (GIF transparency is 1-bit, jagged at 16 px). Not the exe icon: `08_app_icon.ico` is the voter's disc and conviction ring with the arrow on the day / night split (16 to 256 px); a copy is in `Build/Windows/Application.ico`, which Unreal Engine uses as the game's icon at the next build or package |
| Promotional: Wide cover | `09_wide_cover_2100x900.gif` (+ `.png`) | exact 21:9 |
| Promotional: Logo | `10_logo_1600x220.png` | transparent PNG, horizontal; teal and white letters with a navy outline so it reads on white and on black |
| Theme editor: Banner | `12_banner_1200x300.gif` (+ `.png`) | replaces the title above the description, so it carries the logo |
| Theme editor: Background | `13_background_2160x2160.gif` (+ `.png`) | a **square** tile: the follower network on the night board's dot grid: voters with the game's faces, votes running along the edges, a ring when one lands. Flat colours, solid 2 px lines, drawn at 3x and filtered down. A 6 s loop at 10 fps that loops without a jump and tiles without a seam on both axes (the theme editor repeats the image, as the design docs show). The PNG is frame 1 and the fallback; the uploader refuses anything over 3840 x 2160, so 2160 is the largest square; the build drops to 1440 px only if the size cap forces it |
| Devlog post: Cover image | `14_devlog_cover_1280x720.gif` | 16:9, minimum width 500 px per the devlog editor; the trailer thumbnail (logo and caption) with the slow day/night wipe. Its fallback is `11_trailer_thumbnail_1280x720.png`, which equals frame 1 |
| Trailer | `Saved/Trailer/Dist/trailer45.mp4` | the slot takes a **YouTube or Vimeo link**, not a file: upload the MP4 there (public or unlisted, and check it logged out) and paste the link; `11_trailer_thumbnail_1280x720.png` is a thumbnail for it (the same logo and tagline plate as the social image) |

Theme editor colours that match (type them in): BG `#0A0D1C`, BG2 `#0E1226` (BG2 alpha about 85%), Text `#EDEDFF`, Link `#1FB6A6`, Buttons `#1FB6A6`, Headers `#F2B134`. Use "Header 2" for the description's section titles.

What itch.io does **not** state (the docs and FAQ name no GIF or file-size limit, no dimensions for the promotional slots and no accepted formats for the social and wide-cover slots): the 3 MB cap and the sizes above are cautious choices. If an upload is refused, use the PNG next to the GIF or run `Tools\itch\build.ps1 -SkipShoot -MaxMB 2`. The covers and banner are slow wipes and the background is small dots moving along lines, so nothing flashes (`make_images.py check` counts flashes per second against the WCAG limit of 3, which the itch.io quality guidelines cite for animated covers, and also tests that the background loops and tiles without a seam). `Docs/Screenshots/` is from 16 Nov (an older UI): do not upload those.

Before the first **public** publish (it fixes the page's place in "Most Recent"): every image above, the Windows file and the classifications in place; Visibility "Public" and not unlisted; the cover is the only image indexing requires; Platforms: Windows only; the "AI Disclosure" section filled in (the guidelines count AI-written text, and this game's text and code were written with AI); tags relevant and preferably from the suggestions (do not repeat what the metadata page already classifies). "Shift Happens" is a common phrase and search favours exact titles, so the logo and a distinctive tag set matter.

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
