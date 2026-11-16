"""Runtime check of the MetaSound / Harmonix node keys used by the audio build (UE 5.8).

Every key is (namespace, name, variant). The 5.8 registry key format is  namespace :: name :: variant @major.
The script adds each node to a throwaway source builder, records which major version resolved and the pin names/types,
and writes Docs/metasound-nodes-5.8.md. This is the only way to enumerate nodes from Python (there is no listing API),
so the list below is the register: a node used by Tools/py/ms_*.py must appear here as FOUND.
Run: Tools\\run_py.ps1 -Script Tools\\py\\probe_metasound_nodes.py
"""
import os
import sys

import unreal

sys.path.append(os.path.join(unreal.SystemLibrary.get_project_directory(), "Tools", "py"))
from shift_common import PROJECT_DIR, finish, log, stage, warn  # noqa: E402

# (namespace, name, variant)
KEYS = [
    # oscillators / noise
    ("UE", "Sine", "Audio"), ("UE", "Saw", "Audio"), ("UE", "Square", "Audio"), ("UE", "Triangle", "Audio"),
    ("UE", "Noise", "Audio"), ("UE", "Pink Noise", "Audio"), ("UE", "LFO", ""), ("UE", "Sine", "Float"),
    # envelopes
    ("ADSR Envelope", "ADSR Envelope", "Audio"), ("ADSR Envelope", "ADSR Envelope", "Float"),
    ("AD Envelope", "AD Envelope", "Audio"), ("AD Envelope", "AD Envelope", "Float"),
    # mixing / gain / pan
    ("AudioMixer", "Audio Mixer (Mono, 2)", ""), ("AudioMixer", "Audio Mixer (Mono, 4)", ""), ("AudioMixer", "Audio Mixer (Mono, 8)", ""),
    ("AudioMixer", "Audio Mixer (Stereo, 2)", ""), ("AudioMixer", "Audio Mixer (Stereo, 4)", ""), ("AudioMixer", "Audio Mixer (Stereo, 8)", ""),
    ("UE", "Stereo Panner", ""), ("UE", "Multiply", "Audio by Float"), ("UE", "Multiply", "Audio"),
    ("UE", "Multiply", "Float"), ("UE", "Add", "Audio"), ("UE", "Add", "Float"), ("UE", "Subtract", "Float"),
    ("UE", "Subtract", "Audio"), ("UE", "Mono Mixer (2)", ""),
    # filters
    ("UE", "Ladder Filter", "Audio"), ("UE", "State Variable Filter", "Audio"), ("UE", "One-Pole Low Pass Filter", "Audio"),
    ("UE", "One-Pole High Pass Filter", "Audio"), ("UE", "Biquad Filter", "Audio"),
    # triggers / counters
    ("UE", "TriggerRepeat", ""), ("UE", "Trigger Counter", ""), ("UE", "Trigger Delay", ""), ("UE", "Trigger Sequence (2)", ""),
    ("UE", "Trigger Sequence (4)", ""), ("UE", "Trigger Select (2)", ""), ("UE", "Trigger Select (4)", ""),
    ("TriggerCompare", "Clamp", "Int32"), ("TriggerCompare", "Clamp", "Float"), ("UE", "Trigger Route (2)", ""),
    ("UE", "Trigger Any (2)", ""), ("UE", "Trigger Any (4)", ""), ("UE", "Trigger Once", ""), ("UE", "Trigger Toggle", ""),
    ("UE", "Trigger On Threshold", ""), ("UE", "Trigger Filter", ""), ("UE", "Trigger Accumulate", ""), ("UE", "Trigger Pipe", ""),
    ("UE", "Trigger Control", ""), ("UE", "Trigger On Value", ""),
    # arrays
    ("Array", "Get", "Float:Array"), ("Array", "Get", "Int32:Array"), ("Array", "Num", "Float:Array"), ("Array", "Num", "Int32:Array"),
    ("Array", "Random Get", "Float:Array"), ("Array", "Random Get", "Int32:Array"), ("Array", "Set", "Float:Array"),
    # math / conversion
    ("UE", "InterpTo", "Audio"), ("UE", "InterpTo", "Float"), ("MapRange", "MapRange", "Float"), ("MapRange", "MapRange", "Audio"),
    ("Clamp", "Clamp", "Float"), ("Clamp", "Clamp", "Audio"), ("Clamp", "Clamp", "Int32"),
    ("UE", "BPMToSeconds", ""), ("UE", "MIDI To Frequency", "Float"), ("UE", "MIDI To Frequency", "Int32"),
    ("UE", "ConversionFloatToAudio", ""), ("UE", "Float To Audio", ""), ("UE", "Int To Float", ""), ("UE", "Float To Int", ""),
    ("UE", "Modulo", "Int32"), ("UE", "Modulo", "Float"), ("UE", "Wrap", "Float"), ("UE", "Wrap", "Int32"),
    ("UE", "Semitone to Frequency Multiplier", ""), ("UE", "Semitones to Frequency Multiplier", ""), ("UE", "Frequency To Semitone", ""),
    ("UE", "Linear To Log Frequency", ""), ("UE", "Log To Linear", ""), ("UE", "Random", "Float"), ("UE", "Random Float", ""),
    ("UE", "Exp", "Float"), ("UE", "Pow", "Float"), ("UE", "Divide", "Float"), ("UE", "Min", "Float"), ("UE", "Max", "Float"),
    ("UE", "Abs", "Float"), ("UE", "Negate", "Float"), ("UE", "Float To Time", ""), ("UE", "Time To Float", ""),
    ("UE", "Greater", "Int32"), ("UE", "Equal", "Int32"), ("UE", "Int32 To Bool", ""), ("UE", "Select", "Float"),
    ("UE", "Audio To Float", ""), ("UE", "Audio Buffer To Float", ""), ("UE", "Mean", "Float"),
    # effects / dynamics
    ("UE", "Plate Reverb", "Stereo"), ("UE", "Delay", "Audio"), ("UE", "Stereo Delay", "Audio"), ("UE", "WaveShaper", "Audio"),
    ("UE", "Bitcrusher", "Audio"), ("UE", "RingMod", "Audio"), ("UE", "Limiter", "Audio"), ("UE", "Compressor", "Audio"),
    ("UE", "Distortion", "Audio"), ("UE", "Flanger", "Audio"), ("UE", "Chorus", "Audio"), ("UE", "Phaser", "Audio"),
    ("UE", "Gain", "Audio"), ("UE", "Mono To Stereo", ""), ("UE", "Make Array", ""),
    # metering / analysis
    ("UE", "Envelope Follower", ""), ("UE", "BandSplitter", ""), ("UE", "Band Splitter", ""), ("UE", "Audio Peak", ""),
    ("UE", "Audio To Peak", ""), ("UE", "Level Meter", ""), ("UE", "Spectrum Analyzer", ""),
    # I/O we avoid, listed only to document that they exist
    ("UE", "Wave Player", "Mono"), ("UE", "Wave Player", "Stereo"), ("UE", "Wave Writer", "Mono"),
    # Harmonix
    ("HarmonixNodes", "Metronome", ""), ("HarmonixNodes", "TriggerToTransport", ""), ("HarmonixNodes", "MidiClockSubdivisionTrigger", ""),
    ("HarmonixNodes", "MidiClockTimestampTrigger", ""), ("HarmonixNodes", "MidiClockState", ""), ("HarmonixNodes", "MIDIPlayer", ""),
    ("HarmonixNodes", "StepSequencePlayer", ""), ("HarmonixNodes", "MidiNoteTrigger", ""), ("HarmonixNodes", "MidiClockSongPos", ""),
    ("HarmonixNodes", "MidiClockToSeconds", ""), ("HarmonixNodes", "BarBeatTrigger", ""),
]

MAJORS = (1, 0, 2, 3)

OUT_DOC = os.path.join(PROJECT_DIR, "Docs", "metasound-nodes-5.8.md")


def sub():
    return unreal.get_engine_subsystem(unreal.MetaSoundBuilderSubsystem)


def class_name(ns, name, variant):
    cn = unreal.MetasoundFrontendClassName()
    cn.set_editor_property("namespace", ns)
    cn.set_editor_property("name", name)
    cn.set_editor_property("variant", variant)
    return cn


def pins(builder, node):
    ins, _ = builder.find_node_inputs(node)
    outs, _ = builder.find_node_outputs(node)
    in_txt = []
    for h in ins:
        pin_name, data_type, _res = builder.get_node_input_data(h)
        in_txt.append("{}:{}".format(pin_name, data_type))
    out_txt = []
    for h in outs:
        pin_name, data_type, _res = builder.get_node_output_data(h)
        out_txt.append("{}:{}".format(pin_name, data_type))
    return in_txt, out_txt


@stage("probe nodes")
def probe():
    res = sub().create_source_builder("ProbeNodes", unreal.MetaSoundOutputAudioFormat.STEREO, True)
    builder = res[0]
    rows = []
    for ns, name, variant in KEYS:
        found = None
        for major in MAJORS:
            try:
                node, result = builder.add_node_by_class_name(class_name(ns, name, variant), major)
            except Exception as exc:  # noqa: BLE001
                continue
            if result == unreal.MetaSoundBuilderResult.SUCCEEDED:
                found = (major, node)
                break
        key = "{} :: {} :: {}".format(ns, name, variant)
        if found is None:
            rows.append((key, None, [], []))
            log("MISSING {}".format(key))
        else:
            ins, outs = pins(builder, found[1])
            rows.append((key, found[0], ins, outs))
            log("FOUND   {} @{}  in[{}]  out[{}]".format(key, found[0], ", ".join(ins), ", ".join(outs)))
    return rows


def write_doc(rows):
    found = [r for r in rows if r[1] is not None]
    missing = [r for r in rows if r[1] is None]
    lines = [
        "# MetaSound / Harmonix node register (UE 5.8, runtime-probed)",
        "",
        "Generated by `Tools/py/probe_metasound_nodes.py` (do not edit by hand). Keys are `namespace :: name :: variant`; the number is the major version that resolved.",
        "A node that is not listed as FOUND here must not be used by `Tools/py/ms_*.py`.",
        "",
        "## Found ({})".format(len(found)),
        "",
        "| Key | Major | Inputs | Outputs |",
        "|---|---|---|---|",
    ]
    for key, major, ins, outs in found:
        lines.append("| `{}` | {} | {} | {} |".format(key, major, "<br>".join("`{}`".format(i) for i in ins), "<br>".join("`{}`".format(o) for o in outs)))
    lines += ["", "## Not found ({})".format(len(missing)), ""]
    for key, _m, _i, _o in missing:
        lines.append("- `{}`".format(key))
    os.makedirs(os.path.dirname(OUT_DOC), exist_ok=True)
    with open(OUT_DOC, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")
    log("wrote {} ({} found, {} missing)".format(OUT_DOC, len(found), len(missing)))


def main():
    rows = probe()
    if rows:
        write_doc(rows)
    finish()


main()
