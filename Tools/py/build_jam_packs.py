"""Fills the jam pack Game Feature plugins (run Tools/make_jam_plugins.py first): imports each pack's entry table from Data/Packs/Entries_<Key>.csv into the
plugin's content, then creates DA_Jam and the GameFeatureData (with the Shift Jam Pack action) through the C++ builder.
Run: Tools\\run_py.ps1 -Script Tools\\py\\build_jam_packs.py
"""
import os
import sys

import unreal

sys.path.append(os.path.join(unreal.SystemLibrary.get_project_directory(), "Tools", "py"))
from shift_common import ASSET_TOOLS, DATA_DIR, EAL, finish, log, stage  # noqa: E402

# plugin, jam index, content key, card title, card subtitle, rival followers
PACKS = (
    ("ShiftJam_TinyJam7", 0, "TinyJam7", "Tiny Jam #7",
     "A cosy 36-voter jam. The rival has 3K followers. Learn the ropes, then try the night shift.", "3K"),
    ("ShiftJam_Rumble48", 1, "Rumble48", "48h Pixel Rumble",
     "52 voters, streamers behind gates and a Mod Eye that sweeps. The rival has 48K followers.", "48K"),
    ("ShiftJam_GameOff2026", 2, "GameOff2026", "GAME OFF 2026",
     "72 voters, two celebrities and a Mega Campaign in the last days. The rival has 1.2M followers.", "1.2M"),
)


def import_entries(plugin, key):
    csv_path = os.path.join(DATA_DIR, "Packs", "Entries_{}.csv".format(key))
    struct = unreal.load_object(None, "/Script/ShiftHappens.ShiftEntryRow")
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", csv_path)
    task.set_editor_property("destination_path", "/" + plugin)
    task.set_editor_property("destination_name", "DT_Entries")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    factory = unreal.CSVImportFactory()
    settings = factory.get_editor_property("automated_import_settings")
    settings.set_editor_property("import_row_struct", struct)
    settings.set_editor_property("import_type", unreal.CSVImportType.ECSV_DATA_TABLE)
    factory.set_editor_property("automated_import_settings", settings)
    task.set_editor_property("factory", factory)
    ASSET_TOOLS.import_asset_tasks([task])
    table = EAL.load_asset("/{}/DT_Entries".format(plugin))
    if table is None:
        raise RuntimeError("DT_Entries was not created in {}".format(plugin))
    log("{}: DT_Entries has {} rows".format(plugin, len(unreal.DataTableFunctionLibrary.get_data_table_row_names(table))))


@stage("jam packs")
def build():
    failures = []
    for plugin, jam, key, title, subtitle, followers in PACKS:
        try:
            if not unreal.ShiftJamPackBuilder.ensure_mounted(plugin):
                raise RuntimeError("could not mount {}".format(plugin))
            import_entries(plugin, key)
            if not unreal.ShiftJamPackBuilder.build_pack(plugin, jam, key, title, subtitle, followers):
                raise RuntimeError("build_pack failed")
            log("{}: pack built".format(plugin))
        except Exception as exc:  # noqa: BLE001
            failures.append("{}: {}".format(plugin, exc))
            unreal.log_error("[SHIFT] {} failed: {}".format(plugin, exc))
    if failures:
        raise RuntimeError("; ".join(failures))


build()
finish()
