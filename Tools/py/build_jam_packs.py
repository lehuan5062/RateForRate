"""Authors the three jam pack Game Feature plugins entirely from Python (no C++ builder):

  1. Data/Entries.csv is split per jam into Data/Packs/Entries_<Key>.csv
  2. a plugin is created with the PluginToolset ("Game Feature (Content Only)" template, via unreal.ToolsetRegistry.execute_tool), which also
     mounts it and creates its GameFeatureData; the template's descriptor already says ExplicitlyLoaded + BuiltInInitialFeatureState Active
  3. DT_Entries (CSV import, row struct FShiftEntryRow), DA_Jam (a UShiftJamPackData through DataAssetFactory) and the GameFeatureData's
     "Shift Jam Pack" action (an instance of UGameFeatureAction_ShiftJam, set into the Actions array) are created and saved

Idempotent: an existing plugin is reused and its assets are updated in place.
Run: Tools\\run_py.ps1 -Script Tools\\py\\build_jam_packs.py
"""
import csv
import json
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
     "72 voters, three celebrities and a Mega Campaign in the last days. The rival has 1.2M followers.", "1.2M"),
    ("ShiftJam_LevelField", 3, "LevelField", "Level Playing Field",
     "The control group: 60 voters, no head start for anyone. The rival has 41 followers, like you.", "41"),
    ("ShiftJam_Weekend", 4, "Weekend", "Unmoderated Weekend",
     "64 voters. Three times the Mod Eye looks away, and making fake votes adds no Heat. The rival has 310K followers.", "310K"),
)

PLUGIN_TOOLSET = "PluginToolset.PluginToolset"
GF_TEMPLATE = "Game Feature (Content Only)"


def tool(name, **args):
    """One PluginToolset tool call (the same tools an MCP client sees); returns the tool's return value."""
    result = unreal.ToolsetRegistry.execute_tool(PLUGIN_TOOLSET, name, json.dumps(args))
    if result.error:
        raise RuntimeError("{}: {}".format(name, result.error))
    return json.loads(result.value).get("returnValue")


def split_entries():
    with open(os.path.join(DATA_DIR, "Entries.csv"), encoding="utf-8", newline="") as fh:
        rows = list(csv.DictReader(fh))
    fields = list(rows[0].keys())
    packs_dir = os.path.join(DATA_DIR, "Packs")
    os.makedirs(packs_dir, exist_ok=True)
    for _plugin, _jam, key, _title, _subtitle, _followers in PACKS:
        subset = [r for r in rows if r["Jam"] == key]
        with open(os.path.join(packs_dir, "Entries_{}.csv".format(key)), "w", encoding="utf-8", newline="") as fh:
            writer = csv.DictWriter(fh, fieldnames=fields)
            writer.writeheader()
            writer.writerows(subset)
        log("{}: {} entry rows".format(key, len(subset)))


def enable_by_default(plugin):
    """The template descriptor has EnabledByDefault false and the PluginToolset cannot change that (SetPluginEnabled only reports the
    request, UpdatePluginDescriptor has no such field), so the one flag is set in the .uplugin JSON. Returns True when it changed."""
    path = tool("GetPluginInfo", pluginName=plugin)["descriptorPath"]
    with open(path, encoding="utf-8") as fh:
        descriptor = json.load(fh)
    if descriptor.get("EnabledByDefault"):
        return False
    descriptor["EnabledByDefault"] = True
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(descriptor, fh, indent="\t")
        fh.write("\n")
    return True


def ensure_plugin(plugin, title):
    """Create the plugin from the Game Feature template when it does not exist (this also mounts it in the running editor), and make
    sure it is enabled by default. Returns whether its content is mounted in this editor session."""
    if plugin in tool("ListDiscoveredPlugins"):
        log("{}: plugin exists".format(plugin))
    else:
        template = next(t for t in tool("GetPluginTemplateDescriptions") if t["name"] == GF_TEMPLATE)
        path = tool("CreatePlugin", pluginName=plugin, relativePluginLocation="", bPlaceInEngine=False, templateInfo=template,
                    description="Game Feature plugin: the {} jam of Rate for Rate (card text, rival and other entries).".format(title))
        log("{}: created {}".format(plugin, path))
        for dependency in ("GameFeatures", "ModularGameplay"):
            tool("AddPluginDependency", pluginName=plugin, dependencyName=dependency, bOptional=False, bEnabled=True)
        descriptor = tool("GetPluginDescriptor", pluginName=plugin)
        descriptor["friendlyName"] = "Shift Jam Pack: " + title
        descriptor["createdBy"] = "Rate for Rate"
        tool("UpdatePluginDescriptor", pluginName=plugin, newDescriptor=descriptor)
    changed = enable_by_default(plugin)
    mounted = EAL.does_directory_exist("/" + plugin)
    if changed:
        log("{}: set EnabledByDefault".format(plugin))
    if not mounted:
        log("{}: not mounted in this editor session (it was just enabled): run this script once more".format(plugin))
    return mounted


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
    return table


def author_pack(plugin, jam, key, title, subtitle, followers, table):
    root = "/" + plugin
    path = root + "/DA_Jam"
    if EAL.does_asset_exist(path):
        pack = EAL.load_asset(path)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.ShiftJamPackData)
        pack = ASSET_TOOLS.create_asset("DA_Jam", root, unreal.ShiftJamPackData, factory)
    if pack is None:
        raise RuntimeError("could not create {}".format(path))
    pack.set_editor_properties({"jam_index": jam, "key": key, "title": title, "subtitle": subtitle, "rival_followers": followers, "entries": table})
    EAL.save_loaded_asset(pack)

    feature = EAL.load_asset("{}/{}".format(root, plugin))   # created by the plugin template, named like the plugin
    if feature is None:
        raise RuntimeError("{} has no GameFeatureData".format(plugin))
    action_class = unreal.load_object(None, "/Script/ShiftHappens.GameFeatureAction_ShiftJam")
    action = unreal.new_object(action_class, outer=feature)
    action.set_editor_property("pack", pack)
    feature.set_editor_property("actions", [action])
    EAL.save_loaded_asset(feature)
    log("{}: DA_Jam + GameFeatureData action saved".format(plugin))


@stage("jam packs")
def build():
    split_entries()
    failures = []
    for plugin, jam, key, title, subtitle, followers in PACKS:
        try:
            if not ensure_plugin(plugin, title):
                continue
            table = import_entries(plugin, key)
            author_pack(plugin, jam, key, title, subtitle, followers, table)
        except Exception as exc:  # noqa: BLE001
            failures.append("{}: {}".format(plugin, exc))
            unreal.log_error("[SHIFT] {} failed: {}".format(plugin, exc))
    if failures:
        raise RuntimeError("; ".join(failures))


build()
finish()
