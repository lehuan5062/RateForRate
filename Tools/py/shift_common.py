"""Shared helpers for the headless Unreal Python builders (run via Tools/run_py.ps1)."""
import os
import traceback

import unreal

PROJECT_DIR = unreal.SystemLibrary.get_project_directory()  # absolute, ends with '/'
DATA_DIR = os.path.join(PROJECT_DIR, "Data")
ASSET_ROOT = "/Game/Shift"

ASSET_TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary


def log(msg):
    unreal.log("[SHIFT] {}".format(msg))


def warn(msg):
    unreal.log_warning("[SHIFT] {}".format(msg))


def error(msg):
    unreal.log_error("[SHIFT] {}".format(msg))


def stage(name):
    """Decorator: run a build stage, log OK/FAILED with the full traceback, never abort the whole script."""

    def deco(fn):
        def run(*args, **kwargs):
            log("=== {} ===".format(name))
            try:
                result = fn(*args, **kwargs)
                log("=== {}: OK ===".format(name))
                return result
            except Exception:
                error("=== {}: FAILED ===\n{}".format(name, traceback.format_exc()))
                return None

        return run

    return deco


def ensure_dir(game_path):
    if not EAL.does_directory_exist(game_path):
        EAL.make_directory(game_path)


def get_or_create(name, folder, asset_class, factory):
    """Load the asset if it exists, otherwise create it. Builders are idempotent by *reusing and resetting* assets:
    delete-then-recreate inside one editor session fails intermittently (package still in memory)."""
    ensure_dir(folder)
    path = "{}/{}".format(folder, name)
    asset = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
    if asset is None:
        asset = ASSET_TOOLS.create_asset(name, folder, asset_class, factory)
    if asset is None:
        raise RuntimeError("could not load or create {}".format(path))
    return asset


def fresh_material(name, folder):
    """A material with no expressions (reused if it already exists)."""
    mat = get_or_create(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    return mat


# Kept for older scripts.
fresh_asset = get_or_create


def open_fresh_level(game_path):
    """Open (or create) a level and remove every actor except world settings / the builder brush."""
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    ensure_dir(game_path.rsplit("/", 1)[0])
    if EAL.does_asset_exist(game_path):
        if not les.load_level(game_path):
            raise RuntimeError("load_level failed for {}".format(game_path))
        removed = 0
        for actor in eas.get_all_level_actors():
            cls = actor.get_class().get_name()
            if cls in ("WorldSettings", "Brush"):
                continue
            eas.destroy_actor(actor)
            removed += 1
        log("reusing level {} (removed {} actors)".format(game_path, removed))
    else:
        if not les.new_level(game_path):
            raise RuntimeError("new_level failed for {}".format(game_path))
        log("created level {}".format(game_path))
    return les, eas


def connect(from_expr, to_expr, to_input_names, from_output=""):
    """Connect expression outputs to an input pin, trying several spellings of the pin name."""
    for pin in to_input_names:
        if MEL.connect_material_expressions(from_expr, from_output, to_expr, pin):
            return pin
    raise RuntimeError("could not connect {} -> {} pins tried: {}".format(
        from_expr.get_name(), to_expr.get_name(), to_input_names))


def save_all():
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)


def finish():
    save_all()
    log("finished, quitting editor")
    unreal.SystemLibrary.quit_editor()
