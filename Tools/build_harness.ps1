# Builds the Shift Happens standalone sim harness with MSVC (vcvars64.bat) into Tools\SimHarness\out.
#   .\Tools\build_harness.ps1                 build only
#   .\Tools\build_harness.ps1 -Run test       build, then run: harness.exe test
#   .\Tools\build_harness.ps1 -Run "batch --seeds 200"
param(
    [string]$Run = "",
    [ValidateSet("Release", "Debug")][string]$Config = "Release",
    [string]$VsRoot = "C:\Program Files\Microsoft Visual Studio\2022\Community"
)

$ErrorActionPreference = "Stop"
$toolsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$harnessDir = Join-Path $toolsDir "SimHarness"
$coreDir = Join-Path $harnessDir "core"
$outDir = Join-Path $harnessDir "out"
$vcvars = Join-Path $VsRoot "VC\Auxiliary\Build\vcvars64.bat"

if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found: $vcvars" }
New-Item -ItemType Directory -Force $outDir | Out-Null
New-Item -ItemType Directory -Force (Join-Path $outDir "strict") | Out-Null

$coreCpp = Get-ChildItem $coreDir -Filter *.cpp | ForEach-Object { '"' + $_.FullName + '"' }
$coreCppList = $coreCpp -join " "

if ($Config -eq "Release") { $opt = "/O2 /DNDEBUG" } else { $opt = "/Od /Zi /RTC1" }

# 1) Core compile check: warnings-as-errors, exceptions OFF (as in the Unreal module), shadowing as errors.
#    2) Harness build.
$bat = Join-Path $outDir "build.cmd"
$lines = @(
    "@echo off",
    "call `"$vcvars`" >nul 2>&1",
    "cd /d `"$outDir`"",
    "echo [1/2] core strict check (/W4 /WX, no exceptions)",
    "cl /nologo /c /std:c++17 /W4 /WX /we4456 /we4457 /we4458 /we4459 /EHs-c- /D_HAS_EXCEPTIONS=0 /I`"$coreDir`" /Fo`"$outDir\strict\\`" $coreCppList",
    "if errorlevel 1 exit /b 1",
    "echo [2/2] harness",
    "cl /nologo /std:c++17 /W3 /EHsc $opt /I`"$coreDir`" /I`"$harnessDir`" /Fe:`"$outDir\harness.exe`" /Fo`"$outDir\\`" `"$harnessDir\main.cpp`" $coreCppList",
    "if errorlevel 1 exit /b 1"
)
Set-Content -Path $bat -Value $lines -Encoding ASCII

& cmd.exe /c "`"$bat`""
if ($LASTEXITCODE -ne 0) { throw "build failed" }
Write-Host "built $outDir\harness.exe"

if ($Run -ne "") {
    $argList = $Run -split " "
    & "$outDir\harness.exe" @argList
    exit $LASTEXITCODE
}
