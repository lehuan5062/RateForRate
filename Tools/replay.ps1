# Replays recorded jams (Saved\Recordings\*.shiftrec.txt, written by the game for every jam a person plays) headlessly and reports how each went: the recorded result against the
# replayed one (same build and tuning end in the same state hash), the clicks by kind and target, outcomes, the first celebrity taken, the shares along the jam.
#   .\Tools\replay.ps1                                   every recording in Saved\Recordings
#   .\Tools\replay.ps1 -Rec D:\somewhere\shift_jam3_123_20261120-101500_won.shiftrec.txt
#   .\Tools\replay.ps1 -Dir D:\from_the_tester           every recording in a folder (what the playtester sent)
#   .\Tools\replay.ps1 -Rec <file> -Set CelebDrainMult=0.4,CelebGate=0.3   the same clicks under other tuning: "what would this jam have been with my change"
#   .\Tools\replay.ps1 -Rec <file> -Build 4,4,4                              the same clicks with another build (Quality,Thumbnail,Network hours)
# The log is Saved\replay_last.log (or replay_<Tag>.log).
param(
	[string]$Rec = '',
	[string]$Dir = '',
	[string[]]$Set = @(),
	[string[]]$Build = @(),
	[string]$Tag = '',
	[switch]$NoStateTree,
	[string]$EngineRoot = $(if ($env:SHIFT_UE_ROOT) { $env:SHIFT_UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' })
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Exe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$args = @("`"$(Join-Path $Root 'ShiftHappens.uproject')`"", '-run=ShiftReplay', '-unattended', '-nosplash', '-nop4', '-NoSound', '-stdout', '-FullStdOutLogOutput', '-nullrhi')
if ($Rec) { $args += "-Rec=`"$((Resolve-Path $Rec).Path)`"" }
if ($Dir) { $args += "-Dir=`"$((Resolve-Path $Dir).Path)`"" }
$setArg = ($Set | ForEach-Object { $_ -split ',' } | Where-Object { $_ }) -join ','
if ($setArg) { $args += "-Set=$setArg" }
$buildArg = ($Build | ForEach-Object { $_ -split ',' } | Where-Object { $_ }) -join ','
if ($buildArg) { $args += "-Build=$buildArg" }
if ($NoStateTree) { $args += '-NoStateTree' }
$log = Join-Path $Root $(if ($Tag) { "Saved\replay_$Tag.log" } else { 'Saved\replay_last.log' })
New-Item -ItemType Directory -Force -Path (Split-Path $log) | Out-Null
$p = Start-Process -FilePath $Exe -ArgumentList $args -PassThru -NoNewWindow -RedirectStandardOutput $log -RedirectStandardError "$log.err"
$p.WaitForExit()
Select-String -Path $log -Pattern 'LogShift: (Display: |Error: |Warning: )?Replay:' | ForEach-Object { ($_.Line -replace '^\[[^\]]*\]\[[^\]]*\]LogShift: (Display: )?', '') }
exit $p.ExitCode
