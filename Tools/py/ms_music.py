"""MS_Music: the looping game music as one MetaSound Source, clocked by Harmonix.

Clock    TriggerToTransport (Play from OnPlay) -> Metronome (112.5 BPM, 4/4, 8-bar simple loop) -> MidiClockSubdivisionTrigger (16th notes);
         the Metronome's MIDI Clock is also exposed as the graph output "MIDI Clock" (UShiftAudio binds a UMusicClockComponent to it
         for the beat pulse).
Counter  A Trigger Counter (auto-reset at 128) numbers the 16th notes of the 8-bar loop; Array::Get reads Float array literals (0 = rest)
         with that index, TriggerCompare turns every non-zero entry into a note trigger. Patterns are plain Python lists below.
Layers   Day (C major: pad, stabs, bass, marimba, kit), Night (A minor: dark pad, pulsing bass, bell, heartbeat kick, toms) and Tension
         (16th hats, rims, open hat, last-bar snare roll, a loop-long noise riser) are summed by one Audio Mixer whose gains are the float
         parameters DayGain, NightGain and TensionGain; then Plate Reverb, the Volume parameter and a limiter. Meters (Peak, Rms, Low, Mid,
         High) tap the signal before the volume and the limiter.
"""
import math
import os
import sys

import unreal

sys.path.append(os.path.join(unreal.SystemLibrary.get_project_directory(), "Tools", "py"))
from ms_graph import ENUM_COMPARE, GRID_16TH, STEP_SECONDS, Graph, add_meters  # noqa: E402
from ms_sfx import load_trims  # noqa: E402
from shift_common import log  # noqa: E402
from ms_voices import VoiceRef  # noqa: E402

BARS = 8
STEPS = BARS * 16
PAN_COMP = 1.4142   # equal-power panner is -3 dB at the centre; the original mix compensated, so do we
TEMPO = 112.5
LOOP_SECONDS = STEPS * STEP_SECONDS


# ---- pattern data (the original arrangement) ---------------------------------------------------------------------------------

DAY_PROG = [(48, (60, 64, 67)), (43, (59, 62, 67)), (45, (60, 64, 69)), (41, (60, 65, 69)),
            (48, (60, 64, 67)), (43, (59, 62, 67)), (41, (60, 65, 69)), (43, (59, 62, 67))]
DAY_MELODY = [
    [(0, 76), (3, 79), (4, 76), (8, 74), (10, 72), (12, 74)],
    [(0, 74), (3, 79), (4, 74), (8, 71), (10, 74), (12, 79)],
    [(0, 72), (3, 76), (4, 81), (8, 79), (10, 76), (12, 72)],
    [(0, 81), (3, 79), (4, 76), (8, 72), (10, 76), (12, 79)],
    [(0, 76), (3, 79), (4, 76), (8, 74), (10, 72), (12, 79)],
    [(0, 79), (3, 74), (4, 79), (8, 81), (10, 79), (12, 74)],
    [(0, 76), (3, 72), (4, 76), (8, 79), (10, 81), (12, 79)],
    [(0, 74), (2, 76), (4, 79), (6, 81), (8, 79), (12, 74), (14, 71)],
]
NIGHT_PROG = [(45, (57, 60, 64)), (40, (55, 59, 64)), (41, (57, 60, 65)), (38, (57, 62, 65)),
              (45, (57, 60, 64)), (40, (55, 59, 64)), (38, (57, 62, 65)), (40, (56, 59, 64))]
NIGHT_MELODY = [
    [(0, 69), (8, 76), (12, 72)], [(0, 71)], [(0, 72), (6, 69), (12, 65)], [(0, 69)],
    [(0, 76), (4, 74), (8, 72), (12, 69)], [(0, 67)], [(0, 74), (6, 77), (12, 81)], [(0, 68), (8, 71)],
]


def blank():
    return [0.0] * STEPS


def put(pattern, bar, step, value):
    pattern[bar * 16 + step] = float(value)


def from_hits(hits):
    """hits: iterable of (bar, step, value)."""
    p = blank()
    for bar, step, value in hits:
        put(p, bar, step, value)
    return p


# ---- clock and lanes ---------------------------------------------------------------------------------------------------------

class Clock:
    pass


def build_clock(g, harmonix=True):
    clk = Clock()
    if harmonix:
        transport = g.make("to_transport", {"Play": g.on_play}).out("Transport")
        metro = g.make("metronome", {"Transport": transport, "Simple Loop": True, "Loop Length (Bars)": BARS, "Time Sig. Numerator": 4,
                                     "Time Sig. Denominator": 4, "Tempo": TEMPO})
        clock = metro.out("MIDI Clock")
        g.output("MIDI Clock", "MIDIClock", clock)
        clk.step = g.make("subdiv", {"MIDI Clock": clock, "Base Grid Size": GRID_16TH}).out("Trigger Out")
    else:
        clk.step = g.make("repeat", {"Start": g.on_play, "Period": STEP_SECONDS}).out("RepeatOut")
    counter = g.make("counter", {"In": clk.step, "Reset Count": STEPS})
    clk.count = counter.out("Count")                      # 1 .. 128
    clk.on_trigger = counter.out("On Trigger")
    clk.idx = g.make("modulo", {"PrimaryOperand": clk.count, "AdditionalOperands": STEPS}).out("Out")   # 1 .. 127, 0
    bar_pos = g.make("modulo", {"PrimaryOperand": clk.count, "AdditionalOperands": 16}).out("Out")
    clk.loop_start = g.make("cmp_i", {"Compare": clk.on_trigger, "A": clk.count, "B": 1, "Type": ENUM_COMPARE["eq"]}).out("True")
    clk.bar_start = g.make("cmp_i", {"Compare": clk.on_trigger, "A": bar_pos, "B": 1, "Type": ENUM_COMPARE["eq"]}).out("True")
    g.output("StepCount", "Int32", clk.count)
    return clk


def rotated(pattern):
    """Count runs 1..128 and idx = Count % 128, so element i of the array holds step i - 1."""
    return [float(pattern[(i - 1) % STEPS]) for i in range(STEPS)]


def lane(g, clk, pattern, name, voices=1, extras=None):
    """A pattern lane. Returns one dict per voice: {"trig": Out, "val": Out (the pattern value), <extra name>: Out}.

    With several voices the note-on triggers are dealt round-robin (Trigger Sequence), and each voice reads its own value when its
    trigger fires, so an earlier note keeps ringing at its own pitch while the next one starts on another voice.

    QA tap: a Trigger Counter on the hit trigger is exposed as the graph output Hits_<name>_<hits per loop>; the meter run checks it
    against the pattern (a lane that never fires, or fires the wrong number of times, fails).
    """
    extras = extras or {}
    arrays = {"val": rotated(pattern)}
    arrays.update({k: rotated(v) for k, v in extras.items()})

    def read(trigger):
        return {k: g.make("get_f", {"Trigger": trigger, "Array": arr, "Index": clk.idx}).out("Element") for k, arr in arrays.items()}

    first = read(clk.on_trigger)
    hit = g.make("cmp_f", {"Compare": clk.on_trigger, "A": first["val"], "B": 0.0, "Type": ENUM_COMPARE["gt"]}).out("True")
    expected = sum(1 for value in pattern if value > 0.0)
    g.output("Hits_{}_{}".format(name, expected), "Int32", g.make("counter", {"In": hit}).out("Count"))
    if voices == 1:
        first["trig"] = hit
        return [first]
    seq = g.make("seq", {"In": hit, "Loop": True}, n=voices)
    out = []
    for k in range(voices):
        trig = seq.out("Out {}".format(k))
        entry = read(trig)
        entry["trig"] = trig
        out.append(entry)
    return out


def pan_hit(g, audio, pan, gain, tap):
    """Pan one voice into the layer mix. `tap` names a QA output Lvl_<tap> (peak of the voice before panning and gain)."""
    g.output("Lvl_" + tap, "Float", g.follower(audio, "peak", 0.002, 0.2))
    left, right = g.pan(audio, pan)
    return ((left, right), gain * PAN_COMP)


def hz(g, note):
    return g.midi_hz(note)


# ---- layers ------------------------------------------------------------------------------------------------------------------

def day_layer(g, clk, v):
    items = []
    # pad: one chord per bar, 3 tones, 2 voices each so a bar's tail rings into the next bar
    for n in range(3):
        pat = from_hits((b, 0, DAY_PROG[b][1][n]) for b in range(BARS))
        for k, voice in enumerate(lane(g, clk, pat, "DayPad{}".format(n), voices=2)):
            rel = g.delay_trigger(voice["trig"], 15 * STEP_SECONDS)
            audio = v["PadDay"].use(g, Gate=voice["trig"], Release=rel, Frequency=hz(g, voice["val"]))
            items.append(pan_hit(g, audio, (n - 1) * 0.5, 0.075, "DayPad{}_{}".format(n, k)))
    # offbeat stabs (chord tones an octave up)
    for n in range(3):
        hits = []
        for b in range(BARS):
            for s in ((6, 14, 10) if b % 2 == 1 else (6, 14)):
                hits.append((b, s, DAY_PROG[b][1][n] + 12))
        voice = lane(g, clk, from_hits(hits), "DayStab{}".format(n))[0]
        audio = v["Stab"].use(g, Trigger=voice["trig"], Frequency=hz(g, voice["val"]))
        items.append(pan_hit(g, audio, (n - 1) * 0.45, 0.085, "DayStab{}".format(n)))   # the patch's default Velocity (0.5) is the original voice level
    # bass: four hits per bar
    hits = []
    for b in range(BARS):
        bass = DAY_PROG[b][0]
        hits += [(b, 0, bass), (b, 6, bass), (b, 10, bass + 7), (b, 14, bass + 12)]
    voice = lane(g, clk, from_hits(hits), "DayBass")[0]
    items.append(pan_hit(g, v["BassDay"].use(g, Trigger=voice["trig"], Frequency=hz(g, voice["val"])), 0.0, 0.27, "DayBass"))
    # marimba melody, two voices
    hits = [(b, s, m) for b in range(BARS) for s, m in DAY_MELODY[b]]
    for k, voice in enumerate(lane(g, clk, from_hits(hits), "DayMelody", voices=2)):
        audio = v["Marimba"].use(g, Trigger=voice["trig"], Frequency=hz(g, voice["val"]), Velocity=0.92)
        items.append(pan_hit(g, audio, 0.25, 0.26, "DayMelody_{}".format(k)))
    # drums: the pattern values are velocity x the original lane gain
    hits = []
    for b in range(BARS):
        hits += [(b, 0, 0.9 * 0.5), (b, 8, 0.8 * 0.45)] + ([(b, 10, 0.6 * 0.36)] if b % 2 == 1 else [])
    voice = lane(g, clk, from_hits(hits), "DayKick")[0]
    items.append(pan_hit(g, v["KickDay"].use(g, Trigger=voice["trig"], Velocity=voice["val"]), 0.0, 1.0, "DayKick"))
    hits = [(b, s, vel) for b in range(BARS) for s, vel in ((4, 0.9 * 0.20), (12, 0.95 * 0.22))]
    voice = lane(g, clk, from_hits(hits), "DaySnare")[0]
    items.append(pan_hit(g, v["Snare"].use(g, Trigger=voice["trig"], Velocity=voice["val"]), 0.1, 1.0, "DaySnare"))
    hits = [(b, s, (0.9 if s % 4 == 0 else 0.55) * 0.085) for b in range(BARS) for s in range(0, 16, 2)]
    voice = lane(g, clk, from_hits(hits), "DayHat")[0]
    items.append(pan_hit(g, v["Hat"].use(g, Trigger=voice["trig"], Velocity=voice["val"]), -0.35, 1.0, "DayHat"))
    return g.mix_stereo(items)


def night_layer(g, clk, v):
    items = []
    for n in range(3):
        for octave, key, gain, pan in ((0, "PadNightLow", 0.15, (n - 1) * 0.6), (12, "PadNightHigh", 0.05, (1 - n) * 0.6)):
            pat = from_hits((b, 0, NIGHT_PROG[b][1][n] + octave) for b in range(BARS))
            for k, voice in enumerate(lane(g, clk, pat, "Night{}{}".format(key[8:], n), voices=2)):
                rel = g.delay_trigger(voice["trig"], 15 * STEP_SECONDS)
                audio = v[key].use(g, Gate=voice["trig"], Release=rel, Frequency=hz(g, voice["val"]))
                items.append(pan_hit(g, audio, pan, gain, "Night{}{}_{}".format(key[8:], n, k)))
    # pulsing bass: eighth notes with an accent pattern driving level and filter
    hits, accent = [], blank()
    for b in range(BARS):
        for s in range(0, 16, 2):
            a = 1.0 if s % 8 == 0 else (0.7 if s % 4 == 0 else 0.45)
            hits.append((b, s, NIGHT_PROG[b][0]))
            put(accent, b, s, a)
    voice = lane(g, clk, from_hits(hits), "NightBass", extras={"accent": accent})[0]
    cutoff = g.make("add_f", {"PrimaryOperand": g.fscale(voice["accent"], 90.0), "AdditionalOperands": 260.0}).out("Out")
    vel = g.make("add_f", {"PrimaryOperand": g.fscale(voice["accent"], 0.26), "AdditionalOperands": 0.05}).out("Out")
    audio = v["BassNight"].use(g, Trigger=voice["trig"], Frequency=hz(g, voice["val"]), Cutoff=cutoff, Velocity=vel)
    items.append(pan_hit(g, audio, 0.0, 1.0, "NightBass"))
    # bell melody
    hits = [(b, s, m) for b in range(BARS) for s, m in NIGHT_MELODY[b]]
    for k, voice in enumerate(lane(g, clk, from_hits(hits), "NightBell", voices=2)):
        audio = v["Bell"].use(g, Trigger=voice["trig"], Frequency=hz(g, voice["val"]), Velocity=0.8)
        items.append(pan_hit(g, audio, -0.1, 0.17, "NightBell_{}".format(k)))
    # heartbeat kick on every beat, shaker on the offbeats, toms every fourth bar
    hits = [(b, s, (0.9 * 0.42) if s % 8 == 0 else (0.6 * 0.28)) for b in range(BARS) for s in range(0, 16, 4)]
    voice = lane(g, clk, from_hits(hits), "NightKick")[0]
    items.append(pan_hit(g, v["KickNight"].use(g, Trigger=voice["trig"], Velocity=voice["val"]), 0.0, 1.0, "NightKick"))
    hits = [(b, s, 0.4 * 0.045) for b in range(BARS) for s in range(2, 16, 4)]
    voice = lane(g, clk, from_hits(hits), "NightShaker")[0]
    items.append(pan_hit(g, v["Hat"].use(g, Trigger=voice["trig"], Velocity=voice["val"]), 0.3, 1.0, "NightShaker"))
    hits = [(b, s, f) for b in range(BARS) if b % 4 == 3 for s, f in ((10, 90.0), (13, 75.0))]
    voice = lane(g, clk, from_hits(hits), "NightTom")[0]
    audio = v["Tom"].use(g, Trigger=voice["trig"], Frequency=voice["val"], Velocity=0.8 * 0.30)
    items.append(pan_hit(g, audio, 0.0, 1.0, "NightTom"))
    return g.mix_stereo(items)


def tension_layer(g, clk, v):
    items = []
    pattern = (1.0, 0.35, 0.7, 0.35)
    for parity, pan in ((0, -0.25), (1, 0.25)):
        hits = [(b, s, pattern[s % 4] * (0.55 + 0.045 * b) * 0.16) for b in range(BARS) for s in range(parity, 16, 2)]
        voice = lane(g, clk, from_hits(hits), "TensionHat{}".format(parity))[0]
        items.append(pan_hit(g, v["Hat"].use(g, Trigger=voice["trig"], Velocity=voice["val"]), pan, 1.0, "TensionHat{}".format(parity)))
    hits = [(b, 14, 0.8 * 0.10) for b in range(BARS)]
    voice = lane(g, clk, from_hits(hits), "TensionOpenHat")[0]
    items.append(pan_hit(g, v["HatOpen"].use(g, Trigger=voice["trig"], Velocity=voice["val"]), 0.0, 1.0, "TensionOpenHat"))
    for step, vel, pan in ((3, 0.7 * 0.20, 0.35), (11, 0.6 * 0.18, -0.35)):
        voice = lane(g, clk, from_hits((b, step, vel) for b in range(BARS)), "TensionRim{}".format(step))[0]
        items.append(pan_hit(g, v["Rim"].use(g, Trigger=voice["trig"], Velocity=voice["val"]), pan, 1.0, "TensionRim{}".format(step)))
    hits = [(BARS - 1, s, (0.25 + 0.65 * s / 15.0) * 0.10) for s in range(16)]
    voice = lane(g, clk, from_hits(hits), "TensionRoll")[0]
    items.append(pan_hit(g, v["Snare"].use(g, Trigger=voice["trig"], Velocity=voice["val"]), 0.0, 1.0, "TensionRoll"))
    # riser across the loop: noise through a band-pass sweeping 300 Hz -> 6 kHz, amplitude 0.04 + 0.55 * u^2, restarted every loop
    u = g.ad(clk.loop_start, LOOP_SECONDS, 0.001, audio=False)[0]
    cutoff = g.log_sweep(u, 300.0, 6000.0)
    band = g.svf(g.noise(), cutoff, 2.0)["bp"]
    swell, _ = g.ad(clk.loop_start, LOOP_SECONDS, 0.001, curve_a=2.0, audio=True)
    riser = g.mix([(band, 0.04), (g.mul(band, swell), 0.55)])
    items.append(pan_hit(g, riser, 0.0, 0.30, "TensionRiser"))
    return g.mix_stereo(items)


# ---- the source --------------------------------------------------------------------------------------------------------------

def build_music(patches, harmonix=True):
    trims = load_trims()
    v = {name[4:]: VoiceRef(asset) for name, asset in patches.items()}
    g = Graph("MS_Music", "source", stereo=True, one_shot=False)
    day_gain = g.input("DayGain", "Float", 1.0)
    night_gain = g.input("NightGain", "Float", 0.0)
    tension_gain = g.input("TensionGain", "Float", 0.0)
    volume = g.input("Volume", "Float", 1.0)
    clk = build_clock(g, harmonix)

    def trimmed(gain_in, key):
        db = float(trims.get(key, 0.0))
        return gain_in if abs(db) < 1e-3 else g.fscale(gain_in, math.pow(10.0, db / 20.0))

    day = day_layer(g, clk, v)
    night = night_layer(g, clk, v)
    tension = tension_layer(g, clk, v)
    left, right = g.mix_stereo([(day, trimmed(day_gain, "music_day")), (night, trimmed(night_gain, "music_night")),
                                (tension, trimmed(tension_gain, "music_tension"))])
    left, right = g.reverb(left, right, wet=0.16, dry=1.0, decay=0.8, damp=0.45)
    add_meters(g, left, right)
    g.stereo_out(g.limiter(g.mul(left, volume), -1.0), g.limiter(g.mul(right, volume), -1.0))
    asset = g.save("MS_Music")
    log("MS_Music built ({} bars, {} steps, {:.3f} s loop, harmonix clock: {})".format(BARS, STEPS, LOOP_SECONDS, harmonix))
    return asset
