"""Compile the Custom-node HLSL bodies standalone with fxc, so syntax errors show up in seconds instead of after an editor run.

    python -I Tools/py/check_hlsl.py            (from the repo root)

Each .hlsl file in Tools/py/hlsl is wrapped in a pixel shader whose parameters are the Custom node inputs (declared by a
'// Inputs' convention below), then compiled with the Windows SDK's fxc.exe (ps_5_0, only used to validate syntax and types).
"""
import glob
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FXC_CANDIDATES = [
    r"C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\fxc.exe",
]

# Custom-node input signatures per shader: (type, name) in the order they are declared in the material.
SIGNATURES = {
    "node": [("float2", "PW"), ("float2", "CW"), ("float", "Rad"), ("float3", "Fill"), ("float", "Conv"), ("float", "Lock"),
             ("float", "Gate"), ("float", "Hover"), ("float", "Flash"), ("float", "Role"), ("float", "Expr"),
             ("float", "Sock"), ("float", "Pressure"), ("float", "Night"), ("float", "Tm"), ("float", "Heat"), ("float2", "Cur")],
    "edge": [("float2", "PW"), ("float2", "P0"), ("float2", "P1"), ("float", "Bulge"), ("float3", "Col"), ("float", "Base"),
             ("float", "Pulse"), ("float", "Seed"), ("float", "Speed"), ("float", "Wid"), ("float", "Night"), ("float", "Tm")],
    "glow": [("float2", "PW"), ("float2", "CW"), ("float", "Rad"), ("float3", "Fill"), ("float", "Flash"), ("float", "Hover"),
             ("float", "Night"), ("float", "Pow")],
    "bg": [("float2", "PW"), ("float3", "PaperA"), ("float3", "PaperB"), ("float3", "InkA"), ("float3", "InkB"),
           ("float", "Night"), ("float", "Tm"), ("float", "Beat")],
}

# Custom nodes that return float3 instead of float4 (the wrapper's return type follows this).
RETURNS_FLOAT3 = {"glow", "bg"}


def find_fxc():
    for path in FXC_CANDIDATES:
        if os.path.exists(path):
            return path
    raise SystemExit("fxc.exe not found; install the Windows 10/11 SDK")


def wrap(name, body):
    sig = SIGNATURES[name]
    params = ", ".join("{} {}".format(t, n) for t, n in sig)
    is3 = name in RETURNS_FLOAT3
    call = "float4(Shade({}), 1.0)" if is3 else "Shade({})"
    return "{} Shade({}) {{\n{}\n}}\n\nfloat4 main(float4 pos : SV_Position) : SV_Target\n{{\n    return {};\n}}\n".format(
        "float3" if is3 else "float4", params, body,
        call.format(", ".join(_default(t, n) for t, n in sig)))


def _default(t, n):
    if n == "PW":
        return "pos.xy * 0.1"
    if t == "float":
        return "0.5"
    if t == "float2":
        return "float2(1.0, 2.0)"
    return "float3(0.2, 0.7, 0.6)"


def main():
    fxc = find_fxc()
    failed = False
    for path in sorted(glob.glob(os.path.join(HERE, "hlsl", "*.hlsl"))):
        name = os.path.splitext(os.path.basename(path))[0]
        if name not in SIGNATURES:
            print("skip", name)
            continue
        with open(path, "r", encoding="utf-8") as fh:
            body = fh.read()
        src = wrap(name, body)
        with tempfile.TemporaryDirectory() as tmp:
            src_path = os.path.join(tmp, name + "_wrapped.hlsl")
            with open(src_path, "w", encoding="utf-8") as fh:
                fh.write(src)
            proc = subprocess.run([fxc, "/nologo", "/T", "ps_5_0", "/E", "main", "/Fo", os.path.join(tmp, "out.bin"), src_path],
                                  capture_output=True, text=True)
        status = "OK" if proc.returncode == 0 else "FAILED"
        print("{}: {}".format(name, status))
        if proc.returncode != 0:
            failed = True
            print(proc.stdout.strip())
            print(proc.stderr.strip())
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
