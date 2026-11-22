"""Builds all of the game's audio as MetaSound assets (no .wav, no SoundWave): 26 sound effects and the Harmonix-clocked music.

Every asset holds ONE node: a composite of the engine's own nodes that is assembled in C++ (Source/ShiftHappens/Audio/Nodes/).
  /Game/Shift/Audio/MS_Sfx_<Name>  one-shot sound effects (MetaSound Sources, one per EShiftSfx): "Shift Sfx"
  /Game/Shift/Audio/MS_Music       looping music: DayGain / NightGain / TensionGain / Volume parameters, Harmonix clock: "Shift Music"

Run: Tools\\build_audio.ps1   (removes the old MS*.uasset first; a serialised MetaSound cannot be overwritten in one editor session)
Level calibration: Tools\\verify.ps1 -Audio runs the in-graph meters and Tools\\py\\ms_calibrate.py writes Tools\\py\\ms_trims.json.
"""
import os
import sys

import unreal

sys.path.append(os.path.join(unreal.SystemLibrary.get_project_directory(), "Tools", "py"))
from shift_common import EAL, error, finish, log, stage  # noqa: E402
from ms_graph import MS_DIR  # noqa: E402
from ms_music import build_music  # noqa: E402
from ms_sfx import RECIPES, build_sfx  # noqa: E402


@stage("sound effects")
def sfx():
    built, failed = build_sfx()
    if failed:
        raise RuntimeError("sfx failed: {}".format(", ".join(failed)))


@stage("music")
def music():
    build_music()


@stage("verify")
def verify():
    expected = ["MS_Music"] + ["MS_Sfx_" + n for n in RECIPES]
    missing = [n for n in expected if not EAL.does_asset_exist("{}/{}".format(MS_DIR, n))]
    waves = []
    for path in EAL.list_assets(MS_DIR, recursive=True, include_folder=False):
        data = EAL.find_asset_data(path)
        if "SoundWave" in str(data.asset_class_path):
            waves.append(str(path))
    if missing:
        error("missing assets: {}".format(", ".join(missing)))
    if waves:
        error("SoundWave assets found (not allowed): {}".format(", ".join(waves)))
    log("{} assets expected, {} missing, {} SoundWave assets".format(len(expected), len(missing), len(waves)))


def main():
    sfx()
    music()
    verify()
    finish()


main()
