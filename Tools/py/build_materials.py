"""Builds MPC_Theme and the board materials: M_Bg, M_Node, M_Glow, M_Edge.

All materials are Substrate (Unlit BSDF, Front Material) with HLSL SDF Custom nodes (sources in Tools/py/hlsl).
Per-instance data comes from PerInstanceCustomData; global theme (night, time, cursor, palette) from MPC_Theme.
Idempotent: reuses and resets existing assets.   Run: Tools\\run_py.ps1 -Script Tools\\py\\build_materials.py

Custom-data layouts (must match Source/ShiftHappens/Board/ShiftBoard.cpp):
  node (20): 0-2 fill rgb, 3 conv, 4 lock, 5 gate, 6 hover, 7 flash, 8 role, 9 expr, 10 sock, 11 pressure, 12-13 centre world xy, 14 radius,
             15 threat, 16-18 pusher rgb, 19 shield
  glow (9):  0-2 fill rgb, 3 flash, 4 hover, 5-6 centre world xy, 7 radius, 8 strength
  edge (13): 0-2 colour rgb, 3 base alpha, 4 pulse, 5 seed, 6 speed, 7 half width, 8-9 P0 xy, 10-11 P1 xy, 12 bulge
"""
import os
import sys

import unreal

sys.path.append(os.path.join(unreal.SystemLibrary.get_project_directory(), "Tools", "py"))
from shift_common import (ASSET_ROOT, EAL, MEL, PROJECT_DIR, connect, finish, fresh_material,  # noqa: E402
                          get_or_create, log, stage)

MAT_DIR = ASSET_ROOT + "/Materials"
HLSL_DIR = os.path.join(PROJECT_DIR, "Tools", "py", "hlsl")
MPC_PATH = MAT_DIR + "/MPC_Theme"


def read_hlsl(name):
    with open(os.path.join(HLSL_DIR, name + ".hlsl"), "r", encoding="utf-8") as fh:
        return fh.read()


# ------------------------------------------------------------------ graph helpers
def add(mat, cls, x, y):
    return MEL.create_material_expression(mat, cls, x, y)


def custom(mat, desc, code, input_names, out_type, x, y):
    node = add(mat, unreal.MaterialExpressionCustom, x, y)
    node.set_editor_property("description", desc)
    node.set_editor_property("output_type", out_type)
    node.set_editor_property("code", code)
    inputs = []
    for name in input_names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        inputs.append(ci)
    node.set_editor_property("inputs", inputs)
    return node


def idata(mat, index, default, x, y):
    e = add(mat, unreal.MaterialExpressionPerInstanceCustomData, x, y)
    e.set_editor_property("data_index", index)
    e.set_editor_property("const_default_value", default)
    return e


def mpc_param(mat, mpc, name, x, y):
    e = add(mat, unreal.MaterialExpressionCollectionParameter, x, y)
    e.set_editor_property("collection", mpc)
    e.set_editor_property("parameter_name", name)
    return e


def mask(mat, src, r, g, b, a, x, y):
    m = add(mat, unreal.MaterialExpressionComponentMask, x, y)
    m.set_editor_property("r", r)
    m.set_editor_property("g", g)
    m.set_editor_property("b", b)
    m.set_editor_property("a", a)
    connect(src, m, ["", "Input"])
    return m


def append(mat, a, b, x, y):
    e = add(mat, unreal.MaterialExpressionAppendVector, x, y)
    connect(a, e, ["A"])
    connect(b, e, ["B"])
    return e


def vec2(mat, ia, ib, x, y):
    return append(mat, ia, ib, x, y)


def vec3(mat, ia, ib, ic, x, y):
    return append(mat, append(mat, ia, ib, x, y), ic, x + 120, y)


def world_xy(mat, x, y):
    wp = add(mat, unreal.MaterialExpressionWorldPosition, x, y)
    return mask(mat, wp, True, True, False, False, x + 160, y)


def base_flags(mat, translucent, instanced):
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT if translucent else unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("two_sided", True)
    if instanced:
        mat.set_editor_property("used_with_instanced_static_meshes", True)


def finish_substrate(mat, rgb, alpha=None, over=True):
    """Unlit BSDF -> optional coverage weight -> Front Material. over=True uses transmittance 0 (occludes what is behind)."""
    unlit = add(mat, unreal.MaterialExpressionSubstrateUnlitBSDF, 600, 0)
    connect(rgb, unlit, ["EmissiveColor", "Emissive Color"])
    if over:
        black = add(mat, unreal.MaterialExpressionConstant3Vector, 300, 700)
        black.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 0.0, 1.0))
        connect(black, unlit, ["TransmittanceColor", "Transmittance Color"])
    root = unlit
    if alpha is not None:
        weight = add(mat, unreal.MaterialExpressionSubstrateWeight, 900, 0)
        connect(unlit, weight, ["A"])
        connect(alpha, weight, ["Weight"])
        root = weight
    MEL.connect_material_property(root, "", unreal.MaterialProperty.MP_FRONT_MATERIAL)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)


# ------------------------------------------------------------------ assets
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
    vectors = []
    for name, value in (
        ("WipeCenter", (0.0, 0.0, 0.0, 0.0)),
        ("Cursor", (0.0, 0.0, 0.0, 0.0)),
        ("PaperDay", (1.40, 1.27, 1.06, 1.0)),       # warm paper (linear, >1 because the filmic tonemapper darkens and desaturates)
        ("PaperNight", (0.0035, 0.0050, 0.0170, 1.0)),  # midnight navy (linear)
        ("InkDay", (0.10, 0.09, 0.17, 1.0)),
        ("InkNight", (0.85, 0.84, 1.00, 1.0)),
    ):
        v = unreal.CollectionVectorParameter()
        v.set_editor_property("parameter_name", name)
        v.set_editor_property("default_value", unreal.LinearColor(*value))
        vectors.append(v)
    mpc.set_editor_property("vector_parameters", vectors)
    EAL.save_loaded_asset(mpc)
    return mpc


@stage("M_Bg")
def build_bg(mpc):
    mat = fresh_material("M_Bg", MAT_DIR)
    base_flags(mat, translucent=False, instanced=False)
    pw = world_xy(mat, -1500, 0)
    paper_a = mask(mat, mpc_param(mat, mpc, "PaperDay", -1500, 200), True, True, True, False, -1340, 200)
    paper_b = mask(mat, mpc_param(mat, mpc, "PaperNight", -1500, 360), True, True, True, False, -1340, 360)
    ink_a = mask(mat, mpc_param(mat, mpc, "InkDay", -1500, 520), True, True, True, False, -1340, 520)
    ink_b = mask(mat, mpc_param(mat, mpc, "InkNight", -1500, 680), True, True, True, False, -1340, 680)
    night = mpc_param(mat, mpc, "Night", -1500, 840)
    tm = mpc_param(mat, mpc, "Time", -1500, 960)
    beat = mpc_param(mat, mpc, "Beat", -1500, 1080)
    node = custom(mat, "background", read_hlsl("bg"),
                  ["PW", "PaperA", "PaperB", "InkA", "InkB", "Night", "Tm", "Beat"],
                  unreal.CustomMaterialOutputType.CMOT_FLOAT3, -600, 300)
    for src, pin in ((pw, "PW"), (paper_a, "PaperA"), (paper_b, "PaperB"), (ink_a, "InkA"), (ink_b, "InkB"),
                     (night, "Night"), (tm, "Tm"), (beat, "Beat")):
        connect(src, node, [pin])
    finish_substrate(mat, node, alpha=None, over=False)


@stage("M_Node")
def build_node(mpc):
    mat = fresh_material("M_Node", MAT_DIR)
    base_flags(mat, translucent=True, instanced=True)
    d = [idata(mat, i, 0.0, -1800, i * 110) for i in range(20)]
    pw = world_xy(mat, -1500, -300)
    fill = vec3(mat, d[0], d[1], d[2], -1400, 0)
    cw = vec2(mat, d[12], d[13], -1400, 1450)
    push = vec3(mat, d[16], d[17], d[18], -1400, 2300)
    night = mpc_param(mat, mpc, "Night", -1500, -100)
    tm = mpc_param(mat, mpc, "Time", -1500, 0)
    heat = mpc_param(mat, mpc, "Heat", -1500, 100)
    cur = mask(mat, mpc_param(mat, mpc, "Cursor", -1500, 200), True, True, False, False, -1340, 200)
    node = custom(mat, "voter node", read_hlsl("node"),
                  ["PW", "CW", "Rad", "Fill", "Conv", "Lock", "Gate", "Hover", "Flash", "Role", "Expr", "Sock", "Pressure",
                   "Threat", "PushCol", "Shield", "Night", "Tm", "Heat", "Cur"],
                  unreal.CustomMaterialOutputType.CMOT_FLOAT4, -400, 300)
    wiring = (
        (pw, "PW"), (cw, "CW"), (d[14], "Rad"), (fill, "Fill"), (d[3], "Conv"), (d[4], "Lock"), (d[5], "Gate"),
        (d[6], "Hover"), (d[7], "Flash"), (d[8], "Role"), (d[9], "Expr"), (d[10], "Sock"), (d[11], "Pressure"),
        (d[15], "Threat"), (push, "PushCol"), (d[19], "Shield"),
        (night, "Night"), (tm, "Tm"), (heat, "Heat"), (cur, "Cur"),
    )
    for src, pin in wiring:
        connect(src, node, [pin])
    rgb = mask(mat, node, True, True, True, False, 0, 0)
    alpha = mask(mat, node, False, False, False, True, 0, 160)
    finish_substrate(mat, rgb, alpha, over=True)


@stage("M_Glow")
def build_glow(mpc):
    mat = fresh_material("M_Glow", MAT_DIR)
    base_flags(mat, translucent=True, instanced=True)
    d = [idata(mat, i, 0.0, -1800, i * 110) for i in range(9)]
    pw = world_xy(mat, -1500, -300)
    fill = vec3(mat, d[0], d[1], d[2], -1400, 0)
    cw = vec2(mat, d[5], d[6], -1400, 700)
    night = mpc_param(mat, mpc, "Night", -1500, -100)
    node = custom(mat, "node glow", read_hlsl("glow"),
                  ["PW", "CW", "Rad", "Fill", "Flash", "Hover", "Night", "Pow"],
                  unreal.CustomMaterialOutputType.CMOT_FLOAT3, -400, 300)
    for src, pin in ((pw, "PW"), (cw, "CW"), (d[7], "Rad"), (fill, "Fill"), (d[3], "Flash"), (d[4], "Hover"),
                     (night, "Night"), (d[8], "Pow")):
        connect(src, node, [pin])
    finish_substrate(mat, node, alpha=None, over=False)   # additive: default transmittance 1


@stage("M_Edge")
def build_edge(mpc):
    mat = fresh_material("M_Edge", MAT_DIR)
    base_flags(mat, translucent=True, instanced=True)
    d = [idata(mat, i, 0.0, -1800, i * 110) for i in range(13)]
    pw = world_xy(mat, -1500, -300)
    col = vec3(mat, d[0], d[1], d[2], -1400, 0)
    p0 = vec2(mat, d[8], d[9], -1400, 900)
    p1 = vec2(mat, d[10], d[11], -1400, 1100)
    night = mpc_param(mat, mpc, "Night", -1500, -100)
    tm = mpc_param(mat, mpc, "Time", -1500, 0)
    node = custom(mat, "influence edge", read_hlsl("edge"),
                  ["PW", "P0", "P1", "Bulge", "Col", "Base", "Pulse", "Seed", "Speed", "Wid", "Night", "Tm"],
                  unreal.CustomMaterialOutputType.CMOT_FLOAT4, -400, 300)
    for src, pin in ((pw, "PW"), (p0, "P0"), (p1, "P1"), (d[12], "Bulge"), (col, "Col"), (d[3], "Base"), (d[4], "Pulse"),
                     (d[5], "Seed"), (d[6], "Speed"), (d[7], "Wid"), (night, "Night"), (tm, "Tm")):
        connect(src, node, [pin])
    rgb = mask(mat, node, True, True, True, False, 0, 0)
    alpha = mask(mat, node, False, False, False, True, 0, 160)
    finish_substrate(mat, rgb, alpha, over=True)


def main():
    mpc = build_mpc()
    if mpc is not None:
        build_bg(mpc)
        build_node(mpc)
        build_glow(mpc)
        build_edge(mpc)
    finish()


main()
