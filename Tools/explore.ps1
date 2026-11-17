# Looks for dominant strategies before a player does (headless, one process): a random search plus hill climb over the Explorer bot's knobs, per jam,
# validated against the honest greedy bot on fresh seeds. See UShiftExploreCommandlet.
#   .\Tools\explore.ps1                                   all jams, 30 random samples + 10 climb steps, 40 runs each (about 20 minutes)
#   .\Tools\explore.ps1 -Jam 2 -Samples 60 -Runs 60       a deeper search on one jam
#   .\Tools\explore.ps1 -Set RallyCooldown=6,RallyLock=4  the same search under a tuning change (any sim, jam or policy field by name)
# The last lines per jam are "Explore: jam=N best-response ... = +X points" and the configuration that achieves it; the full table is
# Saved\Balance\explore_<time>.csv. A jam whose best response beats greedy by more than the limit in Data\BalanceTargets.csv has a strategy that is too strong.
param(
	[int]$Samples = 30,
	[int]$Climb = 10,
	[int]$Runs = 40,
	[string]$Jam = 'all',
	[int]$Seed0 = 1000,
	[int]$Seed = 1,                 # the search's own random seed
	[string[]]$Set = @(),
	[string[]]$Build = @(),
	[string]$Tag = '',
	[switch]$NoStateTree,
	[switch]$NoMods,
	[string]$EngineRoot = $(if ($env:SHIFT_UE_ROOT) { $env:SHIFT_UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' })
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Exe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$args = @("`"$(Join-Path $Root 'ShiftHappens.uproject')`"", '-run=ShiftExplore', "-Samples=$Samples", "-Climb=$Climb", "-Runs=$Runs", "-Jam=$Jam", "-Seed0=$Seed0", "-Seed=$Seed", '-unattended', '-nosplash', '-nop4', '-NoSound', '-stdout', '-FullStdOutLogOutput', '-nullrhi')
$buildArg = ($Build | ForEach-Object { $_ -split ',' } | Where-Object { $_ }) -join ','
if ($buildArg) { $args += "-Build=$buildArg" }
$setArg = ($Set | ForEach-Object { $_ -split ',' } | Where-Object { $_ }) -join ','
if ($setArg) { $args += "-Set=$setArg" }
if ($NoStateTree) { $args += '-NoStateTree' }
if ($NoMods) { $args += '-NoMods' }
$log = Join-Path $Root $(if ($Tag) { "Saved\explore_$Tag.log" } else { 'Saved\explore_last.log' })
New-Item -ItemType Directory -Force -Path (Split-Path $log) | Out-Null
$p = Start-Process -FilePath $Exe -ArgumentList $args -PassThru -NoNewWindow -RedirectStandardOutput $log -RedirectStandardError "$log.err"
$p.WaitForExit()
Select-String -Path $log -Pattern 'LogShift: (Display: )?(Explore|Error)' | ForEach-Object { ($_.Line -replace '^\[[^\]]*\]\[[^\]]*\]LogShift: (Display: )?', '') }
exit $p.ExitCode
