"""S1 spike: what can headless Python author on UE 5.8? Probes the API and builds test assets.

Builds: MPC_Theme, three SDF circle materials (Substrate default-transmittance, Substrate transmittance 0, legacy
translucent), a navy background material, and the test level L_S1 (ortho camera, bloom post-process volume, bg plane,
three SDF quads) so a screenshot shows how each blends and whether bloom picks up the HDR emissive.
Run: Tools\\run_py.ps1 -Script Tools\\py\\s1_probe.py
"""
import os
import sys

import unreal

sys.path.append(os.path.join(unreal.SystemLibrary.get_project_directory(), "Tools", "py"))
from shift_common import (ASSET_ROOT, ASSET_TOOLS, EAL, MEL, connect, ensure_dir, finish,  # noqa: E402
                          fresh_material, get_or_create, log, open_fresh_level, stage, warn)

MAT_DIR = ASSET_ROOT + "/Materials"
LEVEL_DIR = ASSET_ROOT + "/Levels"

SDF_HLSL = """
float2 p = UV * 2.0 - 1.0;
float d = length(p) - 0.80;
float aa = max(fwidth(d), 1e-4);
float cov = 1.0 - smoothstep(-aa, aa, d);
float rim = 1.0 - smoothstep(0.0, 0.10, abs(d + 0.06));
float3 col = Color.rgb * (1.0 + 0.6 * rim) * Intensity;
return float4(col, cov);
"""


@stage("probe API")
def probe():
    log("Engine version: {}".format(unreal.SystemLibrary.get_engine_version()))
    try:
        log("r.Substrate = {}".format(unreal.SystemLibrary.get_console_variable_int_value("r.Substrate")))
    except Exception as exc:  # noqa: BLE001
        warn("could not read r.Substrate: {}".format(exc))
    substrate = sorted(n for n in dir(unreal) if n.startswith("MaterialExpressionSubstrate"))
    log("Substrate expression classes: {}".format(len(substrate)))
    rot = unreal.Rotator(0, -90, 0)
    log("Rotator(0,-90,0) -> pitch={} yaw={} roll={}".format(rot.pitch, rot.yaw, rot.roll))


@stage("MPC_Theme")
def build_mpc():
    mpc = get_or_create("MPC_Theme", MAT_DIR, unreal.MaterialParameterCollection,
                        unreal.MaterialParameterCollectionFactoryNew())
    scalars = []
    for name, default in (("Night", 0.0), ("Heat", 0.0), ("Beat", 0.0), ("Time", 0.0), ("WipeRadius", 0.0)):
        p = unreal.CollectionScalarParameter()
        p.set_editor_property("parameter_name", name)
        p.set_editor_property("default_value", default)
        scalars.append(p)
    mpc.set_editor_property("scalar_parameters", scalars)
    v = unreal.CollectionVectorParameter()
    v.set_editor_property("parameter_name", "WipeCenter")
    v.set_editor_property("default_value", unreal.LinearColor(0.5, 0.5, 0.0, 0.0))
    mpc.set_editor_property("vector_parameters", [v])
    EAL.save_loaded_asset(mpc)


def _sdf_core(mat, color, intensity):
    uv = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -900, 0)
    col = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -900, 200)
    col.set_editor_property("parameter_name", "Color")
    col.set_editor_property("default_value", color)
    inten = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -900, 400)
    inten.set_editor_property("parameter_name", "Intensity")
    inten.set_editor_property("default_value", intensity)

    custom = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, -500, 100)
    custom.set_editor_property("description", "SDF circle")
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property("code", SDF_HLSL)
    inputs = []
    for nm in ("UV", "Color", "Intensity"):
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", nm)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)
    connect(uv, custom, ["UV"])
    connect(col, custom, ["Color"])
    connect(inten, custom, ["Intensity"])
    return custom


def _mask(mat, source, r, g, b, a, x, y):
    m = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, x, y)
    m.set_editor_property("r", r)
    m.set_editor_property("g", g)
    m.set_editor_property("b", b)
    m.set_editor_property("a", a)
    connect(source, m, ["", "Input"])
    return m


def _build_substrate(name, color, intensity, zero_transmittance):
    mat = fresh_material(name, MAT_DIR)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("two_sided", True)
    custom = _sdf_core(mat, color, intensity)
    rgb = _mask(mat, custom, True, True, True, False, -250, 0)
    alpha = _mask(mat, custom, False, False, False, True, -250, 200)

    unlit = MEL.create_material_expression(mat, unreal.MaterialExpressionSubstrateUnlitBSDF, 50, 0)
    connect(rgb, unlit, ["EmissiveColor", "Emissive Color"])
    if zero_transmittance:
        black = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -250, 420)
        black.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 0.0, 1.0))
        pin = connect(black, unlit, ["TransmittanceColor", "Transmittance Color"])
        log("{}: transmittance pin {}".format(name, pin))
    weight = MEL.create_material_expression(mat, unreal.MaterialExpressionSubstrateWeight, 350, 0)
    connect(unlit, weight, ["A"])
    connect(alpha, weight, ["Weight"])
    MEL.connect_material_property(weight, "", unreal.MaterialProperty.MP_FRONT_MATERIAL)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat


@stage("M_S1_SdfSubstrate (default transmittance)")
def build_substrate_default():
    return _build_substrate("M_S1_SdfSubstrate", unreal.LinearColor(0.12, 0.71, 0.65, 1.0), 4.0, False)


@stage("M_S1_SdfSubstrateOver (transmittance 0)")
def build_substrate_over():
    return _build_substrate("M_S1_SdfSubstrateOver", unreal.LinearColor(1.0, 0.30, 0.35, 1.0), 4.0, True)


@stage("M_S1_SdfLegacy")
def build_legacy_material():
    mat = fresh_material("M_S1_SdfLegacy", MAT_DIR)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    custom = _sdf_core(mat, unreal.LinearColor(1.0, 0.85, 0.2, 1.0), 4.0)
    rgb = _mask(mat, custom, True, True, True, False, -250, 0)
    alpha = _mask(mat, custom, False, False, False, True, -250, 200)
    MEL.connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat


@stage("M_S1_Bg (navy, opaque)")
def build_bg_material():
    mat = fresh_material("M_S1_Bg", MAT_DIR)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    c = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -300, 0)
    c.set_editor_property("constant", unreal.LinearColor(0.05, 0.07, 0.16, 1.0))
    MEL.connect_material_property(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat


@stage("DT_S1Test from CSV")
def build_datatable():
    struct = unreal.load_object(None, "/Script/ShiftHappens.ShiftTestRow")
    if struct is None:
        raise RuntimeError("row struct /Script/ShiftHappens.ShiftTestRow not found")
    ensure_dir(ASSET_ROOT + "/Data")
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", os.path.join(unreal.SystemLibrary.get_project_directory(),
                                                      "Tools", "py", "test_data", "S1Test.csv"))
    task.set_editor_property("destination_path", ASSET_ROOT + "/Data")
    task.set_editor_property("destination_name", "DT_S1Test")
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
    dt = unreal.load_asset(ASSET_ROOT + "/Data/DT_S1Test")
    names = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)
    log("DT_S1Test rows: {}".format([str(n) for n in names]))
    if len(names) != 3:
        raise RuntimeError("expected 3 rows, got {}".format(len(names)))


@stage("L_S1 level")
def build_level():
    les, eas = open_fresh_level(LEVEL_DIR + "/L_S1")

    # unreal.Rotator is (roll, pitch, yaw); pitch -90 looks straight down.
    cam = eas.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(0, 0, 1000), unreal.Rotator(0, -90, 0))
    cam.set_actor_label("ShiftCamera")
    comp = cam.get_component_by_class(unreal.CameraComponent)
    comp.set_editor_property("projection_mode", unreal.CameraProjectionMode.ORTHOGRAPHIC)
    comp.set_editor_property("ortho_width", 1280.0)
    comp.set_editor_property("constrain_aspect_ratio", False)
    cam.set_editor_property("auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0)
    log("camera rotation after setup: {}".format(cam.get_actor_rotation()))

    ppv = eas.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0))
    ppv.set_actor_label("ShiftPost")
    ppv.set_editor_property("unbound", True)
    s = ppv.get_editor_property("settings")
    # Manual exposure in UE uses camera ISO / shutter / aperture, which default to ~EV100 10 (everything near black).
    # f/1, 1 s, ISO 100 gives EV100 = 0, i.e. unit exposure for unlit HDR emissive.
    for flag, prop, value in (
        ("override_bloom_intensity", "bloom_intensity", 2.0),
        ("override_bloom_threshold", "bloom_threshold", 0.6),
        ("override_auto_exposure_method", "auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL),
        ("override_auto_exposure_bias", "auto_exposure_bias", 0.0),
        ("override_camera_iso", "camera_iso", 100.0),
        ("override_camera_shutter_speed", "camera_shutter_speed", 1.0),
        ("override_depth_of_field_fstop", "depth_of_field_fstop", 1.0),
        ("override_vignette_intensity", "vignette_intensity", 0.35),
    ):
        try:
            s.set_editor_property(flag, True)
            s.set_editor_property(prop, value)
        except Exception as exc:  # noqa: BLE001
            warn("post-process property {} failed: {}".format(prop, exc))
    ppv.set_editor_property("settings", s)

    plane = unreal.load_asset("/Engine/BasicShapes/Plane")
    placements = (
        ("M_S1_Bg", unreal.Vector(0, 0, -20), unreal.Vector(16.0, 16.0, 1.0)),
        ("M_S1_SdfSubstrate", unreal.Vector(0, -400, 0), unreal.Vector(3.0, 3.0, 1.0)),
        ("M_S1_SdfSubstrateOver", unreal.Vector(0, 0, 0), unreal.Vector(3.0, 3.0, 1.0)),
        ("M_S1_SdfLegacy", unreal.Vector(0, 400, 0), unreal.Vector(3.0, 3.0, 1.0)),
    )
    for mat_name, loc, scale in placements:
        mat = unreal.load_asset("{}/{}".format(MAT_DIR, mat_name))
        a = eas.spawn_actor_from_class(unreal.StaticMeshActor, loc, unreal.Rotator(0, 0, 0))
        a.set_actor_label("Quad_" + mat_name)
        a.set_actor_scale3d(scale)
        smc = a.get_component_by_class(unreal.StaticMeshComponent)
        smc.set_static_mesh(plane)
        if mat is not None:
            smc.set_material(0, mat)
        else:
            warn("material {} not found".format(mat_name))
    les.save_current_level()
    log("saved level")


def main():
    probe()
    build_mpc()
    build_substrate_default()
    build_substrate_over()
    build_legacy_material()
    build_bg_material()
    build_datatable()
    build_level()
    finish()


main()
