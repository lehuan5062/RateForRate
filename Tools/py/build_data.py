"""Imports the text CSVs in Data/ into /Game/Shift/Data/DT_* DataTables (row structs live in Source/ShiftHappens/Data/ShiftRows.h).

The first CSV column (Id) becomes the row name; other headers match the struct property names. Re-importing replaces the
tables, so the builder is idempotent. Run: Tools\\run_py.ps1 -Script Tools\\py\\build_data.py
"""
import csv
import os
import sys

import unreal

sys.path.append(os.path.join(unreal.SystemLibrary.get_project_directory(), "Tools", "py"))
from shift_common import ASSET_ROOT, ASSET_TOOLS, DATA_DIR, ensure_dir, finish, log, stage  # noqa: E402

# (csv file stem, table asset name, row struct)
TABLES = (
    ("Handles", "DT_Handles", "ShiftHandleRow"),
    ("Bios", "DT_Bios", "ShiftBioRow"),
    ("Comments", "DT_Comments", "ShiftCommentRow"),
    ("Headlines", "DT_Headlines", "ShiftHeadlineRow"),
    ("Entries", "DT_Entries", "ShiftEntryRow"),
    ("Blurbs", "DT_Blurbs", "ShiftBlurbRow"),
    ("Achievements", "DT_Achievements", "ShiftAchievementRow"),
    ("Tutorial", "DT_Tutorial", "ShiftTutorialRow"),
    ("Lessons", "DT_Lessons", "ShiftLessonRow"),
)


def csv_row_count(path):
    with open(path, "r", encoding="utf-8", newline="") as fh:
        return sum(1 for _ in csv.DictReader(fh))


def import_table(stem, asset_name, struct_name):
    csv_path = os.path.join(DATA_DIR, stem + ".csv")
    if not os.path.exists(csv_path):
        raise RuntimeError("missing {}".format(csv_path))
    struct = unreal.load_object(None, "/Script/ShiftHappens.{}".format(struct_name))
    if struct is None:
        raise RuntimeError("row struct {} not found (module not built?)".format(struct_name))

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", csv_path)
    task.set_editor_property("destination_path", ASSET_ROOT + "/Data")
    task.set_editor_property("destination_name", asset_name)
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

    table = unreal.load_asset("{}/Data/{}".format(ASSET_ROOT, asset_name))
    if table is None:
        raise RuntimeError("{} was not created".format(asset_name))
    imported = len(unreal.DataTableFunctionLibrary.get_data_table_row_names(table))
    expected = csv_row_count(csv_path)
    log("{}: imported {} rows (csv has {})".format(asset_name, imported, expected))
    if imported != expected:
        raise RuntimeError("{}: row count mismatch {} != {}".format(asset_name, imported, expected))


@stage("import text tables")
def build_tables():
    ensure_dir(ASSET_ROOT + "/Data")
    failures = []
    for stem, asset_name, struct_name in TABLES:
        try:
            import_table(stem, asset_name, struct_name)
        except Exception as exc:  # noqa: BLE001
            failures.append("{}: {}".format(asset_name, exc))
            unreal.log_error("[SHIFT] {} failed: {}".format(asset_name, exc))
    if failures:
        raise RuntimeError("; ".join(failures))


def main():
    build_tables()
    finish()


main()
