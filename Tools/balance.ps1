# Headless balance runs against the real behaviour layer (StateTrees): win / DQ rates, rank, share and the thesis metrics per jam and scripted player.
#   .\Tools\balance.ps1 -Runs 200                       all jams, all bots
#   .\Tools\balance.ps1 -Jam 2 -Bot greedy,sock -Runs 300 -Build 4,4,4
#   .\Tools\balance.ps1 -NoStateTree                    the native stand-in policies instead (what the harness calibrated)
param(
	[int]$Runs = 100,
	[string]$Jam = 'all',
	[string]$Bot = 'all',
	[string]$Build = '',
	[int]$Seed0 = 1000,
	[switch]$NoStateTree,
	[switch]$NoMods,
	[string]$EngineRoot = $(if ($env:SHIFT_UE_ROOT) { $env:SHIFT_UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' })
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Exe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$args = @("`"$(Join-Path $Root 'ShiftHappens.uproject')`"", '-run=ShiftBalance', "-Runs=$Runs", "-Jam=$Jam", "-Bot=$Bot", "-Seed0=$Seed0", '-unattended', '-nosplash', '-nop4', '-NoSound', '-stdout', '-FullStdOutLogOutput', '-nullrhi')
if ($Build) { $args += "-Build=$Build" }
if ($NoStateTree) { $args += '-NoStateTree' }
if ($NoMods) { $args += '-NoMods' }
$log = Join-Path $Root 'Saved\balance_last.log'
New-Item -ItemType Directory -Force -Path (Split-Path $log) | Out-Null
$p = Start-Process -FilePath $Exe -ArgumentList $args -PassThru -NoNewWindow -RedirectStandardOutput $log -RedirectStandardError "$log.err"
$p.WaitForExit()
Select-String -Path $log -Pattern 'LogShift: (Display: )?(Balance|jam=)' | ForEach-Object { ($_.Line -replace '^\[[^\]]*\]\[[^\]]*\]LogShift: (Display: )?', '') }
exit $p.ExitCode
