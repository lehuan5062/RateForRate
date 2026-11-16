"""S4 probe 3: how do we turn a source builder into a saved MetaSound Source asset in the editor? Prints MetaSoundEditorSubsystem's API."""
import os
import sys

import unreal

sys.path.append(os.path.join(unreal.SystemLibrary.get_project_directory(), "Tools", "py"))
from shift_common import finish, log, stage  # noqa: E402


@stage("editor subsystem api")
def api():
    names = sorted(n for n in dir(unreal) if "metasound" in n.lower() and ("editor" in n.lower() or "factory" in n.lower() or "options" in n.lower()))
    log("names: {}".format(", ".join(names)))
    cls = getattr(unreal, "MetaSoundEditorSubsystem", None)
    if cls is None:
        log("MetaSoundEditorSubsystem missing")
        return
    for n in dir(cls):
        if n.startswith("_"):
            continue
        member = getattr(cls, n, None)
        doc = (getattr(member, "__doc__", "") or "").strip().replace("\n", " | ")
        if any(k in n for k in ("build", "asset", "builder", "create", "register", "find")):
            log("MetaSoundEditorSubsystem.{}: {}".format(n, doc[:900]))


finish_called = False
api()
finish()
