# Frame-exact headless screenshots of one scene, for the itch.io images (Tools\itch\build.ps1 calls this once per scene).
# One editor start per scene, no window, no sound, no OBS, no injected input: the game renders off screen at a fixed 60 fps time step, so a frame is 1/60 s of game time
# and two runs of the same scene give the same pictures. The scene is either a trailer-director cue file (Tools\itch\cues\<Scene>.csv: the same commands as Data\Trailer.csv,
# times in bars of the music, 1 bar = 2.1333 s, 1 s = 0.46875 bar) or a plain run described by -Extra (for example -ShiftBot=greedy -ShiftJam=2 -ShiftSeed=7 -ShiftWarp=110).
#   Tools\itch\shoot.ps1 -Name flip -Scene flip -Seconds 5                       Saved\Itch\frames\flip\f_0010.png ... (the number is the film time in hundredths of a second)
#   Tools\itch\shoot.ps1 -Name hero_day -Seconds 0.1 -Fps 10 -Extra @('-ShiftNoUI','-ShiftBot=greedy','-ShiftJam=2','-ShiftSeed=7','-ShiftWarp=110','-ShiftNight=0')
param(
	[Parameter(Mandatory = $true)][string]$Name,
	[string]$Scene = '',
	[double]$Start = 0.1,        # film time of the first frame in seconds (the game takes nothing before 0.1 s)
	[double]$Seconds = 4,        # length of the scene from -Start
	[double]$Fps = 20,           # frames per second of film; 20 gives a GIF delay of 5 cs
	[double]$Offset = 0,         # shifts every frame time (two half-offset runs interleave into a double frame rate)
	[string[]]$Extra = @(),
	[int]$ResX = 1920,
	[int]$ResY = 1080,
	[int]$TimeoutSec = 900
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$Out = Join-Path $Root "Saved\Itch\frames\$Name"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
Get-ChildItem $Out -Filter 'f_*.png' -ErrorAction SilentlyContinue | Remove-Item -Force
$inv = [Globalization.CultureInfo]::InvariantCulture

# frame times on the 1/60 s grid of the fixed time step
$n = [int][Math]::Floor($Seconds * $Fps + 1e-6)
$times = @(0..($n - 1) | ForEach-Object { [Math]::Max(0.1, [Math]::Round(($Start + $Offset + $_ / $Fps) * 60.0) / 60.0) })
$list = ($times | ForEach-Object { $_.ToString('0.####', $inv) }) -join ','

# DisableAllScreenMessages: the engine prints "Preparing Textures" on the screen in the first frames of a run
$flags = @('-UseFixedTimeStep', '-FPS=60', "-ShiftShotTimes=$list", '-ExecCmds=DisableAllScreenMessages')
if ($Scene) {
	$cues = Join-Path $PSScriptRoot "cues\$Scene.csv"
	if (-not (Test-Path $cues)) { throw "no cue file $cues" }
	$flags = @("-ShiftTrailer=$cues", '-ShiftNoTutorial') + $flags
}
$flags += $Extra
$sw = [Diagnostics.Stopwatch]::StartNew()
& (Join-Path $Root 'Tools\run.ps1') -Shot (Join-Path $Out 'f.png') -Quit -ResX $ResX -ResY $ResY -LogFile (Join-Path $Out 'shoot.log') -TimeoutSec $TimeoutSec -Extra $flags | Out-Null
$code = $LASTEXITCODE
$got = @(Get-ChildItem $Out -Filter 'f_*.png' -ErrorAction SilentlyContinue).Count
# the shot files are named after the requested time in hundredths; some of 0.0167-grid times round to the same hundredth only if the frame rate is above 100
"{0}: {1} of {2} frames in {3}s (exit {4}) -> {5}" -f $Name, $got, $n, [int]$sw.Elapsed.TotalSeconds, $code, $Out
if ($got -lt $n) { Write-Warning "$($n - $got) frames are missing: see $Out\shoot.log" }
