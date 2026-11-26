# Builds a Rate for Rate target with Unreal Build Tool (UE 5.8).
#   Tools\build.ps1                          -> RateForRateEditor, Win64, Development
#   Tools\build.ps1 -Target RateForRate -Config Shipping
param(
	[string]$Target = 'RateForRateEditor',
	[string]$Config = 'Development',
	[string]$EngineRoot = $(if ($env:SHIFT_UE_ROOT) { $env:SHIFT_UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' })
)
$ErrorActionPreference = 'Stop'
$Project = (Resolve-Path (Join-Path $PSScriptRoot '..\RateForRate.uproject')).Path
$BuildBat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
if (-not (Test-Path $BuildBat)) { throw "Build.bat not found at $BuildBat (set SHIFT_UE_ROOT)" }

Write-Host "Building $Target Win64 $Config with $EngineRoot"
& $BuildBat $Target Win64 $Config "-Project=$Project" -WaitMutex -FromMsBuild
exit $LASTEXITCODE
