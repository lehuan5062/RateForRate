"""The 26 one-shot sound effects, each a MetaSound Source (/Game/Shift/Audio/MS_Sfx_<Name>) holding ONE node: "Shift Sfx".

The recipes live in C++ (Source/ShiftHappens/Audio/Nodes/ShiftSfxNode.cpp): a composite of the engine's own nodes (voices, mixer, reverb, limiter) that
the asset shows as a single node. This script only creates the 26 assets: the node's `Sound` pin is the effect's number (the order below is the
order of the recipes in C++ and of EShiftSfx), its `Trim dB` pin is the level trim of Tools/py/ms_trims.json (written by Tools/py/ms_calibrate.py from
the in-graph meters so each effect peaks at the level in ms_targets.py).
"""
import json
import os
import sys

import unreal

sys.path.append(os.path.join(unreal.SystemLibrary.get_project_directory(), "Tools", "py"))
from ms_graph import Graph, Node, class_name, ok  # noqa: E402
from shift_common import PROJECT_DIR, error, log  # noqa: E402

TRIMS_PATH = os.path.join(PROJECT_DIR, "Tools", "py", "ms_trims.json")   # dB per sound, from Tools/py/ms_calibrate.py (targets: ms_targets.py)

# the effects in the order of the C++ recipes (ShiftSfxNode.cpp) and of EShiftSfx; the value is the effect's number
RECIPES = {name: i for i, name in enumerate((
    "Click", "Hover", "Slider", "Upload", "Transition", "Tip", "Plug", "Flip", "Reinforce", "Weaken", "Gated", "Locked", "NoPosts", "NightOn",
    "NightOff", "Sock", "Heartbeat", "ModWarn", "ModSweep", "Strike", "Shadowban", "Blame", "Day", "Win", "Lose", "DQ"))}


def load_trims():
    try:
        with open(TRIMS_PATH, "r", encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return {}


def composite(g, ns_name_variant_major):
    """Adds the composite node of a project class to the graph."""
    ns, name, variant, major = ns_name_variant_major
    handle, result = g.b.add_node_by_class_name(class_name(ns, name, variant), major)
    if not ok(result):
        raise RuntimeError("{} :: {} is not registered (is the game module loaded?)".format(ns, name))
    g.node_count += 1
    return Node(g, handle, "{}::{}".format(ns, name))


def build_sfx(trims=None):
    """Build MS_Sfx_<Name> for every effect. Returns (built, failed)."""
    trims = load_trims() if trims is None else trims
    built, failed = 0, []
    for name, number in RECIPES.items():
        asset_name = "MS_Sfx_" + name
        try:
            g = Graph(asset_name, "source", stereo=True, one_shot=True)
            node = composite(g, ("Shift", "Sfx", "", 1))
            g.feed(node, "Play", g.on_play)
            g.feed(node, "Sound", number)
            g.feed(node, "Trim dB", float(trims.get(asset_name, 0.0)))
            g.connect(node.out("Left"), g.audio_outs[0], "left -> audio out 0")
            g.connect(node.out("Right"), g.audio_outs[1], "right -> audio out 1")
            g.connect(node.out("Finished"), g.on_finished, "OnFinished")
            g.save(asset_name)
            built += 1
        except Exception as exc:  # noqa: BLE001
            failed.append(name)
            error("sfx {} failed: {}".format(asset_name, exc))
    log("sfx built {} of {}".format(built, len(RECIPES)))
    return built, failed
