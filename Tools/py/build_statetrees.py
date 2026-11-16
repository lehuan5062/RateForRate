"""Builds, compiles and saves the behaviour StateTrees (ST_Rival, ST_Others, ST_Director, ST_Ambient) through the C++ editor-side builder.
Run: Tools\\run_py.ps1 -Script Tools\\py\\build_statetrees.py
"""
import os
import sys

import unreal

sys.path.append(os.path.join(unreal.SystemLibrary.get_project_directory(), "Tools", "py"))
from shift_common import ASSET_ROOT, PROJECT_DIR, ensure_dir, error, finish, log, stage  # noqa: E402

TREES = ("ST_Rival", "ST_Others", "ST_Director", "ST_Ambient")


@stage("build StateTrees")
def build():
    # A serialised asset that already exists is opened "partially loaded" and cannot be saved over, so the old files go first.
    for name in TREES:
        path = os.path.join(PROJECT_DIR, "Content", "Shift", "StateTrees", name + ".uasset")
        if os.path.exists(path):
            os.remove(path)
    ensure_dir(ASSET_ROOT + "/StateTrees")
    saved = unreal.ShiftStateTreeBuilder.build_all()
    log("saved {} of 4 StateTrees".format(saved))
    if saved != 4:
        raise RuntimeError("not all StateTrees were built, see the LogShift lines above")


build()
finish()
