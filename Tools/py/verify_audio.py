"""Asset-level audio check (headless editor): the game's audio is MetaSound only.

  Tools\\run_py.ps1 -Script Tools\\py\\verify_audio.py        (Tools\\verify.ps1 runs it)

Fails (error lines + a final "AUDIO ASSETS: FAIL") when
  * an expected asset is missing (MS_Music, 26 MS_Sfx_*; every one holds a single composite node),
  * /Game/Shift/Audio holds anything that is not a MetaSound Source or Patch (no SoundWave, no Wave Player assets),
  * any MetaSound asset has a package dependency on a SoundWave or on /Game/Shift/Audio/Waves,
  * a .wav file exists anywhere in the project folder (Saved, Intermediate and DerivedDataCache are scratch and skipped).
"""
import os
import sys

import unreal

sys.path.append(os.path.join(unreal.SystemLibrary.get_project_directory(), "Tools", "py"))
from shift_common import EAL, PROJECT_DIR, error, finish, log  # noqa: E402
from ms_graph import MS_DIR  # noqa: E402
from ms_sfx import RECIPES  # noqa: E402

SKIP_DIRS = {"Saved", "Intermediate", "DerivedDataCache", "Binaries", ".git", "Build"}


def main():
    problems = []
    expected = ["MS_Music"] + ["MS_Sfx_" + n for n in RECIPES]
    for name in expected:
        if not EAL.does_asset_exist("{}/{}".format(MS_DIR, name)):
            problems.append("missing asset {}/{}".format(MS_DIR, name))

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    options = unreal.AssetRegistryDependencyOptions()
    options.set_editor_property("include_hard_package_references", True)
    options.set_editor_property("include_soft_package_references", True)
    counts = {"MetaSoundSource": 0, "MetaSoundPatch": 0}
    for path in EAL.list_assets(MS_DIR, recursive=True, include_folder=False):
        data = EAL.find_asset_data(path)
        klass = str(data.asset_class_path.asset_name)
        if klass not in counts:
            problems.append("{} is a {} (only MetaSound Sources and Patches are allowed)".format(path, klass))
            continue
        counts[klass] += 1
        package = data.package_name
        deps = registry.get_dependencies(package, options) or []
        for dep in deps:
            dep_name = str(dep)
            if dep_name.startswith("/Game/Shift/Audio/Waves"):
                problems.append("{} depends on {}".format(path, dep_name))
                continue
            if not dep_name.startswith("/Game/"):
                continue
            for dep_data in registry.get_assets_by_package_name(dep):
                if "SoundWave" in str(dep_data.asset_class_path.asset_name):
                    problems.append("{} depends on the SoundWave {}".format(path, dep_name))
    log("MetaSound assets: {} Sources, {} Patches".format(counts["MetaSoundSource"], counts["MetaSoundPatch"]))

    waves = []
    for root, dirs, files in os.walk(PROJECT_DIR):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        waves += [os.path.join(root, f) for f in files if f.lower().endswith(".wav")]
    for wav in waves:
        problems.append("wav file in the project: {}".format(wav))
    if os.path.isdir(os.path.join(PROJECT_DIR, "Content", "Shift", "Audio", "Waves")):
        problems.append("Content/Shift/Audio/Waves still exists")

    for p in problems:
        error("audio check: " + p)
    log("AUDIO ASSETS: {} ({} problem(s))".format("PASS" if not problems else "FAIL", len(problems)))
    finish()


main()
