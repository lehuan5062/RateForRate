"""S6 probe: what does the editor expose for creating Game Feature plugins and their GameFeatureData from Python?"""
import os
import sys

import unreal

sys.path.append(os.path.join(unreal.SystemLibrary.get_project_directory(), "Tools", "py"))
from shift_common import finish, log, stage  # noqa: E402


@stage("names")
def names():
    found = sorted(n for n in dir(unreal) if "gamefeature" in n.lower() or "modulargameplay" in n.lower())
    log("{} names: {}".format(len(found), ", ".join(found)))


@stage("subsystem api")
def api():
    for cls_name in ("GameFeaturesSubsystem", "GameFeaturesSubsystemSettings"):
        cls = getattr(unreal, cls_name, None)
        if cls is None:
            log("{} missing".format(cls_name))
            continue
        for n in dir(cls):
            if n.startswith("_"):
                continue
            member = getattr(cls, n, None)
            doc = (getattr(member, "__doc__", "") or "").strip().replace("\n", " | ")
            log("{}.{}: {}".format(cls_name, n, doc[:300]))


@stage("plugins")
def plugins():
    mgr = unreal.PluginBlueprintLibrary if hasattr(unreal, "PluginBlueprintLibrary") else None
    log("PluginBlueprintLibrary: {}".format(mgr))


names()
api()
plugins()
finish()
