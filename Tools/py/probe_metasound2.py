"""S4 probe 2: which MetaSound standard nodes exist (class name / variant) and what are their pin names?

Tries candidate (name, variant) pairs in namespace 'UE' on a throwaway source builder and prints every node that exists with its input and
output pins. Also lists the audio-format enum and checks the build/overwrite path with a tiny graph.
Run: Tools\\run_py.ps1 -Script Tools\\py\\probe_metasound2.py
"""
import os
import sys

import unreal

sys.path.append(os.path.join(unreal.SystemLibrary.get_project_directory(), "Tools", "py"))
from shift_common import finish, log, stage, warn  # noqa: E402

CANDIDATES = [
    ("Wave Player", "Mono"), ("Wave Player", "Stereo"), ("Wave Player", "Mono 1.0"), ("Wave Player", ""),
    ("Multiply", "Audio by Float"), ("Multiply", "Audio by Audio"), ("Multiply", "Float"), ("Multiply", "Audio"),
    ("Add", "Audio"), ("Add", "Float"),
    ("Mono Mixer (2)", ""), ("Mono Mixer (2)", "Audio"), ("Mono Mixer (4)", ""), ("Mono Mixer (4)", "Audio"),
    ("Stereo Mixer (2)", ""), ("Stereo Mixer (2)", "Audio"), ("Stereo Mixer (4)", ""), ("Stereo Mixer (4)", "Audio"),
    ("Mixer (Stereo, 2)", ""), ("Mixer (Stereo, 4)", ""),
    ("Interpolate To", "Float"), ("InterpTo", "Float"), ("Interpolate To (Float)", ""),
    ("Trigger Repeat", ""), ("Trigger Counter", ""), ("Trigger Delay", ""),
    ("AD Envelope", "Audio"), ("AD Envelope", "Float"), ("ADSR Envelope", "Audio"),
    ("Sine", "Audio"), ("Saw", "Audio"), ("Square", "Audio"), ("Triangle", "Audio"), ("Noise", "Audio"),
    ("Biquad Filter", "Audio"), ("One-Pole Low Pass Filter", "Audio"), ("State Variable Filter", "Audio"),
    ("Delay", "Audio"), ("Stereo Delay", "Audio"), ("Plate Reverb", "Stereo"),
    ("Mono To Stereo", "Audio"), ("Stereo To Mono", "Audio"), ("Audio To Mono", ""), ("Pan", "Audio"), ("Stereo Panner", "Audio"),
    ("Linear To Log Frequency", "Float"), ("Semitones to Frequency Multiplier", "Float"), ("MIDI To Frequency", "Float"),
    ("Compressor", "Audio"), ("Limiter", "Audio"), ("Gain", "Audio"), ("Clamp", "Float"), ("Map Range", "Float"),
    ("Float To Audio", ""), ("Audio To Float", ""), ("Random Get", "Float"), ("Random", "Float"),
    ("Wave Player", "Stereo 1.0"), ("WavePlayer", "Mono"),
]


def sub():
    return unreal.get_engine_subsystem(unreal.MetaSoundBuilderSubsystem)


def new_builder(name):
    result = sub().create_source_builder(name)
    return result[0]


def describe_pins(builder, node):
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
    return "in[{}] out[{}]".format(", ".join(in_txt), ", ".join(out_txt))


@stage("enums")
def enums():
    fmt = unreal.MetaSoundOutputAudioFormat
    log("MetaSoundOutputAudioFormat: {}".format([n for n in dir(fmt) if n.isupper()]))
    res = unreal.MetaSoundBuilderResult
    log("MetaSoundBuilderResult: {}".format([n for n in dir(res) if n.isupper()]))
    cn = unreal.MetasoundFrontendClassName()
    log("MetasoundFrontendClassName fields: {}".format([n for n in dir(cn) if not n.startswith("_")][:30]))
    opt = unreal.MetaSoundBuilderOptions()
    log("MetaSoundBuilderOptions fields: {}".format([n for n in dir(opt) if not n.startswith("_")][:30]))


@stage("candidate nodes")
def candidates():
    builder = new_builder("ProbeBuilder")
    found = 0
    for name, variant in CANDIDATES:
        cn = unreal.MetasoundFrontendClassName()
        cn.set_editor_property("namespace", "UE")
        cn.set_editor_property("name", name)
        cn.set_editor_property("variant", variant)
        try:
            node, result = builder.add_node_by_class_name(cn, 1)
        except Exception as exc:  # noqa: BLE001
            log("  {!r} / {!r}: exception {}".format(name, variant, exc))
            continue
        if result == unreal.MetaSoundBuilderResult.SUCCEEDED:
            found += 1
            log("FOUND UE.{}.{} -> {}".format(name, variant, describe_pins(builder, node)))
        else:
            log("  missing UE.{}.{}".format(name, variant))
    log("found {} of {}".format(found, len(CANDIDATES)))


def main():
    enums()
    candidates()
    finish()


main()
