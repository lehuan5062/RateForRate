"""S4 probe: what does the MetaSound Builder API look like from Python on UE 5.8?

Prints the Python-visible API (names, signatures, docs) of the builder subsystem and builder classes, lists the
MetaSound-related classes, then tries to build a tiny source (sine -> output) and save it as /Game/Shift/Audio/MS_S4Test.
Run: Tools\\run_py.ps1 -Script Tools\\py\\probe_metasound.py
"""
import os
import sys

import unreal

sys.path.append(os.path.join(unreal.SystemLibrary.get_project_directory(), "Tools", "py"))
from shift_common import ASSET_ROOT, EAL, ensure_dir, error, finish, log, stage, warn  # noqa: E402


def dump_class(cls, label, only=None):
    names = [n for n in dir(cls) if not n.startswith("_")]
    if only:
        names = [n for n in names if only(n)]
    log("--- {} ({} members) ---".format(label, len(names)))
    for n in names:
        member = getattr(cls, n, None)
        doc = (getattr(member, "__doc__", "") or "").strip().replace("\n", " | ")
        log("{}.{}: {}".format(label, n, doc[:700]))


@stage("list MetaSound classes")
def list_classes():
    names = sorted(n for n in dir(unreal) if "metasound" in n.lower())
    log("MetaSound-related names in unreal ({}): {}".format(len(names), ", ".join(names)))
    names = sorted(n for n in dir(unreal) if "harmonix" in n.lower() or "musicclock" in n.lower())
    log("Harmonix-related names ({}): {}".format(len(names), ", ".join(names)))


@stage("dump builder API")
def dump_api():
    for cls_name in ("MetaSoundBuilderSubsystem",):
        cls = getattr(unreal, cls_name, None)
        if cls is None:
            warn("{} missing".format(cls_name))
            continue
        dump_class(cls, cls_name)
    for cls_name in ("MetaSoundBuilderBase", "MetaSoundSourceBuilder", "MetaSoundPatchBuilder"):
        cls = getattr(unreal, cls_name, None)
        if cls is None:
            warn("{} missing".format(cls_name))
            continue
        dump_class(cls, cls_name)


@stage("minimal source build")
def build_minimal():
    sub = unreal.get_engine_subsystem(unreal.MetaSoundBuilderSubsystem)
    log("subsystem: {}".format(sub))
    # Out-params come back as a tuple in Python. Print whatever we get, then inspect it.
    result = sub.create_source_builder("S4TestBuilder")
    log("create_source_builder -> type {} value {}".format(type(result), result))


def main():
    list_classes()
    dump_api()
    build_minimal()
    finish()


main()
