# Shift Happens: text Data Tables

All player-facing text lives here as CSV and is imported into Unreal Data Tables (`DT_*`) by the build scripts.
The CSVs are the source of truth: edit them, run the validator, re-import. This README covers the **text** tables.
`Jams.csv` and `Tuning.csv` are exports of the sim's C++ defaults for the standalone harness (the game itself reads the C++ defaults) and are documented separately.

Tone: dry, affectionate satire of game-jam culture. The joke is always on the system (fame, rate-for-rate, devlog
guilt), never on the player. All names are fictional. The jam's own name appears only as the final jam title
"GAME OFF 2026" (the `GameOff2026` id is just an identifier).

## Conventions (every file)

- UTF-8, **pure ASCII text**, no BOM, LF line endings, first row is the header, file ends with a newline.
  The UI font is Roboto, so no emoji and no star or arrow symbols. Write "stars" as a word, use `...` and `-`.
- Comma separated. Any field containing a comma or a double quote is quoted (`"like, this"`). Avoid double quotes
  inside text; use single quotes for quoted speech.
- The first column is always `Id`: unique, starts with a letter, letters/digits/underscore only. For Data Table
  import it becomes the **row name**; the other headers are the struct field names.
- No empty fields, no leading/trailing spaces, no double spaces. No duplicate or near-duplicate lines in a file.
- No URLs, no real people, brands, companies or streamers.
- Length limits are in characters and apply to the raw text (before placeholder substitution).
- Enum-like columns (`Kind`, `Sentiment`, `Context`, ...) use the exact spellings below, so they can be imported as
  `UENUM`s, `FName`s or strings. `Hidden` is `true` / `false`. `Step` is an integer.
- Placeholders are written `{name}` and substituted by plain string replace at runtime. A placeholder not listed for
  that file/event is a validation error. Files with no placeholder list never contain braces.

## Files and columns

### Handles.csv  (`Id,Handle,Kind`)  150 rows
Community member names, one per voter node. The rival's own handle (see Entries.csv) is not repeated here.

| Column | Meaning |
|---|---|
| Id | `H001`... |
| Handle | Display name, max 26 chars, `A-Z a-z 0-9 _ .` only, unique case-insensitively. Never collides with an entry Author. |
| Kind | `Lurker`, `Dev`, `Streamer`, `Bot`, `Mod`, `Celebrity` (at least 8 of each). |

Intended use: the graph builder draws a handle whose Kind fits the node's role (Celebrity for the hubs the rival
owns, Streamer for streamer gates, Mod for Mod-Eye flavour, Bot for sockpuppets and spam, Dev/Lurker for the rest).
`{handle}` in Headlines is drawn from this table (Kind per event below).

### Bios.csv  (`Id,Kind,Text`)  96 rows
One-line bios for the hover tooltip.

| Column | Meaning |
|---|---|
| Id | `B001`... |
| Kind | Same enum as Handles. Pick a bio whose Kind matches the node's handle Kind. |
| Text | Max **60** chars. No placeholders. |

### Comments.csv  (`Id,Sentiment,Context,Text`)  220 rows
Speech bubbles over nodes and lines in the feed. No placeholders.

| Column | Meaning |
|---|---|
| Id | `CM001`... |
| Sentiment | `Positive`, `Negative`, `Neutral`, `Suspicious`. Drives bubble tint / feed marker. Every Context mixes sentiments so the same event does not always read the same. |
| Context | Which game moment may show the line (table below). At least 8 lines per Context. |
| Text | Max **70** chars. No placeholders. |

| Context | When to use it | Speaker |
|---|---|---|
| FlipToYou | A voter flips to the player's entry. | The voter who flipped. |
| FlipToRival | A voter flips to the rival's entry (a rival attack, or your flip undone by herd pressure). | The voter. |
| FlipToOther | A voter flips to a third-party ("Other") entry. | The voter. |
| Dent | A Plug lowered a voter's conviction but did not flip them. | The voter hit. |
| Reinforce | A Plug on your own voter raised their conviction. | The voter. |
| GateOpen | A gated hub reaches its follower threshold and the lock opens to you. | The hub. |
| Sockpuppet | A SHIFT (night shift) flip landed. Bystanders react; mostly Suspicious. | Neighbours, feed. |
| Shadowban | A strike put the player in the 6 second shadowban and sockpuppet flips revert. | Feed. |
| Strike | The Mod Eye issued a strike. | Feed, mods. |
| ModScan | The Mod Eye is telegraphing or scanning (3 second warning, then the sweep). | Feed, nearby nodes. |
| AlgorithmBoost | The Algorithm boost event is active (pairs with the AlgorithmBoost headline). | Feed. |
| StreamerFeature | A streamer is featuring an entry (pairs with StreamerDiscovers). | Feed. |
| Drama | A drama thread event is running (pairs with DramaThread). | Feed. |
| Idle | Ambient chatter when nothing special is happening; also fine for random hover bubbles. Mostly Neutral. | Anyone. |
| BotComment | Spam from Bot-Kind handles (after sockpuppets, or ambient while bots exist). | Bot handles. |

### Headlines.csv  (`Id,Event,Text`)  71 rows
News ticker lines.

| Column | Meaning |
|---|---|
| Id | `HL001`... |
| Event | Which director event triggers the line (table below). At least 5 per Event. |
| Text | Max **100** chars. Placeholders from the allowed set for that Event only. |

Placeholders (substituted by the director when the ticker line is built):

| Placeholder | Meaning |
|---|---|
| `{you}` | Title of the player's entry (Entries.csv `Name` of the chosen You row). |
| `{rival}` | Title of the jam's rival entry (e.g. `LEGEND OF DEV`). |
| `{entry}` | Title of the entry the event is about: the entry the director picked for that event (an Other entry, or any entry for LeadChange / AlgorithmBoost). |
| `{handle}` | A Handles.csv handle. Suggested Kind: Streamer for StreamerDiscovers, Mod for ModSweep and Disqualified, Celebrity or Dev for DramaThread, any for the rest. |

| Event | Fires when | Allowed placeholders |
|---|---|---|
| AlgorithmBoost | The Algorithm boosts an entry's visibility. | entry, handle |
| StreamerDiscovers | A streamer features an entry. | entry, handle |
| DramaThread | A drama thread erupts. | entry, handle, rival, you |
| ModSweep | A Mod Eye sweep happens. | handle, you |
| MegaCampaign | The rival starts the late-game Mega Campaign. Lines must name `{rival}`. | rival, you |
| LeadChange | The front-page leader changes; `{entry}` is the new leader. | entry, you, rival |
| FinalHours | The last stretch of voting begins. | you, rival, entry |
| Disqualified | The player is disqualified (3 strikes). Lines must name `{you}`. | you, handle |
| RivalCollapses | The rival collapses or is struck out (for example via Shift the Blame). Lines must name `{rival}`. | rival, you, handle |
| RivalWins | Final result: the rival won. Lines must name `{rival}`. | rival, you |
| YouWin | Final result: the player won. Lines must name `{you}`. | you, rival |

### Entries.csv  (`Id,Jam,Role,Name,Author,Blurb`)  41 rows
The games in each jam.

| Column | Meaning |
|---|---|
| Id | `TJ_Other01`, `RB_Other01`, `GO_Other01`, `You01`..., `Rival_TJ`, `Rival_RB`, `Rival_GO`. |
| Jam | `TinyJam7`, `Rumble48`, `GameOff2026`, or `All` (only for You rows, which are shared across jams). |
| Role | `Other` (third-party entry), `You` (a title option for the player), `Rival`. |
| Name | Game title, max 40 chars, unique across the whole file. |
| Author | Max 24 chars. Other: a fictional handle (never in Handles.csv). You: always `you` (the UI shows the player's name). Rival: the rival's handle. |
| Blurb | One-line pitch, max 90 chars. For the Rival it doubles as the one-line bio. |

Per jam there are exactly 8 `Other` rows and exactly 1 `Rival`; there are at least 12 `You` options. The Other
entries mine the jam's suggested readings of SHIFT (night shift, dimensions, gears, shifting blame, bit shifting,
shapeshifting, the Shift keys as the only controls) plus the "Cat Game" and "Untitled Platformer (Final).exe"
gags; each jam uses six of the seven readings and both gags, so names never repeat between jams.
Rivals: `@pixel_prince` / "Prince of Pixels" (Tiny Jam #7), `DevLegend` / "LEGEND OF DEV" (48h Pixel Rumble),
`MEGA STUDIOS` / "ULTRA GAME 9 (AAA)" (GAME OFF 2026). Follower counts (3K, 48K, 1.2M) in the rival blurbs are flavour
only; the numeric source of truth is `Jams.csv`.

Note: the brief listed only the three jam ids for `Jam`; You rows use the extra value `All` because the same
titles are offered in every jam. Treat `All` as "valid in any jam".

### Blurbs.csv  (`Id,Outcome,Condition,Text`)  53 rows
Results-screen lines and the closing wink.

| Column | Meaning |
|---|---|
| Id | `BL001`... |
| Outcome | `Win`, `Lose`, `DQ`, or `Wink`. |
| Condition | Why this line fits (table below). Only the combinations below exist. |
| Text | Max **110** chars. Placeholders below (none for Wink). |

Placeholders (all plain integers; thousands separators are fine):

| Placeholder | Meaning |
|---|---|
| `{rank}` | The player's final rank by votes (1 = winner). For DQ: the rank held when disqualified. |
| `{votes}` | The player's total votes. |
| `{honest}` | Votes that came from real (non-sockpuppet) flips. |
| `{sock}` | Votes that came from sockpuppet flips. |
| `{qualityRank}` | The player's rank by quality in this jam (1 = best game). Lines never add an ordinal suffix, so write "rank {qualityRank}" and not "{qualityRank}th". |
| `{plays}` | How many times the player's game was actually played (as opposed to voted for). |

| Outcome / Condition | Meaning |
|---|---|
| Win / CleanWin | Won with zero sockpuppets. |
| Win / SockpuppetWin | Won, and sockpuppets contributed. |
| Win / NarrowWin | Won by a small margin. |
| Win / Landslide | Won by a large margin. |
| Win / QualityIgnored | Won while the quality rank was far worse than first. |
| Win / NobodyPlayed | Won with a very high votes-per-play ratio. |
| Win / RivalCollapsed | Won after the rival collapsed or was struck out. |
| Lose / NearMiss | Lost by a small margin. |
| Lose / QualityIgnored | Lost, and the quality rank was much better than the final rank. |
| Lose / NobodyPlayed | Lost, with votes far outnumbering plays. |
| Lose / Landslide | Lost by a large margin. |
| Lose / RivalCollapsed | The rival collapsed but someone else won. |
| DQ / DQEarly | Disqualified in the first half of the jam. |
| DQ / DQLate | Disqualified in the second half. |
| Wink / Closing | Shown beneath the results or credits: a gentle nudge to leave an honest rating on the itch page. No placeholders, no URL. |

Selection suggestion: collect the conditions that apply to the run, prefer the more specific ones (RivalCollapsed,
NobodyPlayed, QualityIgnored, Landslide, then NarrowWin/NearMiss, then CleanWin/SockpuppetWin), pick a line with the
seeded PRNG, and if nothing applies fall back to any line with the same Outcome. Each valid combination has at least
2 lines; Wink has 6.

### Achievements.csv  (`Id,Name,Description,Hidden`)  14 rows

| Column | Meaning |
|---|---|
| Id | PascalCase key used by game code, e.g. `HonestRun`. |
| Name | Max 28 chars, unique. |
| Description | Max 90 chars. Qualitative; exact thresholds (seconds of SHIFT, "a third of the jam", cascade size) are decided in code. |
| Hidden | `true` shows the entry as locked with no text until earned (Banned Speedrun, Sticky Situation, Schadenfreude, Nobody Played It). |

### Lessons.csv  (`Id,Chapter,Step,Text`)  24 rows
The guided lessons of *Learn to play* (`Game/ShiftLesson.cpp`; the steps and what proves each one are in code, only the words are data). Chapter 0 holds the six blocks of the *How to play* reference page, each `Title: Text`; chapters 1 and 2 hold the steps of lesson 1 (10) and lesson 2 (8). Placeholders (`{sockheat}`, `{launder}`, `{strikes}`) are the coach-card ones, filled from the live parameters. `Data/validate_text.py` checks them (max 260 chars, `L<chapter>S<nn>` ids, steps 1..N per chapter, known placeholders only).

| Column | Meaning |
|---|---|
| Id | `L<chapter>S<nn>`, e.g. `L1S03`. |
| Chapter | 0 = reference page, 1 = The basics, 2 = Night shift. |
| Step | 1..N within the chapter. |
| Text | Max **260** chars. |

### Tutorial.csv  (`Id,Step,Trigger,Text`)  26 rows
Coachmarks shown during play. Numbers that come from the sim tuning are written as placeholders (`{sockheat}`, `{heatrate}`, `{redzone}`, `{strikes}`, `{ban}`, `{launder}`, `{rallycost}`, ...; the whitelist is `TUT_ALLOWED` in `validate_text.py`) and filled in when a card is shown.

| Column | Meaning |
|---|---|
| Id | `TUT01`... |
| Step | 1..N in teaching order (also the file order). |
| Trigger | The moment that shows the coachmark (table below). Unique. |
| Text | Max **90** chars. |

| Trigger | Fires when |
|---|---|
| JamStart | The run starts. |
| FirstHover | The first time the cursor hovers a voter. |
| FirstPlug | The player is about to click or has hovered a flippable voter (first click prompt). |
| FirstConvictionRing | The first conviction ring changes after a Plug. |
| PostsLow | Posts drop to 0 for the first time. |
| FirstReinforce | The player first has a voter of their own to Plug. |
| FirstGateSeen | A gated hub first comes into view or is hovered. |
| FirstGateOpened | The first gate unlocks without SHIFT. |
| ShiftUnlocked | The mid-run beat where night shift is introduced. Includes the Sticky Keys fallback ("press N"). |
| FirstHeat | Heat first rises above 0. |
| FirstModEye | The first Mod Eye telegraph. |
| FirstStrike | The first strike lands. |
| BlameRule | One strike from the end (the second strike lands): how Shift the Blame works as a last chance. The row used to be "BlameAvailable" (retired: its seen-bit 12 is never set again, the new card has its own, so players who read the old one are taught the new rule). May use `{blamefake}`. |
| KeysHint | A calm moment after the other coachmarks (pause, mute, menu keys). |

### Trailer.csv  (`Id,Bar,Cmd,Args`)  the cue script of the trailer run
Not a text table: it is not imported into a Data Table and `validate_text.py` does not read it. It is the cue script of the scripted trailer run (in the editor and Development builds, not in Shipping) (`Source/ShiftHappens/Trailer/ShiftTrailer.h`; how it is played, authored and recorded: `Docs/QA.md`, Trailer). The director reads the file when the run starts, so it can be edited without a rebuild. `Bar` is the position in bars of the music (112.5 BPM, 1 bar = 2.1333 s, 0.25 = one beat), `Cmd` is one of `black`, `flash`, `title`, `run`, `warp`, `bot`, `speed`, `night`, `tension`, `click`, `hover`, `spot`, `zoom`, `slam`, `caption`, `end`, and `Args` are space separated (the words of a `slam` or `caption` come last, a `|` starts a new line, no commas). Lines starting with `#` are comments. The rules of this README for words on screen apply: pure ASCII, no real people, brands or URLs.

## Validation

```
python -I Data/validate_text.py
```

Run it from the repo root (it locates the CSVs next to itself). It checks: header and column count per row (an
unquoted comma shows up as a wrong column count), pure ASCII, unique ids, enum values, minimum counts (Handles 120,
Bios 80, Comments 200 with at least 8 per Context, Headlines 60 with at least 5 per Event, Blurbs 40 results lines
plus 6 Wink lines, Achievements exactly 14, Tutorial 12 to 30 rows), length limits, the placeholder whitelist per file and
Event, exactly 8 Other and 1 Rival entries per jam, duplicate and near-duplicate lines, and a blocklist of real brand
names. It exits with status 1 and a list of problems if anything fails.

## Adding content

1. Append rows with the next free id (do not renumber existing ids; code and saves may reference them).
2. Keep to the length limit and the placeholder list for that file.
3. Run the validator, then re-run the Data Table import script.
