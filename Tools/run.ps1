# Runs Rate for Rate as a game from the UE 5.8 editor binary (no packaging needed). HEADLESS by default: no window, no console window, no sound.
#   Tools\run.ps1 -Shot D:\GameOff111609\Saved\Shots\s0.png -ShotDelay 6 -Quit      headless screenshot run (log: Saved\run_<name>.log)
#   Tools\run.ps1 -Visible -Sound                                                    a normal windowed run for playing it yourself
# Headless means -RenderOffScreen: on Windows that replaces the OS application with the null application (no window is ever created) while D3D12
# still renders into off-screen buffers, so screenshots work. UnrealEditor-Cmd.exe is the console-subsystem binary and its output goes to the log file,
# so nothing pops up on the desktop. Sound is off unless -Sound is given (audio checks use it deliberately, at a low -ShiftMasterVolume).
param(
	[string]$Shot = '',
	[double]$ShotDelay = 4,
	[switch]$Quit,
	[switch]$Visible,
	[switch]$Sound,
	[switch]$OffScreen,   # accepted for older call sites: headless is already the default
	[switch]$NoSound,     # accepted for older call sites: silence is already the default
	[int]$ResX = 1280,
	[int]$ResY = 720,
	[string[]]$Extra = @(),
	[string]$LogFile = '',
	[int]$TimeoutSec = 900,
	[string]$EngineRoot = $(if ($env:SHIFT_UE_ROOT) { $env:SHIFT_UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' })
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Project = Join-Path $Root 'RateForRate.uproject'
$Exe = Join-Path $EngineRoot $(if ($Visible) { 'Engine\Binaries\Win64\UnrealEditor.exe' } else { 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe' })
if (-not (Test-Path $Exe)) { throw "$Exe not found (set SHIFT_UE_ROOT)" }

$args = @("`"$Project`"", '-game', "-ResX=$ResX", "-ResY=$ResY", '-nosplash', '-nop4', '-unattended')
if ($Visible) { $args += '-windowed' } else { $args += @('-RenderOffScreen', '-ForceRes', '-stdout', '-FullStdOutLogOutput') }
if (-not $Sound) { $args += '-NoSound' }
if ($Shot) { $args += "-ShiftShot=`"$Shot`"", "-ShiftShotDelay=$ShotDelay" }
if ($Quit) { $args += '-ShiftQuit' }
$args += $Extra

if ($Visible) {
	Write-Host "$Exe $($args -join ' ')"
	$p = Start-Process -FilePath $Exe -ArgumentList $args -PassThru -Wait
	exit $p.ExitCode
}

if (-not $LogFile) { $LogFile = Join-Path $Root 'Saved\run_last.log' }
New-Item -ItemType Directory -Force -Path (Split-Path $LogFile) | Out-Null
$Err = "$LogFile.err"
$p = Start-Process -FilePath $Exe -ArgumentList $args -PassThru -NoNewWindow -RedirectStandardOutput $LogFile -RedirectStandardError $Err
if (-not $p.WaitForExit($TimeoutSec * 1000)) {
	Write-Host "TIMEOUT after $TimeoutSec s, killing the game (pid $($p.Id))"
	Stop-Process -Id $p.Id -Force
	exit 124
}
exit $p.ExitCode
