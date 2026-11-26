"""MetaSound graph-building helpers for Rate for Rate (UE 5.8 Builder API, Python).

Everything audible in the game is a MetaSound Source or Patch built with these helpers (no .wav, no SoundWave assets, no sample
players). Node keys are the ones registered by `Tools/py/probe_metasound_nodes.py` (see Docs/metasound-nodes-5.8.md).

Conventions
* `Out` wraps a node output handle (or a graph input) and knows its data type; `Graph.feed(node, pin, value)` connects an `Out` or
  sets a literal default.
* Float / Time pins and Float envelopes run at block rate (default 100 Hz), so anything that must move faster than that (pitch sweeps,
  FM) goes through an Audio-typed envelope into an oscillator's `Modulation` input (per-sample Hz offset).
* Exponential decays are approximated with the AD envelope's `Decay Curve` (value = 1 - fraction ** curve): D = 5 * tau, curve = 0.25.
"""
import math
import os
import sys

import unreal

sys.path.append(os.path.join(unreal.SystemLibrary.get_project_directory(), "Tools", "py"))
from shift_common import ASSET_ROOT, EAL, ensure_dir, log  # noqa: E402

MS_DIR = ASSET_ROOT + "/Audio"
AUTHOR = "Shift Happens"
OK = unreal.MetaSoundBuilderResult.SUCCEEDED

# (namespace, name, variant, major)
KEYS = {
    "sine": ("UE", "Sine", "Audio", 1), "saw": ("UE", "Saw", "Audio", 1), "square": ("UE", "Square", "Audio", 1),
    "tri": ("UE", "Triangle", "Audio", 1), "noise": ("UE", "Noise", "Audio", 1),
    "adsr": ("ADSR Envelope", "ADSR Envelope", "Audio", 1), "adsr_f": ("ADSR Envelope", "ADSR Envelope", "Float", 1),
    "ad": ("AD Envelope", "AD Envelope", "Audio", 1), "ad_f": ("AD Envelope", "AD Envelope", "Float", 1),
    "mixer_m": ("AudioMixer", "Audio Mixer (Mono, {n})", "", 1), "mixer_s": ("AudioMixer", "Audio Mixer (Stereo, {n})", "", 1),
    "panner": ("UE", "Stereo Panner", "", 1),
    "mul_a": ("UE", "Multiply", "Audio", 1), "mul_af": ("UE", "Multiply", "Audio by Float", 1), "mul_f": ("UE", "Multiply", "Float", 1),
    "add_a": ("UE", "Add", "Audio", 1), "add_f": ("UE", "Add", "Float", 1), "sub_f": ("UE", "Subtract", "Float", 1),
    "div_f": ("UE", "Divide", "Float", 1),
    "ladder": ("UE", "Ladder Filter", "Audio", 1), "svf": ("UE", "State Variable Filter", "Audio", 1),
    "lp1": ("UE", "One-Pole Low Pass Filter", "Audio", 1), "hp1": ("UE", "One-Pole High Pass Filter", "Audio", 1),
    "biquad": ("UE", "Biquad Filter", "Audio", 2),
    "repeat": ("UE", "TriggerRepeat", "", 1), "counter": ("UE", "Trigger Counter", "", 1), "delay_t": ("UE", "Trigger Delay", "", 1),
    "seq": ("UE", "Trigger Sequence ({n})", "", 1), "select": ("UE", "Trigger Select ({n})", "", 1),
    "cmp_i": ("TriggerCompare", "Clamp", "Int32", 1), "cmp_f": ("TriggerCompare", "Clamp", "Float", 1),
    "get_f": ("Array", "Get", "Float:Array", 1), "get_i": ("Array", "Get", "Int32:Array", 1),
    "modulo": ("UE", "Modulo", "Int32", 1), "clamp_f": ("Clamp", "Clamp", "Float", 1),
    "map_f": ("MapRange", "MapRange", "Float", 1), "interp": ("UE", "InterpTo", "Audio", 1),
    "bpm": ("UE", "BPMToSeconds", "", 1), "midi_f": ("UE", "MIDI To Frequency", "Float", 1), "midi_i": ("UE", "MIDI To Frequency", "Int32", 1),
    "f2a": ("UE", "ConversionFloatToAudio", "", 1), "lin2log": ("UE", "Linear To Log Frequency", "", 1),
    "reverb": ("UE", "Plate Reverb", "Stereo", 1), "delay_a": ("UE", "Delay", "Audio", 1), "limiter": ("UE", "Limiter", "Audio", 1),
    "compressor": ("UE", "Compressor", "Audio", 1), "shaper": ("UE", "WaveShaper", "Audio", 1), "crusher": ("UE", "Bitcrusher", "Audio", 1),
    "follower": ("UE", "Envelope Follower", "", 1),
    "metronome": ("HarmonixNodes", "Metronome", "", 0), "to_transport": ("HarmonixNodes", "TriggerToTransport", "", 0),
    "subdiv": ("HarmonixNodes", "MidiClockSubdivisionTrigger", "", 0), "clock_ts": ("HarmonixNodes", "MidiClockTimestampTrigger", "", 1),
    "clock_state": ("HarmonixNodes", "MidiClockState", "", 0),
}

# Integer values of the enum pins we set (MetaSound enum literals are the enum's integer value).
ENUM_COMPARE = {"eq": 0, "ne": 1, "lt": 2, "gt": 3, "le": 4, "ge": 5}   # TriggerComparisonType
PEAK_MODE = {"peak": 0, "ms": 1, "rms": 2}                              # EnvelopePeakMode
GRID_16TH = 3                                                           # EMidiClockSubdivisionQuantization::SixteenthNote
NOISE_WHITE = 1                                                         # ENoiseType: Pink = 0 (the default), White = 1

STEP_SECONDS = 60.0 / 112.5 / 4.0   # one 16th note at 112.5 BPM


def sub():
    return unreal.get_engine_subsystem(unreal.MetaSoundBuilderSubsystem)


def editor_sub():
    return unreal.get_editor_subsystem(unreal.MetaSoundEditorSubsystem)


def ok(result):
    return result == OK


def class_name(ns, name, variant):
    cn = unreal.MetasoundFrontendClassName()
    cn.set_editor_property("namespace", ns)
    cn.set_editor_property("name", name)
    cn.set_editor_property("variant", variant)
    return cn


def literal(dtype, value):
    """Literal for a pin of the given MetaSound data type."""
    s = sub()
    if dtype in ("Float", "Time"):
        return s.create_float_meta_sound_literal(float(value))[0]
    if dtype == "Int32" or dtype.startswith("Enum:"):
        return s.create_int_meta_sound_literal(int(value))[0]
    if dtype in ("Bool", "Trigger"):
        return s.create_bool_meta_sound_literal(bool(value))[0]
    if dtype == "Float:Array":
        return s.create_float_array_meta_sound_literal([float(v) for v in value])[0]
    if dtype == "Int32:Array":
        return s.create_int_array_meta_sound_literal([int(v) for v in value])[0]
    raise RuntimeError("no literal for data type {}".format(dtype))


def empty_literal():
    return unreal.MetasoundFrontendLiteral()


class Out:
    """A signal: an output handle plus its data type ("Audio", "Float", "Trigger", "Int32", ...)."""

    def __init__(self, graph, handle, dtype):
        self.g = graph
        self.h = handle
        self.t = dtype

    def __mul__(self, other):
        return self.g.mul(self, other)

    __rmul__ = __mul__

    def __add__(self, other):
        return self.g.add(self, other)

    __radd__ = __add__


class Node:
    def __init__(self, graph, handle, key):
        self.g = graph
        self.h = handle
        self.key = key

    def out(self, pin):
        return self.g.pin_out(self, pin)


class Graph:
    """Wraps a MetaSound source or patch builder; every call raises with a readable message when it fails."""

    def __init__(self, name, kind="source", stereo=True, one_shot=True):
        self.name = name
        self.kind = kind
        self.on_play = None
        self.on_finished = None
        self.audio_outs = []
        self.node_count = 0   # nodes added (graph inputs and outputs are not counted); logged when the asset is saved
        if kind == "source":
            fmt = unreal.MetaSoundOutputAudioFormat.STEREO if stereo else unreal.MetaSoundOutputAudioFormat.MONO
            res = sub().create_source_builder(name, fmt, one_shot)
            builder, on_play, on_finished, outs, result = res
            if not ok(result):
                raise RuntimeError("create_source_builder failed for {}".format(name))
            self.b = builder
            self.on_play = Out(self, on_play, "Trigger")
            self.on_finished = on_finished
            self.audio_outs = list(outs)
        else:
            res = sub().create_patch_builder(name)
            builder, result = res
            if not ok(result):
                raise RuntimeError("create_patch_builder failed for {}".format(name))
            self.b = builder
        self._seed = 1000 + (sum(ord(c) for c in name) % 5000)

    # ---- raw access -----------------------------------------------------------------------------------------------------
    def node(self, key, n=None, major=None):
        ns, name, variant, default_major = KEYS[key]
        if n is not None:
            name = name.format(n=n)
            variant = variant.format(n=n)
        handle, result = self.b.add_node_by_class_name(class_name(ns, name, variant), default_major if major is None else major)
        if not ok(result):
            raise RuntimeError("add_node failed: {} :: {} :: {} (@{})".format(ns, name, variant, default_major))
        self.node_count += 1
        return Node(self, handle, "{}::{}::{}".format(ns, name, variant))

    def patch_node(self, patch_asset):
        handle, result = self.b.add_node(patch_asset)
        if not ok(result):
            raise RuntimeError("add_node(patch {}) failed".format(patch_asset.get_name()))
        self.node_count += 1
        return Node(self, handle, "patch:" + patch_asset.get_name())

    def pin_in(self, node, pin):
        handle, result = self.b.find_node_input_by_name(node.h, pin)
        if not ok(result):
            raise RuntimeError("{} has no input pin {!r}".format(node.key, pin))
        return handle

    def pin_out(self, node, pin):
        handle, result = self.b.find_node_output_by_name(node.h, pin)
        if not ok(result):
            raise RuntimeError("{} has no output pin {!r}".format(node.key, pin))
        _name, dtype, _res = self.b.get_node_output_data(handle)
        return Out(self, handle, str(dtype))

    def input_type(self, ih):
        _name, dtype, _res = self.b.get_node_input_data(ih)
        return str(dtype)

    def feed(self, node, pin, value):
        """Connect an Out to the pin, or set a literal default when `value` is a Python number / list."""
        ih = self.pin_in(node, pin)
        if isinstance(value, Out):
            if not ok(self.b.connect_nodes(value.h, ih)):
                raise RuntimeError("connect failed: {} -> {}.{}".format(value.t, node.key, pin))
        else:
            dtype = self.input_type(ih)
            if not ok(self.b.set_node_input_default(ih, literal(dtype, value))):
                raise RuntimeError("could not set {}.{} ({}) to {!r}".format(node.key, pin, dtype, value))

    def feed_all(self, node, pins):
        for pin, value in pins.items():
            if value is not None:
                self.feed(node, pin, value)

    def make(self, key, pins=None, n=None, major=None):
        node = self.node(key, n=n, major=major)
        if pins:
            self.feed_all(node, pins)
        return node

    # ---- graph IO -------------------------------------------------------------------------------------------------------
    @staticmethod
    def _default_literal(dtype, default):
        if dtype in ("Audio", "MIDIClock"):
            return empty_literal()
        if dtype in ("Trigger", "Bool"):
            return literal(dtype, bool(default))
        return literal(dtype, 0 if default is None else default)

    def input(self, name, dtype, default=None):
        handle, result = self.b.add_graph_input_node(name, dtype, self._default_literal(dtype, default), False)
        if not ok(result):
            raise RuntimeError("add_graph_input_node {} ({}) failed".format(name, dtype))
        return Out(self, handle, dtype)

    def output(self, name, dtype, value=None):
        """Add a graph output and (optionally) connect `value` (an Out) to it."""
        handle, result = self.b.add_graph_output_node(name, dtype, self._default_literal(dtype, None), False)
        if not ok(result):
            raise RuntimeError("add_graph_output_node {} ({}) failed".format(name, dtype))
        if value is not None:
            if not ok(self.b.connect_nodes(value.h, handle)):
                raise RuntimeError("connect to graph output {} failed".format(name))
        return handle

    def connect(self, out, in_handle, what=""):
        if not ok(self.b.connect_nodes(out.h, in_handle)):
            raise RuntimeError("connect failed {}".format(what))

    # ---- DSP blocks (all return Out) ------------------------------------------------------------------------------------
    def seed(self):
        self._seed += 7919
        return self._seed

    def noise(self):
        """White noise with a fixed seed (the node defaults to pink noise, which has far less energy above a few kHz)."""
        return self.make("noise", {"Seed": self.seed(), "Type": NOISE_WHITE}).out("Audio")

    def osc(self, kind, freq, fm=None, sync=None, glide=None, extra=None):
        pins = {"Frequency": freq, "Modulation": fm, "Sync": sync, "Glide": glide}
        if extra:
            pins.update(extra)
        return self.make(kind, pins).out("Audio")

    def ad(self, trig, attack, decay, curve_a=1.0, curve_d=1.0, audio=True, looping=False):
        """AD envelope: returns (envelope, on_done trigger). Times in seconds."""
        node = self.make("ad" if audio else "ad_f", {"Trigger": trig, "Attack Time": attack, "Decay Time": decay,
                                                      "Attack Curve": curve_a, "Decay Curve": curve_d, "Looping": True if looping else None})
        return node.out("Out Envelope"), node.out("On Done")

    def exp_env(self, trig, tau, attack=0.002, audio=True, max_len=None):
        """Percussive envelope: linear attack, then a decay shaped like exp(-t / tau) that ends after 5 tau (or at max_len seconds).

        The AD envelope decays as 1 - fraction ** curve; the curve is fitted so that it passes through exp(-k * 0.3) at 30 % of the
        decay (k = decay length / tau), which tracks an exponential closely for k between 1 and 6.
        """
        length = 5.0 * tau
        if max_len is not None:
            length = min(length, max(0.01, max_len - attack))
        length = max(0.01, length)
        k = length / tau
        curve = math.log(max(1e-6, 1.0 - math.exp(-0.3 * k))) / math.log(0.3)
        return self.ad(trig, attack, length, curve_a=1.0, curve_d=min(3.0, max(0.15, curve)), audio=audio)[0]

    def glide(self, trig, f0, f1, dur, audio=True):
        """Exponential-looking pitch offset: returns an envelope (Out) that goes 0 -> 1 over dur seconds shaped so that
        f0 + (f1 - f0) * envelope follows f0 * (f1 / f0) ** (t / dur) at its midpoint."""
        ratio = max(1e-3, f1 / f0)
        curve = math.log(math.sqrt(ratio) + 1.0, 2.0)
        return self.ad(trig, dur, 0.001, curve_a=min(3.0, max(0.2, curve)), curve_d=1.0, audio=audio)[0]

    def adsr(self, trig_on, trig_off, attack, decay, sustain, release, audio=True):
        node = self.make("adsr" if audio else "adsr_f", {"Trigger Attack": trig_on, "Trigger Release": trig_off, "Attack Time": attack,
                                                           "Decay Time": decay, "Sustain Level": sustain, "Release Time": release,
                                                           "Hard Reset": True})
        return node.out("Out Envelope"), node.out("On Done")

    def mul(self, a, b):
        """Audio*Audio, Audio*Float (Out or number), Float*Float."""
        if a.t == "Audio":
            if isinstance(b, Out) and b.t == "Audio":
                node = self.make("mul_a", {"PrimaryOperand": a, "AdditionalOperands": b})
            else:
                node = self.make("mul_af", {"PrimaryOperand": a, "AdditionalOperands": b})
        else:
            node = self.make("mul_f", {"PrimaryOperand": a, "AdditionalOperands": b})
        return node.out("Out")

    def add(self, a, b):
        if a.t == "Audio":
            return self.make("add_a", {"PrimaryOperand": a, "AdditionalOperands": b}).out("Out")
        return self.make("add_f", {"PrimaryOperand": a, "AdditionalOperands": b}).out("Out")

    def sub_f(self, a, b):
        return self.make("sub_f", {"PrimaryOperand": a, "AdditionalOperands": b}).out("Out")

    def fscale(self, a, k):
        """Float Out times a constant."""
        return self.make("mul_f", {"PrimaryOperand": a, "AdditionalOperands": float(k)}).out("Out")

    def mix(self, items):
        """Sum of (audio, gain) pairs through Audio Mixer (Mono, N); more than 8 inputs are summed in groups."""
        items = [(sig, 1.0 if gain is None else gain) for sig, gain in items]
        if len(items) == 1:
            sig, gain = items[0]
            return sig if gain == 1.0 else self.mul(sig, gain)
        if len(items) > 8:
            groups = [self.mix(items[i:i + 8]) for i in range(0, len(items), 8)]
            return self.mix([(g_, 1.0) for g_ in groups])
        node = self.node("mixer_m", n=max(2, len(items)))
        for i, (sig, gain) in enumerate(items):
            self.feed(node, "In {}".format(i), sig)
            self.feed(node, "Gain {}".format(i), gain)
        return node.out("Out")

    def mix_stereo(self, items):
        """Sum of ((l, r), gain) pairs through Audio Mixer (Stereo, N); returns (l, r)."""
        items = [(pair, 1.0 if gain is None else gain) for pair, gain in items]
        if len(items) == 1:
            return items[0][0]
        if len(items) > 8:
            groups = [self.mix_stereo(items[i:i + 8]) for i in range(0, len(items), 8)]
            return self.mix_stereo([(p, 1.0) for p in groups])
        node = self.node("mixer_s", n=max(2, len(items)))
        for i, ((left, right), gain) in enumerate(items):
            self.feed(node, "In {} L".format(i), left)
            self.feed(node, "In {} R".format(i), right)
            self.feed(node, "Gain {}".format(i), gain)
        return node.out("Out L"), node.out("Out R")

    def pan(self, sig, amount):
        node = self.make("panner", {"In": sig, "Pan Amount": amount})
        return node.out("Out Left"), node.out("Out Right")

    def svf(self, sig, cutoff, q=0.7):
        """State variable filter; q is a conventional filter Q (the node's Resonance pin maps 1..10 onto Q 0.5..25)."""
        resonance = min(10.0, max(1.0, 1.0 + 9.0 * (q - 0.5) / 24.5))
        node = self.make("svf", {"In": sig, "Cutoff Frequency": cutoff, "Resonance": resonance})
        return {"lp": node.out("Low Pass Filter"), "hp": node.out("High Pass Filter"), "bp": node.out("Band Pass"), "bs": node.out("Band Stop")}

    def ladder(self, sig, cutoff, resonance=1.0):
        """4-pole low-pass; Resonance is clamped to 1..10 by the node."""
        return self.make("ladder", {"In": sig, "Cutoff Frequency": cutoff, "Resonance": resonance}).out("Out")

    def log_sweep(self, u, f0, f1):
        """Float envelope u (0 -> 1) to an exponential frequency sweep from f0 to f1 (Float Out)."""
        if f1 >= f0:
            return self.make("lin2log", {"Value": u, "Min Domain": 0.0, "Max Domain": 1.0, "Min Range": f0, "Max Range": f1}).out("Frequency")
        inverted = self.make("sub_f", {"PrimaryOperand": 1.0, "AdditionalOperands": u}).out("Out")
        return self.make("lin2log", {"Value": inverted, "Min Domain": 0.0, "Max Domain": 1.0, "Min Range": f1, "Max Range": f0}).out("Frequency")

    def lp1(self, sig, cutoff):
        return self.make("lp1", {"In": sig, "Cutoff Frequency": cutoff}).out("Out")

    def hp1(self, sig, cutoff):
        return self.make("hp1", {"In": sig, "Cutoff Frequency": cutoff}).out("Out")

    def midi_hz(self, note):
        """Float MIDI note (Out or number) to frequency in Hz (Out, Float)."""
        return self.make("midi_f", {"MIDI In": note}).out("Out Frequency")

    def delay_trigger(self, trig, seconds):
        return self.make("delay_t", {"In": trig, "Delay Time": seconds}).out("Out")

    def reverb(self, left, right, wet=0.15, dry=1.0, decay=0.7, damp=0.5):
        node = self.make("reverb", {"In Left": left, "In Right": right, "Dry Level": dry, "Wet Level": wet, "Decay": decay, "Dampening": damp})
        return node.out("Out Left"), node.out("Out Right")

    def limiter(self, sig, threshold_db=-1.0, release=0.05):
        return self.make("limiter", {"Audio": sig, "Threshold dB": threshold_db, "Release Time": release}).out("Audio")

    def follower(self, sig, mode="rms", attack=0.005, release=0.15):
        node = self.make("follower", {"In": sig, "Attack Time": attack, "Release Time": release, "Peak Mode": PEAK_MODE[mode]})
        return node.out("Envelope")

    # ---- completion -----------------------------------------------------------------------------------------------------
    def stereo_out(self, left, right):
        self.connect(left, self.audio_outs[0], "left -> audio out 0")
        self.connect(right, self.audio_outs[1], "right -> audio out 1")

    def mono_to_stereo_out(self, sig):
        self.stereo_out(sig, sig)

    def finish_after(self, seconds, trig=None):
        """One-shot lifetime: OnFinished fires `seconds` after `trig` (default OnPlay)."""
        done = self.delay_trigger(trig if trig is not None else self.on_play, seconds)
        self.connect(done, self.on_finished, "OnFinished")

    def save(self, asset_name):
        """Write the builder to /Game/Shift/Audio/<asset_name> (serialised assets need the editor subsystem)."""
        ensure_dir(MS_DIR)
        path = "{}/{}".format(MS_DIR, asset_name)
        if EAL.does_asset_exist(path):
            # An existing serialised MetaSound cannot be overwritten in the same editor session: Tools\build_audio.ps1 deletes the
            # old MS_*/MSP_* files before the editor starts.
            raise RuntimeError("{} already exists (run Tools\\build_audio.ps1, which clears the old assets first)".format(path))
        asset, result = editor_sub().build_to_asset(self.b, AUTHOR, asset_name, MS_DIR)
        if not ok(result) or asset is None:
            raise RuntimeError("build_to_asset failed for {}".format(asset_name))
        EAL.save_loaded_asset(asset)
        log("{}: {} nodes".format(asset_name, self.node_count))
        return asset
