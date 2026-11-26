# Packages a Windows build of Rate for Rate with the engine's BuildCookRun (UAT). Cooked, staged and archived into Saved\PackageDev (git-ignored).
#   Tools\package.ps1                        Development build: the scripted trailer (Source\ShiftHappens\Trailer) and the QA flags are in, no editor around the game.
#                                            Tools\trailer\shoot.ps1 records from it.
#   Tools\package.ps1 -Config Shipping       the build for itch.io (no trailer code); archived into Saved\PackageShipping
# The first cook compiles the shaders and takes a while; later runs only recook what changed. An open Unreal Editor does not matter. The result is
# Saved\PackageDev\Windows\RateForRate.exe, a launcher; the game itself is Windows\RateForRate\Binaries\Win64\RateForRate.exe (what shoot.ps1 runs).
param(
	[ValidateSet('Development', 'Shipping')][string]$Config = 'Development',
	[string]$LogFile = '',
	[string]$EngineRoot = $(if ($env:SHIFT_UE_ROOT) { $env:SHIFT_UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' })
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Project = Join-Path $Root 'RateForRate.uproject'
$Uat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
if (-not (Test-Path $Uat)) { throw "RunUAT.bat not found at $Uat (set SHIFT_UE_ROOT)" }
$Out = Join-Path $Root $(if ($Config -eq 'Development') { 'Saved\PackageDev' } else { 'Saved\PackageShipping' })
if (-not $LogFile) { $LogFile = Join-Path $Root "Saved\package_$($Config.ToLower()).log" }
New-Item -ItemType Directory -Force -Path (Split-Path $LogFile) | Out-Null

$uatArgs = @('BuildCookRun', "-project=`"$Project`"", '-noP4', '-platform=Win64', "-clientconfig=$Config", '-cook', '-build', '-stage', '-pak', '-archive', "-archivedirectory=`"$Out`"", '-utf8output', '-unattended')
Write-Host "RunUAT $($uatArgs -join ' ')"
& $Uat @uatArgs *> $LogFile
$code = $LASTEXITCODE
Select-String -Path $LogFile -Pattern 'BUILD SUCCESSFUL|BUILD FAILED|Error:|error C' | Select-Object -Last 8 | ForEach-Object { $_.Line.Trim() }
$Exe = Join-Path $Out 'Windows\RateForRate.exe'
if ($code -eq 0 -and (Test-Path $Exe)) { Write-Host "packaged: $Exe  (log: $LogFile)" } else { Write-Host "package FAILED (exit $code), log: $LogFile"; exit $(if ($code -ne 0) { $code } else { 1 }) }
