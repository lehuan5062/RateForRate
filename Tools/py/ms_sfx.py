"""The 26 one-shot sound effects, each a MetaSound Source (/Game/Shift/Audio/MS_Sfx_<Name>) built from the voices in ms_voices.

A recipe is a function (g, t) -> (list of (audio, gain) parts, seconds, reverb_wet): `t` is the OnPlay trigger, `seconds` how long the
sound lasts (OnFinished fires after it, plus a tail when reverb is on). The level of every effect is set by a trim in dB
(Tools/py/ms_trims.json, produced by Tools/py/ms_calibrate.py from the in-graph meters so each effect peaks at TARGET_DB) and a final limiter.
"""
import json
import math
import os
import sys

import unreal

sys.path.append(os.path.join(unreal.SystemLibrary.get_project_directory(), "Tools", "py"))
from ms_graph import Graph, add_meters  # noqa: E402
from ms_voices import bell, burst, marimba, noise_sweep, pad, tom, tone  # noqa: E402
from shift_common import PROJECT_DIR, error, log  # noqa: E402

TRIMS_PATH = os.path.join(PROJECT_DIR, "Tools", "py", "ms_trims.json")   # dB per sound, from Tools/py/ms_calibrate.py (targets: ms_targets.py)


def at(g, t, seconds):
    return t if seconds <= 0.0 else g.delay_trigger(t, seconds)


# ---- recipes -----------------------------------------------------------------------------------------------------------------

def click(g, t):
    return [(tone(g, t, "tri", 1300, 1250, 0.04, 0.001, 0.012), 0.8), (burst(g, t, 0.02, 0.004, "hp", 3000, 0.7), 0.3)], 0.05, 0.0


def hover(g, t):
    return [(tone(g, t, "sine", 880, 1100, 0.06, 0.008, 0.025), 1.0)], 0.07, 0.0


def slider(g, t):
    return [(tone(g, t, "square", 640, 640, 0.03, 0.001, 0.01), 1.0)], 0.04, 0.0


def upload(g, t):
    parts = [(marimba(g, at(g, t, 0.075 * i), n), 0.7) for i, n in enumerate((523.25, 659.26, 783.99, 1046.5))]
    parts.append((noise_sweep(g, t, 0.5, 800, 6000, 1.5, 0.6), 0.15))
    return parts, 1.5, 0.12


def transition(g, t):
    return [(tone(g, t, "sine", 300, 700, 0.18, 0.004, 0.08), 0.5), (noise_sweep(g, t, 0.25, 400, 2500, 1.2, 0.5), 0.2)], 0.3, 0.1


def tip(g, t):
    return [(bell(g, t, 1318.5, 0.9, 0.20), 1.0)], 1.0, 0.1


def plug(g, t):
    return [(tone(g, t, "sine", 620, 190, 0.09, 0.002, 0.04), 0.9), (burst(g, t, 0.015, 0.003, "hp", 2500, 0.7), 0.3)], 0.1, 0.0


def flip(g, t):
    t2 = at(g, t, 0.07)
    return [(bell(g, t, 1318.5, 0.9, 0.16), 0.8), (bell(g, t2, 1975.5, 0.8, 0.14), 0.7), (tone(g, t, "sine", 500, 1800, 0.12, 0.002, 0.05), 0.2)], 0.9, 0.12


def reinforce(g, t):
    return [(tone(g, t, "sine", 520, 520, 0.07, 0.003, 0.03), 0.7), (tone(g, at(g, t, 0.07), "sine", 780, 780, 0.08, 0.003, 0.035), 0.7)], 0.16, 0.0


def weaken(g, t):
    return [(tone(g, t, "tri", 420, 240, 0.2, 0.004, 0.1, 14.0, 0.5), 1.0)], 0.22, 0.0


def gated(g, t):
    return [(tone(g, t, "square", 120, 90, 0.18, 0.002, 0.06), 0.7), (burst(g, t, 0.12, 0.03, "lp", 900, 0.7), 0.6)], 0.2, 0.0


def locked(g, t):
    return [(burst(g, t, 0.03, 0.006, "bp", 2200, 4.0), 0.9), (burst(g, at(g, t, 0.05), 0.03, 0.006, "bp", 1700, 4.0), 0.8)], 0.1, 0.0


def no_posts(g, t):
    a = tone(g, t, "saw", 196, 196, 0.1, 0.003, 0.08, level=0.6)
    b = tone(g, at(g, t, 0.1), "saw", 147, 147, 0.16, 0.003, 0.1, level=0.6)
    return [(g.lp1(g.mix([(a, 1.0), (b, 1.0)]), 900.0), 1.0)], 0.3, 0.0


def night_on(g, t):
    return [(noise_sweep(g, t, 0.9, 5000, 200, 1.2, 0.25), 0.5), (tone(g, t, "sine", 130, 38, 0.7, 0.01, 0.3), 0.9)], 0.95, 0.2


def night_off(g, t):
    return [(noise_sweep(g, t, 0.6, 200, 6000, 1.2, 0.75), 0.5), (tone(g, t, "sine", 180, 720, 0.45, 0.02, 0.2), 0.5)], 0.65, 0.12


def sock(g, t):
    voice = g.mix([(tone(g, t, "saw", 1200, 90, 0.22, 0.001, 0.09), 0.7), (tone(g, at(g, t, 0.04), "square", 240, 1900, 0.1, 0.001, 0.05), 0.3)])
    # stutter: a 66.7 Hz gate that is open for two thirds of every period
    gate = g.osc("square", 66.667, extra={"Bi Polar": False, "Pulse Width": 0.667})
    return [(g.mul(voice, gate), 1.0), (burst(g, t, 0.06, 0.015, "hp", 4000, 0.7), 0.3)], 0.25, 0.0


def heartbeat(g, t):
    return [(tom(g, t, 62.0, 1.0), 1.0), (tom(g, at(g, t, 0.19), 56.0, 0.8), 0.8)], 0.8, 0.0


def mod_warn(g, t):
    return [(tone(g, t, "sine", 1400, 1400, 1.2, 0.003, 0.35), 0.7), (tone(g, at(g, t, 0.34), "sine", 1400, 1400, 1.2, 0.003, 0.35), 0.3)], 1.6, 0.15


def mod_sweep(g, t):
    return [(noise_sweep(g, t, 1.0, 300, 5000, 2.5, 0.5), 1.0)], 1.0, 0.15


def strike(g, t):
    parts = [(tom(g, t, 55.0, 1.0), 1.0)]
    for i in range(2):
        ti = at(g, t, 0.16 * i)
        parts.append((tone(g, ti, "square", 820, 800, 0.12, 0.002, 0.1), 0.6))
        parts.append((tone(g, ti, "square", 1230, 1200, 0.12, 0.002, 0.1), 0.4))
    return parts, 0.6, 0.08


def shadowban(g, t):
    voice = g.mix([(pad(g, t, at(g, t, 0.3), 220.0, 4000.0, 0.005, 0.2), 0.5), (noise_sweep(g, t, 0.6, 6000, 150, 1.0, 0.1), 0.3)])
    u = g.ad(t, 0.6, 0.001, audio=False)[0]
    cutoff = g.log_sweep(u, 2500.0, 150.0)   # a low-pass that closes from 2.5 kHz to 150 Hz
    return [(g.svf(voice, cutoff, 0.8)["lp"], 1.0)], 0.7, 0.1


def blame(g, t):
    return [(tom(g, t, 70.0, 1.0), 0.9), (burst(g, at(g, t, 0.02), 0.05, 0.012, "hp", 1800, 0.7), 0.6),
            (noise_sweep(g, at(g, t, 0.05), 0.25, 500, 3500, 1.0, 0.3), 0.25)], 0.5, 0.06


def day(g, t):
    return [(tone(g, t, "sine", 1000, 960, 0.03, 0.001, 0.01), 0.6), (burst(g, t, 0.012, 0.002, "hp", 3500, 0.7), 0.3)], 0.04, 0.0


def win(g, t):
    parts = [(marimba(g, at(g, t, 0.11 * i), n), 0.6) for i, n in enumerate((523.25, 659.26, 783.99, 1046.5))]
    t_chord = at(g, t, 0.40)
    t_off = at(g, t, 0.40 + 0.9)
    for n in (523.25, 659.26, 783.99, 1046.5):
        parts.append((pad(g, t_chord, t_off, n, 3000.0, 0.05, 0.9), 0.18))
    for n in (1046.5, 1318.5, 1568.0):
        parts.append((bell(g, t_chord, n, 0.7, 0.5), 0.18))
    return parts, 2.4, 0.2


def lose(g, t):
    voice = g.mix([(tone(g, t, "saw", 311, 294, 0.5, 0.01, 0.35, 5.0, 0.4), 0.6), (tone(g, at(g, t, 0.45), "saw", 262, 247, 0.7, 0.01, 0.45, 5.0, 0.4), 0.6)])
    return [(g.ladder(voice, 1200.0, 1.0), 1.0)], 1.3, 0.12


def dq(g, t):
    return [(tone(g, t, "square", 130, 128, 0.6, 0.004, 0.35), 0.6), (tone(g, t, "square", 137, 135, 0.6, 0.004, 0.35), 0.6),
            (burst(g, t, 0.4, 0.12, "lp", 1500, 0.7), 0.4)], 0.65, 0.0


RECIPES = {
    "Click": click, "Hover": hover, "Slider": slider, "Upload": upload, "Transition": transition, "Tip": tip, "Plug": plug, "Flip": flip,
    "Reinforce": reinforce, "Weaken": weaken, "Gated": gated, "Locked": locked, "NoPosts": no_posts, "NightOn": night_on,
    "NightOff": night_off, "Sock": sock, "Heartbeat": heartbeat, "ModWarn": mod_warn, "ModSweep": mod_sweep, "Strike": strike,
    "Shadowban": shadowban, "Blame": blame, "Day": day, "Win": win, "Lose": lose, "DQ": dq,
}


def load_trims():
    try:
        with open(TRIMS_PATH, "r", encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return {}


def build_sfx(trims=None):
    """Build MS_Sfx_<Name> for every recipe. Returns (built, failed)."""
    trims = load_trims() if trims is None else trims
    built, failed = 0, []
    for name, recipe in RECIPES.items():
        asset_name = "MS_Sfx_" + name
        try:
            g = Graph(asset_name, "source", stereo=True, one_shot=True)
            parts, seconds, wet = recipe(g, g.on_play)
            mono = g.mix(parts)
            trim_db = float(trims.get(asset_name, 0.0))
            if abs(trim_db) > 1e-3:
                mono = g.mul(mono, math.pow(10.0, trim_db / 20.0))
            left = right = mono
            if wet > 0.0:
                left, right = g.reverb(mono, mono, wet=wet, dry=1.0, decay=0.7, damp=0.45)
            add_meters(g, left, right)
            g.stereo_out(g.limiter(left, -1.0), g.limiter(right, -1.0))
            g.finish_after(seconds + (0.7 if wet > 0.0 else 0.05))
            g.save(asset_name)
            built += 1
        except Exception as exc:  # noqa: BLE001
            failed.append(name)
            error("sfx {} failed: {}".format(asset_name, exc))
    log("sfx built {} of {}".format(built, len(RECIPES)))
    return built, failed
