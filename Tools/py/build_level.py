"""Builds L_Jam: the single level the game runs in.

Orthographic top-down camera (1280 world units wide to start with, MaintainXFOV, NOT letterboxed: the game sets the width every frame so that the
1280 x 720 board always fits and the window shows paper wherever it is wider or taller), an unbound post-process volume
(unit exposure, bloom, light vignette) and the background plane with M_Bg. The board (nodes/edges) is spawned at runtime
by AShiftBoard. Idempotent: reuses and clears the level.   Run: Tools\\run_py.ps1 -Script Tools\\py\\build_level.py
"""
import os
import sys

import unreal

sys.path.append(os.path.join(unreal.SystemLibrary.get_project_directory(), "Tools", "py"))
from shift_common import ASSET_ROOT, finish, log, open_fresh_level, stage, warn  # noqa: E402

MAT_DIR = ASSET_ROOT + "/Materials"
LEVEL_DIR = ASSET_ROOT + "/Levels"


def set_props(obj, props):
    for flag, prop, value in props:
        try:
            if flag:
                obj.set_editor_property(flag, True)
            obj.set_editor_property(prop, value)
        except Exception as exc:  # noqa: BLE001
            warn("property {} failed: {}".format(prop, exc))


@stage("L_Jam")
def build_level():
    les, eas = open_fresh_level(LEVEL_DIR + "/L_Jam")

    # unreal.Rotator is (roll, pitch, yaw); pitch -90 looks straight down (screen right = +Y, screen up = +X).
    cam = eas.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(0, 0, 1000), unreal.Rotator(0, -90, 0))
    cam.set_actor_label("ShiftCamera")
    comp = cam.get_component_by_class(unreal.CameraComponent)
    comp.set_editor_property("projection_mode", unreal.CameraProjectionMode.ORTHOGRAPHIC)
    comp.set_editor_property("ortho_width", 1280.0)
    comp.set_editor_property("constrain_aspect_ratio", False)   # no black bars: the window shows what its own shape shows (AShiftPlayerController::UpdateView sets the width)
    comp.set_editor_property("aspect_ratio", 1280.0 / 720.0)
    axis = unreal.AspectRatioAxisConstraint
    maintain_x = [getattr(axis, n) for n in dir(axis) if "XFOV" in n.upper().replace("_", "")]
    if maintain_x:
        comp.set_editor_property("aspect_ratio_axis_constraint", maintain_x[0])
        try:
            # without the override the engine keeps the HEIGHT fixed when the aspect ratio is not constrained, and the board would grow with the window
            comp.set_editor_property("override_aspect_ratio_axis_constraint", True)
        except Exception as exc:  # noqa: BLE001
            warn("override_aspect_ratio_axis_constraint failed (the game sets it at run time): {}".format(exc))
    else:
        warn("no MaintainXFOV member found in {}".format([n for n in dir(axis) if n.isupper()]))
    cam.set_editor_property("auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0)
    log("camera: {} ortho={}".format(cam.get_actor_rotation(), comp.get_editor_property("ortho_width")))

    ppv = eas.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0))
    ppv.set_actor_label("ShiftPost")
    ppv.set_editor_property("unbound", True)
    s = ppv.get_editor_property("settings")
    # Manual exposure uses camera ISO/shutter/aperture; f/1, 1 s, ISO 100 is EV100 0 (unit exposure for unlit HDR emissive).
    set_props(s, (
        ("override_bloom_intensity", "bloom_intensity", 1.6),
        ("override_bloom_threshold", "bloom_threshold", 0.9),
        ("override_auto_exposure_method", "auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL),
        ("override_auto_exposure_bias", "auto_exposure_bias", 0.0),
        ("override_camera_iso", "camera_iso", 100.0),
        ("override_camera_shutter_speed", "camera_shutter_speed", 1.0),
        ("override_depth_of_field_fstop", "depth_of_field_fstop", 1.0),
        ("override_vignette_intensity", "vignette_intensity", 0.25),
    ))
    ppv.set_editor_property("settings", s)

    plane = unreal.load_asset("/Engine/BasicShapes/Plane")
    bg_mat = unreal.load_asset(MAT_DIR + "/M_Bg")
    bg = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -10), unreal.Rotator(0, 0, 0))
    bg.set_actor_label("Background")
    bg.set_actor_scale3d(unreal.Vector(60.0, 60.0, 1.0))   # 6000 x 6000 units: covers any window shape, from a tall phone-like one to an ultra-wide one
    smc = bg.get_component_by_class(unreal.StaticMeshComponent)
    smc.set_static_mesh(plane)
    if bg_mat is not None:
        smc.set_material(0, bg_mat)
    else:
        warn("M_Bg not found; run build_materials.py first")
    les.save_current_level()
    log("saved {}/L_Jam".format(LEVEL_DIR))


def main():
    build_level()
    finish()


main()
