# Headless balance runs against the real behaviour layer (StateTrees): win / DQ rates, rank, share and the thesis metrics per jam and scripted player.
#   .\Tools\balance.ps1 -Runs 200                       all jams, all bots (idle random greedy sock finisher late)
#   .\Tools\balance.ps1 -Jam 2 -Bot greedy,sock -Runs 300 -Build 4,4,4
#   .\Tools\balance.ps1 -Bot finisher,late -Set LateSeconds=24,SockHeat=24      override fields by name (sim Params, jam or policy fields) without a rebuild
#   .\Tools\balance.ps1 -NoStateTree                    the native stand-in policies instead (what the harness calibrated)
# -Bot, -Build and -Set take comma separated lists. They are string arrays here on purpose: a plain [string] parameter would flatten "greedy,sock" to
# "greedy sock" and "4,0,8" to "4 0 8" and the engine would silently run only the first bot or fall back to build 4/4/4. The first output line echoes what the engine parsed.
param(
	[int]$Runs = 100,
	[string]$Jam = 'all',
	[string[]]$Bot = @('all'),
	[string[]]$Build = @(),
	[string[]]$Set = @(),
	[int]$Seed0 = 1000,
	[string]$Tag = '',          # log suffix (Saved\balance_<Tag>.log): lets several experiments run side by side
	[switch]$NoStateTree,
	[switch]$NoMods,
	[string]$EngineRoot = $(if ($env:SHIFT_UE_ROOT) { $env:SHIFT_UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' })
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Exe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$botArg = ($Bot | ForEach-Object { $_ -split ',' } | Where-Object { $_ }) -join ','
$args = @("`"$(Join-Path $Root 'RateForRate.uproject')`"", '-run=ShiftBalance', "-Runs=$Runs", "-Jam=$Jam", "-Bot=$botArg", "-Seed0=$Seed0", '-unattended', '-nosplash', '-nop4', '-NoSound', '-stdout', '-FullStdOutLogOutput', '-nullrhi')
$buildArg = ($Build | ForEach-Object { $_ -split ',' } | Where-Object { $_ }) -join ','
if ($buildArg) { $args += "-Build=$buildArg" }
$setArg = ($Set | ForEach-Object { $_ -split ',' } | Where-Object { $_ }) -join ','
if ($setArg) { $args += "-Set=$setArg" }
if ($NoStateTree) { $args += '-NoStateTree' }
if ($NoMods) { $args += '-NoMods' }
$log = Join-Path $Root $(if ($Tag) { "Saved\balance_$Tag.log" } else { 'Saved\balance_last.log' })
New-Item -ItemType Directory -Force -Path (Split-Path $log) | Out-Null
$p = Start-Process -FilePath $Exe -ArgumentList $args -PassThru -NoNewWindow -RedirectStandardOutput $log -RedirectStandardError "$log.err"
$p.WaitForExit()
Select-String -Path $log -Pattern 'LogShift: (Display: )?(Balance|jam=)' | ForEach-Object { ($_.Line -replace '^\[[^\]]*\]\[[^\]]*\]LogShift: (Display: )?', '') }
exit $p.ExitCode
