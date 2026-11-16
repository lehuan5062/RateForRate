"""Instrument voices for the music and the sound effects, written as MetaSound graphs.

Each voice is a Python function that emits nodes into a Graph (a source or a patch builder) and returns a mono Audio Out.
The ones the music reuses many times are also wrapped as MetaSound Patch assets (/Game/Shift/Audio/MSP_*), so the music source
instantiates them as nodes: pad x18, marimba x2, bell x2, ... The sound effects use the same functions inline.

Recipes are ported from the first (offline) version of the game's audio: partial ratios, decay times, filter cutoffs.
Time constants (tau) are the decays of exp(-t / tau); see Graph.exp_env.
"""
import os
import sys

import unreal

sys.path.append(os.path.join(unreal.SystemLibrary.get_project_directory(), "Tools", "py"))
from ms_graph import Graph, Out  # noqa: E402


def fk(g, freq, k):
    """Frequency (number or Float Out) times a constant."""
    if k == 1.0:
        return freq
    if isinstance(freq, Out):
        return g.fscale(freq, k)
    return float(freq) * k


def amp(g, sig, level):
    """Scale an Audio Out by a number or Float Out; skip the node for 1.0."""
    if not isinstance(level, Out) and level == 1.0:
        return sig
    return g.mul(sig, level)


# ---- pitched voices ----------------------------------------------------------------------------------------------------------

def marimba(g, trig, freq, vel=1.0):
    parts = []
    for ratio, tau, gain in ((1.0, 0.50, 1.0), (4.0, 0.12, 0.35), (9.9, 0.04, 0.10)):
        osc = g.osc("sine", fk(g, freq, ratio), sync=trig)
        parts.append((g.mul(osc, g.exp_env(trig, tau, attack=0.002)), gain))
    parts.append((g.mul(g.noise(), g.exp_env(trig, 0.004, attack=0.0005)), 0.12))
    return amp(g, g.mix(parts), vel)


def bell(g, trig, freq, vel=1.0, decay=1.1):
    """Two-operator FM bell: carrier at F, modulator at 3.5 F (index 2.2 decaying), plus the octave partial."""
    modulator = g.osc("sine", fk(g, freq, 3.5), sync=trig)
    deviation = fk(g, freq, 2.2 * 3.5)
    fm = g.mul(g.mul(modulator, g.exp_env(trig, decay * 0.5, attack=0.003)), deviation)
    carrier = g.mul(g.osc("sine", freq, fm=fm, sync=trig), g.exp_env(trig, decay, attack=0.003))
    octave = g.mul(g.osc("sine", fk(g, freq, 2.0), sync=trig), g.exp_env(trig, decay * 0.5, attack=0.003))
    return amp(g, g.mix([(carrier, 1.0), (octave, 0.25)]), vel)


def stab(g, trig, freq, vel=0.5, cutoff=3200.0):
    source = g.mix([(g.osc("tri", freq, sync=trig), 0.7), (g.osc("sine", fk(g, freq, 2.0), sync=trig), 0.3)])
    return amp(g, g.mul(g.ladder(source, cutoff, 1.0), g.exp_env(trig, 0.20, attack=0.004)), vel)


def pad(g, trig_on, trig_off, freq, cutoff=1300.0, attack=0.35, release=0.6, level=0.5):
    """Two detuned saws plus a sub sine through a low-pass, gated by an ADSR (note-on / note-off triggers)."""
    source = g.mix([(g.osc("saw", fk(g, freq, 1.0035)), 0.5), (g.osc("saw", fk(g, freq, 0.9965)), 0.5), (g.osc("sine", fk(g, freq, 0.5)), 0.6)])
    env, _ = g.adsr(trig_on, trig_off, attack, 0.05, 1.0, release)
    return amp(g, g.mul(g.ladder(source, cutoff, 1.0), env), level)


def bass_day(g, trig, freq, vel=1.0):
    source = g.mix([(g.osc("sine", freq, sync=trig), 1.0), (g.osc("sine", fk(g, freq, 2.0), sync=trig), 0.35), (g.osc("sine", fk(g, freq, 3.0), sync=trig), 0.12)])
    return amp(g, amp(g, g.mul(source, g.exp_env(trig, 0.30, attack=0.004, max_len=0.6)), 0.8), vel)


def bass_night(g, trig, freq, cutoff=300.0, vel=1.0):
    source = g.mix([(g.osc("saw", freq, sync=trig), 0.8), (g.osc("sine", fk(g, freq, 0.5), sync=trig), 0.7)])
    return amp(g, g.mul(g.ladder(source, cutoff, 1.3), g.exp_env(trig, 0.10, attack=0.006, max_len=0.26)), vel)


def tom(g, trig, freq, vel=1.0):
    fm = g.mul(g.exp_env(trig, 0.05, attack=0.0005), 30.0)
    return amp(g, g.mul(g.osc("sine", freq, fm=fm, sync=trig), g.exp_env(trig, 0.18, attack=0.0005)), vel)


# ---- drums -------------------------------------------------------------------------------------------------------------------

def kick(g, trig, vel=1.0, click=0.25, sub=46.0):
    """Sine whose pitch falls from sub + 90 Hz (audio-rate FM from an AD envelope), plus a noise click."""
    fm = g.mul(g.exp_env(trig, 0.035, attack=0.0005), 90.0)
    body = g.mul(g.osc("sine", sub, fm=fm, sync=trig), g.exp_env(trig, 0.16, attack=0.0005))
    tick = g.mul(g.mul(g.noise(), g.exp_env(trig, 0.002, attack=0.0003)), click)
    return amp(g, g.add(body, tick), vel)


def snare(g, trig, vel=1.0):
    noise = g.mul(g.svf(g.noise(), 1400.0, 0.7)["hp"], g.exp_env(trig, 0.07, attack=0.0005))
    tone = g.mul(g.osc("sine", 190.0, sync=trig), g.exp_env(trig, 0.05, attack=0.0005))
    return amp(g, g.mix([(noise, 0.7), (tone, 0.5)]), vel)


def hat(g, trig, vel=1.0, decay=0.025):
    return amp(g, g.mul(g.svf(g.noise(), 7000.0, 0.8)["hp"], g.exp_env(trig, decay, attack=0.0005)), vel)


def rim(g, trig, vel=1.0):
    body = g.mix([(g.svf(g.noise(), 1800.0, 3.0)["bp"], 2.0), (g.osc("sine", 820.0, sync=trig), 0.4)])
    return amp(g, g.mul(body, g.exp_env(trig, 0.012, attack=0.0005)), vel)


# ---- generic pieces used by the sound effects --------------------------------------------------------------------------------

def tone(g, trig, kind, f0, f1, dur, attack, tau, vib_hz=0.0, vib_semi=0.0, level=1.0):
    """A pitch glide f0 -> f1 over dur seconds (exponential-looking), amplitude attack + exp decay(tau) ending at dur."""
    fm = None
    if abs(f1 - f0) > 1e-6:
        fm = g.mul(g.glide(trig, f0, f1, dur), f1 - f0)
    if vib_hz > 0.0:
        lfo = g.mul(g.osc("sine", vib_hz), 0.0578 * vib_semi * f0)   # F * ln2 / 12 * semitones, in Hz
        fm = lfo if fm is None else g.add(fm, lfo)
    osc = g.osc(kind, f0, fm=fm, sync=trig)
    return amp(g, g.mul(osc, g.exp_env(trig, tau, attack, max_len=dur)), level)


def burst(g, trig, dur, tau, band, freq, q, attack=0.001, level=1.0):
    """Filtered noise burst. band: 'lp', 'hp' or 'bp'."""
    source = g.svf(g.noise(), freq, q)[band]
    return amp(g, g.mul(source, g.exp_env(trig, tau, attack, max_len=dur)), level)


def noise_sweep(g, trig, dur, f0, f1, q, peak_at, level=1.0):
    """Noise through a band-pass whose centre moves exponentially f0 -> f1 while the amplitude rises to peak_at (0..1) and falls back."""
    u = g.ad(trig, dur, 0.001, audio=False)[0]
    cutoff = g.log_sweep(u, f0, f1)
    band = g.svf(g.noise(), cutoff, q)["bp"]
    rise = max(0.01, peak_at * dur)
    shape, _ = g.ad(trig, rise, max(0.01, dur - rise), audio=True)
    return amp(g, g.mul(g.mul(band, shape), 2.0), level)


# ---- patch wrappers ----------------------------------------------------------------------------------------------------------
# name -> (inputs [(name, type, default)], body(g, ins) -> Audio Out). Every patch has one Audio output.

PATCHES = {
    "MSP_Marimba": ([("Trigger", "Trigger", None), ("Frequency", "Float", 440.0), ("Velocity", "Float", 1.0)],
                    lambda g, i: marimba(g, i["Trigger"], i["Frequency"], i["Velocity"])),
    "MSP_Bell": ([("Trigger", "Trigger", None), ("Frequency", "Float", 440.0), ("Velocity", "Float", 1.0)],
                 lambda g, i: bell(g, i["Trigger"], i["Frequency"], i["Velocity"], 1.1)),
    "MSP_Stab": ([("Trigger", "Trigger", None), ("Frequency", "Float", 440.0), ("Velocity", "Float", 0.5)],
                 lambda g, i: stab(g, i["Trigger"], i["Frequency"], i["Velocity"], 3200.0)),
    "MSP_PadDay": ([("Gate", "Trigger", None), ("Release", "Trigger", None), ("Frequency", "Float", 261.6)],
                   lambda g, i: pad(g, i["Gate"], i["Release"], i["Frequency"], 1300.0, 0.35, 0.6)),
    "MSP_PadNightLow": ([("Gate", "Trigger", None), ("Release", "Trigger", None), ("Frequency", "Float", 220.0)],
                        lambda g, i: pad(g, i["Gate"], i["Release"], i["Frequency"], 650.0, 0.9, 1.0)),
    "MSP_PadNightHigh": ([("Gate", "Trigger", None), ("Release", "Trigger", None), ("Frequency", "Float", 440.0)],
                         lambda g, i: pad(g, i["Gate"], i["Release"], i["Frequency"], 900.0, 1.2, 1.0)),
    "MSP_BassDay": ([("Trigger", "Trigger", None), ("Frequency", "Float", 65.4), ("Velocity", "Float", 1.0)],
                    lambda g, i: bass_day(g, i["Trigger"], i["Frequency"], i["Velocity"])),
    "MSP_BassNight": ([("Trigger", "Trigger", None), ("Frequency", "Float", 55.0), ("Cutoff", "Float", 300.0), ("Velocity", "Float", 1.0)],
                      lambda g, i: bass_night(g, i["Trigger"], i["Frequency"], i["Cutoff"], i["Velocity"])),
    "MSP_Tom": ([("Trigger", "Trigger", None), ("Frequency", "Float", 80.0), ("Velocity", "Float", 1.0)],
                lambda g, i: tom(g, i["Trigger"], i["Frequency"], i["Velocity"])),
    "MSP_KickDay": ([("Trigger", "Trigger", None), ("Velocity", "Float", 1.0)],
                    lambda g, i: kick(g, i["Trigger"], i["Velocity"], 0.25, 46.0)),
    "MSP_KickNight": ([("Trigger", "Trigger", None), ("Velocity", "Float", 1.0)],
                      lambda g, i: kick(g, i["Trigger"], i["Velocity"], 0.05, 42.0)),
    "MSP_Snare": ([("Trigger", "Trigger", None), ("Velocity", "Float", 1.0)],
                  lambda g, i: snare(g, i["Trigger"], i["Velocity"])),
    "MSP_Hat": ([("Trigger", "Trigger", None), ("Velocity", "Float", 1.0)],
                lambda g, i: hat(g, i["Trigger"], i["Velocity"], 0.025)),
    "MSP_HatOpen": ([("Trigger", "Trigger", None), ("Velocity", "Float", 1.0)],
                    lambda g, i: hat(g, i["Trigger"], i["Velocity"], 0.12)),
    "MSP_Rim": ([("Trigger", "Trigger", None), ("Velocity", "Float", 1.0)],
                lambda g, i: rim(g, i["Trigger"], i["Velocity"])),
}


def build_patches():
    """Build every MSP_* patch asset; returns {name: asset}."""
    assets = {}
    for name, (inputs, body) in PATCHES.items():
        p = Graph(name, "patch")
        ins = {n: p.input(n, t, d) for n, t, d in inputs}
        p.output("Audio", "Audio", body(p, ins))
        assets[name] = p.save(name)
    return assets


class VoiceRef:
    """Instantiates a patch asset as a node inside another graph."""

    def __init__(self, asset):
        self.asset = asset

    def use(self, g, **pins):
        node = g.patch_node(self.asset)
        for pin, value in pins.items():
            g.feed(node, pin, value)
        return node.out("Audio")
