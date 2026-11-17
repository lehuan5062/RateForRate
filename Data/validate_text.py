"""Validate the text Data Tables in this folder (Handles, Bios, Comments, Headlines, Entries, Blurbs,
Achievements, Tutorial).

Run:  python -I Data/validate_text.py          (from the repo root, or with any working directory)

Checks: exact header, column count per row (catches unquoted commas), UTF-8 / pure ASCII text, unique ids,
enum values, minimum counts, length limits, placeholder whitelist per file/event, empty or padded fields,
duplicate and near-duplicate lines, and a blocklist of real brand names. Exits 1 on any error.
"""
import csv
import difflib
import pathlib
import re
import sys

DATA = pathlib.Path(__file__).resolve().parent
errors = []
warnings = []


def err(msg):
    errors.append(msg)


# ----------------------------------------------------------------------------- helpers
ID_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")
PH_RE = re.compile(r"\{([^{}]*)\}")
NEAR_DUP = 0.88

# Words that would point at a real brand/company/product/platform. 'godot' is allowed only in the handle godot_gal.
BLOCKLIST = [
    "unity", "unreal", "godot", "steam", "twitch", "discord", "youtube", "reddit", "google", "github", "microsoft",
    "windows", "nintendo", "sony", "playstation", "xbox", "minecraft", "fortnite", "roblox", "tiktok", "twitter",
    "instagram", "facebook", "apple", "valve", "ubisoft", "mario", "zelda", "pokemon", "amazon", "netflix", "nvidia",
]
BLOCK_RE = re.compile(r"\b(" + "|".join(BLOCKLIST) + r")\b", re.IGNORECASE)
GAME_OFF_RE = re.compile(r"game[\s_-]*off", re.IGNORECASE)


def load(name, header):
    """Return list of dict rows; reports structural problems."""
    path = DATA / name
    if not path.exists():
        err(f"{name}: file is missing")
        return []
    raw = path.read_bytes()
    if raw.startswith(b"\xef\xbb\xbf"):
        err(f"{name}: has a UTF-8 BOM (remove it)")
    try:
        text = raw.decode("ascii")
    except UnicodeDecodeError as e:
        bad = raw[max(0, e.start - 20): e.start + 20].decode("utf-8", "replace")
        err(f"{name}: non-ASCII byte near {bad!r}")
        text = raw.decode("utf-8", "replace")
    if "\r" in text:
        err(f"{name}: contains CR characters (use LF line endings)")
    if not text.endswith("\n"):
        err(f"{name}: must end with a newline")
    for ln, line in enumerate(text.split("\n"), 1):
        if any(ord(c) < 32 and c not in "\r" for c in line):
            err(f"{name}:{ln}: control character")
    reader = csv.reader(text.splitlines())
    rows = list(reader)
    if not rows:
        err(f"{name}: empty")
        return []
    if rows[0] != header:
        err(f"{name}: header is {rows[0]} but expected {header}")
        return []
    out = []
    for i, r in enumerate(rows[1:], 2):
        if len(r) != len(header):
            err(f"{name}:{i}: {len(r)} columns, expected {len(header)} (unquoted comma?): {r}")
            continue
        d = dict(zip(header, r))
        d["_line"] = i
        for k, v in d.items():
            if k == "_line":
                continue
            if v == "":
                err(f"{name}:{i}: empty field {k}")
            if v != v.strip():
                err(f"{name}:{i}: field {k} has leading/trailing whitespace")
            if "  " in v:
                err(f"{name}:{i}: field {k} has a double space")
            if BLOCK_RE.search(v) and not (name == "Handles.csv" and v == "godot_gal"):
                err(f"{name}:{i}: possible real brand name in {k}: {v!r}")
            if k != "Jam" and GAME_OFF_RE.search(v):
                err(f"{name}:{i}: the jam name must not appear in text: {v!r}")
        out.append(d)
    ids = [d[header[0]] for d in out]
    seen = set()
    for d in out:
        i = d[header[0]]
        if not ID_RE.match(i):
            err(f"{name}:{d['_line']}: bad id {i!r}")
        if i.lower() in seen:
            err(f"{name}:{d['_line']}: duplicate id {i!r}")
        seen.add(i.lower())
    return out


def check_len(name, rows, col, limit):
    for d in rows:
        if len(d[col]) > limit:
            err(f"{name}:{d['_line']}: {col} is {len(d[col])} chars (max {limit}): {d[col]!r}")


def check_enum(name, rows, col, allowed):
    for d in rows:
        if d[col] not in allowed:
            err(f"{name}:{d['_line']}: {col}={d[col]!r} not in {sorted(allowed)}")


def check_placeholders(name, rows, col, allowed_for):
    for d in rows:
        text = d[col]
        allowed = allowed_for(d)
        for ph in PH_RE.findall(text):
            if ph not in allowed:
                err(f"{name}:{d['_line']}: placeholder {{{ph}}} not allowed here (allowed: {sorted(allowed) or 'none'}): {text!r}")
        if "{" in PH_RE.sub("", text) or "}" in PH_RE.sub("", text):
            err(f"{name}:{d['_line']}: stray brace in {text!r}")


def check_dups(name, rows, col):
    norm = []
    for d in rows:
        n = re.sub(r"[^a-z0-9{} ]", "", d[col].lower())
        norm.append((d["_line"], n, d[col]))
    for a in range(len(norm)):
        for b in range(a + 1, len(norm)):
            la, na, ta = norm[a]
            lb, nb, tb = norm[b]
            if na == nb:
                err(f"{name}: duplicate text on lines {la} and {lb}: {ta!r}")
            elif difflib.SequenceMatcher(None, na, nb).ratio() >= NEAR_DUP:
                err(f"{name}: near-duplicate text on lines {la} and {lb}: {ta!r} / {tb!r}")


def check_min_per(name, rows, col, values, minimum):
    counts = {v: 0 for v in values}
    for d in rows:
        if d[col] in counts:
            counts[d[col]] += 1
    for v, c in counts.items():
        if c < minimum:
            err(f"{name}: only {c} rows with {col}={v} (need {minimum})")
    return counts


summary = []

# ----------------------------------------------------------------------------- Handles
KINDS = ["Lurker", "Dev", "Streamer", "Bot", "Mod", "Celebrity"]
H = load("Handles.csv", ["Id", "Handle", "Kind"])
check_enum("Handles.csv", H, "Kind", KINDS)
check_len("Handles.csv", H, "Handle", 26)
for d in H:
    if not re.match(r"^[A-Za-z0-9_.]+$", d["Handle"]):
        err(f"Handles.csv:{d['_line']}: handle has characters outside A-Z a-z 0-9 _ . : {d['Handle']!r}")
if len(H) < 120:
    err(f"Handles.csv: {len(H)} rows (need >= 120)")
check_min_per("Handles.csv", H, "Kind", KINDS, 8)
seen = {}
for d in H:
    k = d["Handle"].lower()
    if k in seen:
        err(f"Handles.csv:{d['_line']}: duplicate handle {d['Handle']!r} (also line {seen[k]})")
    seen[k] = d["_line"]
check_placeholders("Handles.csv", H, "Handle", lambda d: set())
check_dups("Handles.csv", H, "Handle")
summary.append(("Handles.csv", len(H)))

# ----------------------------------------------------------------------------- Bios
B = load("Bios.csv", ["Id", "Kind", "Text"])
check_enum("Bios.csv", B, "Kind", KINDS)
check_len("Bios.csv", B, "Text", 60)
if len(B) < 80:
    err(f"Bios.csv: {len(B)} rows (need >= 80)")
check_min_per("Bios.csv", B, "Kind", KINDS, 8)
check_placeholders("Bios.csv", B, "Text", lambda d: set())
check_dups("Bios.csv", B, "Text")
summary.append(("Bios.csv", len(B)))

# ----------------------------------------------------------------------------- Comments
SENT = ["Positive", "Negative", "Neutral", "Suspicious"]
CTX = ["FlipToYou", "FlipToRival", "FlipToOther", "Dent", "Reinforce", "GateOpen", "Sockpuppet", "Shadowban", "Strike",
       "ModScan", "AlgorithmBoost", "StreamerFeature", "Drama", "Idle", "BotComment"]
C = load("Comments.csv", ["Id", "Sentiment", "Context", "Text"])
check_enum("Comments.csv", C, "Sentiment", SENT)
check_enum("Comments.csv", C, "Context", CTX)
check_len("Comments.csv", C, "Text", 70)
if len(C) < 200:
    err(f"Comments.csv: {len(C)} rows (need >= 200)")
check_min_per("Comments.csv", C, "Context", CTX, 8)
check_min_per("Comments.csv", C, "Sentiment", SENT, 10)
check_placeholders("Comments.csv", C, "Text", lambda d: set())
check_dups("Comments.csv", C, "Text")
summary.append(("Comments.csv", len(C)))

# ----------------------------------------------------------------------------- Headlines
HL_ALLOWED = {
    "AlgorithmBoost": {"entry", "handle"},
    "StreamerDiscovers": {"entry", "handle"},
    "DramaThread": {"entry", "handle", "rival", "you"},
    "ModSweep": {"handle", "you"},
    "MegaCampaign": {"rival", "you"},
    "LeadChange": {"entry", "you", "rival"},
    "FinalHours": {"you", "rival", "entry"},
    "Disqualified": {"you", "handle"},
    "RivalCollapses": {"rival", "you", "handle"},
    "RivalWins": {"rival", "you"},
    "YouWin": {"you", "rival"},
}
HL = load("Headlines.csv", ["Id", "Event", "Text"])
check_enum("Headlines.csv", HL, "Event", set(HL_ALLOWED))
check_len("Headlines.csv", HL, "Text", 100)
if len(HL) < 60:
    err(f"Headlines.csv: {len(HL)} rows (need >= 60)")
check_min_per("Headlines.csv", HL, "Event", list(HL_ALLOWED), 5)
check_placeholders("Headlines.csv", HL, "Text", lambda d: HL_ALLOWED.get(d["Event"], set()))
check_dups("Headlines.csv", HL, "Text")
# Events that are about a specific party must name it, so the ticker never reads as generic filler.
MUST = {"YouWin": "you", "Disqualified": "you", "RivalWins": "rival", "RivalCollapses": "rival", "MegaCampaign": "rival"}
for d in HL:
    need = MUST.get(d["Event"])
    if need and "{" + need + "}" not in d["Text"]:
        err(f"Headlines.csv:{d['_line']}: {d['Event']} headline should contain {{{need}}}: {d['Text']!r}")
summary.append(("Headlines.csv", len(HL)))

# ----------------------------------------------------------------------------- Entries
JAMS = ["TinyJam7", "Rumble48", "GameOff2026"]
E = load("Entries.csv", ["Id", "Jam", "Role", "Name", "Author", "Blurb"])
check_enum("Entries.csv", E, "Jam", set(JAMS) | {"All"})
check_enum("Entries.csv", E, "Role", {"Other", "You", "Rival"})
check_len("Entries.csv", E, "Name", 40)
check_len("Entries.csv", E, "Author", 24)
check_len("Entries.csv", E, "Blurb", 90)
for d in E:
    if d["Role"] == "You" and (d["Jam"] != "All" or d["Author"] != "you"):
        err(f"Entries.csv:{d['_line']}: You rows must use Jam=All and Author=you")
    if d["Role"] != "You" and d["Jam"] == "All":
        err(f"Entries.csv:{d['_line']}: Jam=All is only for You rows")
for jam in JAMS:
    others = [d for d in E if d["Jam"] == jam and d["Role"] == "Other"]
    rivals = [d for d in E if d["Jam"] == jam and d["Role"] == "Rival"]
    if len(others) != 8:
        err(f"Entries.csv: {jam} has {len(others)} Other rows (need exactly 8)")
    if len(rivals) != 1:
        err(f"Entries.csv: {jam} has {len(rivals)} Rival rows (need exactly 1)")
yous = [d for d in E if d["Role"] == "You"]
if len(yous) < 12:
    err(f"Entries.csv: {len(yous)} You rows (need >= 12)")
RIVALS = {"TinyJam7": ("Prince of Pixels", "@pixel_prince"), "Rumble48": ("LEGEND OF DEV", "DevLegend"),
          "GameOff2026": ("ULTRA GAME 9 (AAA)", "MEGA STUDIOS")}
for d in E:
    if d["Role"] == "Rival" and d["Jam"] in RIVALS and (d["Name"], d["Author"]) != RIVALS[d["Jam"]]:
        err(f"Entries.csv:{d['_line']}: rival for {d['Jam']} should be {RIVALS[d['Jam']]}")
names = {}
for d in E:
    k = d["Name"].lower()
    if k in names:
        err(f"Entries.csv:{d['_line']}: duplicate entry name {d['Name']!r} (also line {names[k]})")
    names[k] = d["_line"]
handle_set = {h["Handle"].lower() for h in H}
authors = {}
for d in E:
    a = d["Author"].lower()
    if d["Role"] == "Other":
        if a in authors:
            err(f"Entries.csv:{d['_line']}: duplicate author {d['Author']!r}")
        authors[a] = d["_line"]
        if a in handle_set:
            err(f"Entries.csv:{d['_line']}: author {d['Author']!r} duplicates a Handles.csv handle")
check_placeholders("Entries.csv", E, "Blurb", lambda d: set())
check_placeholders("Entries.csv", E, "Name", lambda d: set())
check_dups("Entries.csv", E, "Blurb")
summary.append(("Entries.csv", len(E)))

# ----------------------------------------------------------------------------- Blurbs
BL_PH = {"rank", "votes", "honest", "sock", "qualityRank", "plays"}
COMBOS = {
    "Win": ["CleanWin", "SockpuppetWin", "NarrowWin", "Landslide", "QualityIgnored", "NobodyPlayed", "RivalCollapsed"],
    "Lose": ["NearMiss", "QualityIgnored", "NobodyPlayed", "Landslide", "RivalCollapsed"],
    "DQ": ["DQEarly", "DQLate"],
    "Wink": ["Closing"],
}
BL = load("Blurbs.csv", ["Id", "Outcome", "Condition", "Text"])
check_enum("Blurbs.csv", BL, "Outcome", set(COMBOS))
check_enum("Blurbs.csv", BL, "Condition", {c for v in COMBOS.values() for c in v})
check_len("Blurbs.csv", BL, "Text", 110)
for d in BL:
    if d["Outcome"] in COMBOS and d["Condition"] not in COMBOS[d["Outcome"]]:
        err(f"Blurbs.csv:{d['_line']}: Condition {d['Condition']} is not valid for Outcome {d['Outcome']}")
results = [d for d in BL if d["Outcome"] in ("Win", "Lose", "DQ")]
winks = [d for d in BL if d["Outcome"] == "Wink"]
if len(results) < 40:
    err(f"Blurbs.csv: {len(results)} Win/Lose/DQ rows (need >= 40)")
if len(winks) < 6:
    err(f"Blurbs.csv: {len(winks)} Wink rows (need >= 6)")
for o, conds in COMBOS.items():
    for c in conds:
        n = sum(1 for d in BL if d["Outcome"] == o and d["Condition"] == c)
        if n < (6 if o == "Wink" else 2):
            err(f"Blurbs.csv: only {n} rows for {o}/{c}")
check_placeholders("Blurbs.csv", BL, "Text", lambda d: set() if d["Outcome"] == "Wink" else BL_PH)
check_dups("Blurbs.csv", BL, "Text")
for d in winks:
    if "http" in d["Text"].lower() or "www" in d["Text"].lower() or ".com" in d["Text"].lower():
        err(f"Blurbs.csv:{d['_line']}: Wink lines must not contain a URL")
summary.append(("Blurbs.csv", len(BL)))

# ----------------------------------------------------------------------------- Achievements
A = load("Achievements.csv", ["Id", "Name", "Description", "Hidden"])
check_enum("Achievements.csv", A, "Hidden", {"true", "false"})
check_len("Achievements.csv", A, "Name", 28)
check_len("Achievements.csv", A, "Description", 90)
if len(A) != 14:
    err(f"Achievements.csv: {len(A)} rows (need exactly 14)")
if len({d["Name"].lower() for d in A}) != len(A):
    err("Achievements.csv: duplicate achievement names")
check_placeholders("Achievements.csv", A, "Description", lambda d: set())
check_dups("Achievements.csv", A, "Description")
for must in ["Honest Run", "Night Owl", "Snowball", "Banned Speedrun", "Best Game, Worst Rank", "Shifted the Blame",
             "Gate Crasher", "Audience Capture"]:
    if must not in {d["Name"] for d in A}:
        err(f"Achievements.csv: missing required achievement {must!r}")
summary.append(("Achievements.csv", len(A)))

# ----------------------------------------------------------------------------- Tutorial
T = load("Tutorial.csv", ["Id", "Step", "Trigger", "Text"])
check_len("Tutorial.csv", T, "Text", 140)
if not 12 <= len(T) <= 20:
    err(f"Tutorial.csv: {len(T)} rows (expected about 17)")
steps = []
for d in T:
    if not d["Step"].isdigit():
        err(f"Tutorial.csv:{d['_line']}: Step must be an integer")
    else:
        steps.append(int(d["Step"]))
if steps != list(range(1, len(T) + 1)):
    err(f"Tutorial.csv: Step must run 1..{len(T)} in file order, got {steps}")
if len({d["Trigger"] for d in T}) != len(T):
    err("Tutorial.csv: Trigger values must be unique")
for d in T:
    if not re.match(r"^[A-Za-z][A-Za-z0-9]*$", d["Trigger"]):
        err(f"Tutorial.csv:{d['_line']}: bad Trigger {d['Trigger']!r}")
alltext = " ".join(d["Text"].lower() for d in T)
for kw in ["hover", "plug", "gate", "posts", "conviction", "heat", "hold shift", "sticky keys", "press n", "mod eye",
           "strike", "shift the blame"]:
    if kw not in alltext:
        err(f"Tutorial.csv: no line mentions {kw!r}")
# numbers that come from the sim tuning (expanded by AShiftPlayerController::ExpandTutorialText when a card is shown)
TUT_ALLOWED = {"sockheat", "heatrate", "redzone", "strikes", "ban", "launder"}
check_placeholders("Tutorial.csv", T, "Text", lambda d: TUT_ALLOWED)
check_dups("Tutorial.csv", T, "Text")
summary.append(("Tutorial.csv", len(T)))

# ----------------------------------------------------------------------------- report
print("Row counts:")
for n, c in summary:
    print(f"  {n:<18}{c:>4}")
if warnings:
    print("Warnings:")
    for w in warnings:
        print("  " + w)
if errors:
    print(f"\nFAILED: {len(errors)} problem(s)")
    for e in errors:
        print("  - " + e)
    sys.exit(1)
print("\nOK: all text tables valid.")
