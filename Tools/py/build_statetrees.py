"""Builds, compiles and saves the behaviour StateTrees (ST_Rival, ST_Others, ST_Director, ST_Ambient) through the C++ editor-side builder.
Run: Tools\\run_py.ps1 -Script Tools\\py\\build_statetrees.py
"""
import os
import sys

import unreal

sys.path.append(os.path.join(unreal.SystemLibrary.get_project_directory(), "Tools", "py"))
from shift_common import ASSET_ROOT, ensure_dir, error, finish, log, stage  # noqa: E402


@stage("build StateTrees")
def build():
    ensure_dir(ASSET_ROOT + "/StateTrees")
    saved = unreal.ShiftStateTreeBuilder.build_all()
    log("saved {} of 4 StateTrees".format(saved))
    if saved != 4:
        raise RuntimeError("not all StateTrees were built, see the LogShift lines above")


build()
finish()
