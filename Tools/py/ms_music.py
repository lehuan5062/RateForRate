"""MS_Music: the looping game music as one MetaSound Source holding ONE node, "Shift Music", clocked by Harmonix.

The graph lives in C++ (Source/ShiftHappens/Audio/Nodes/ShiftMusicNode.cpp): the Harmonix clock (Trigger To Transport -> Metronome 112.5 BPM, 4/4,
8-bar loop -> 16th-note subdivision), the pattern lanes, the voices, the day / night / tension layers, the layer mixer, the plate reverb, the master
volume and a limiter, all of them the engine's own nodes assembled by the composite node. The asset only wires its interface:
  inputs   DayGain, NightGain, TensionGain, Volume (floats, set by UShiftAudio), On Play
  outputs  Out Left, Out Right, and "MIDI Clock" (UShiftAudio binds a UMusicClockComponent to it for the beat pulse)
The level trims of the three layers (Tools/py/ms_trims.json) are default values of the node's "... Trim dB" pins.
"""
import os
import sys

import unreal

sys.path.append(os.path.join(unreal.SystemLibrary.get_project_directory(), "Tools", "py"))
from ms_graph import Graph  # noqa: E402
from ms_sfx import composite, load_trims  # noqa: E402
from shift_common import log  # noqa: E402


def build_music():
    trims = load_trims()
    g = Graph("MS_Music", "source", stereo=True, one_shot=False)
    day_gain = g.input("DayGain", "Float", 1.0)
    night_gain = g.input("NightGain", "Float", 0.0)
    tension_gain = g.input("TensionGain", "Float", 0.0)
    volume = g.input("Volume", "Float", 1.0)
    node = composite(g, ("Shift", "Music", "", 1))
    g.feed(node, "Play", g.on_play)
    g.feed(node, "DayGain", day_gain)
    g.feed(node, "NightGain", night_gain)
    g.feed(node, "TensionGain", tension_gain)
    g.feed(node, "Volume", volume)
    g.feed(node, "Day Trim dB", float(trims.get("music_day", 0.0)))
    g.feed(node, "Night Trim dB", float(trims.get("music_night", 0.0)))
    g.feed(node, "Tension Trim dB", float(trims.get("music_tension", 0.0)))
    g.connect(node.out("Left"), g.audio_outs[0], "left -> audio out 0")
    g.connect(node.out("Right"), g.audio_outs[1], "right -> audio out 1")
    g.output("MIDI Clock", "MIDIClock", node.out("MIDI Clock"))
    asset = g.save("MS_Music")
    log("MS_Music built (one Shift Music node)")
    return asset
