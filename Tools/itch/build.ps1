# One command for the itch.io images: shoots every scene headless, then makes, encodes and checks everything. Output: Saved\Itch\upload (git-ignored), report in Saved\Itch\report.md.
#   Tools\itch\build.ps1                  everything (about 8 minutes, mostly editor starts)
#   Tools\itch\build.ps1 -SkipShoot       only re-make from the frames already in Saved\Itch\frames (logo, promos, background, encodes, checks: about 5 minutes)
#   Tools\itch\build.ps1 -MaxMB 2         a lower size cap for every GIF if the uploader refuses a file (itch.io states no limit; the default is 3)
#   Tools\itch\build.ps1 -Keep            keep the frames afterwards (about 1 GB)
# Everything runs through Tools\run.ps1: no window, no sound, no OBS, no injected input. Build the game first (Tools\build.ps1) when the sources changed.
param(
	[double]$MaxMB = 3,
	[switch]$SkipShoot,
	[switch]$Keep
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$shoot = Join-Path $PSScriptRoot 'shoot.ps1'
$hero = @('-ShiftNoUI', '-ShiftNoTutorial', '-ShiftScreen=play', '-ShiftBot=greedy', '-ShiftJam=2', '-ShiftSeed=7', '-ShiftSpeed=8')
if (-not $SkipShoot) {
	# the hero board: one sim state at 22 s of film time, rendered in both themes at 4K so the crops stay sharp (same sim, only the theme differs)
	& $shoot -Name hero_day -Start 22 -Seconds 1 -Fps 1 -ResX 3840 -ResY 2160 -Extra ($hero + '-ShiftNight=0')
	& $shoot -Name hero_night -Start 22 -Seconds 1 -Fps 1 -ResX 3840 -ResY 2160 -Extra ($hero + '-ShiftNight=1')
	# the screenshot scenes (cue files in Tools\itch\cues), 20 fps
	& $shoot -Name flip -Scene flip -Start 0.3 -Seconds 4.8 -Fps 20
	& $shoot -Name plug -Scene plug -Start 0.5 -Seconds 5.8 -Fps 20
	& $shoot -Name night -Scene night -Start 0.5 -Seconds 5.5 -Fps 20
	& $shoot -Name modeye -Scene modeye -Start 0.5 -Seconds 4.9 -Fps 20
	& $shoot -Name results -Scene results -Start 9.5 -Seconds 1 -Fps 1
}
& python (Join-Path $PSScriptRoot 'make_images.py') all --max-mb $MaxMB
$code = $LASTEXITCODE
if (-not $Keep) {
	foreach ($d in 'flip', 'plug', 'night', 'modeye', 'results', 'hero_day', 'hero_night') { Remove-Item (Join-Path $Root "Saved\Itch\frames\$d") -Recurse -Force -ErrorAction SilentlyContinue }
	Remove-Item (Join-Path $Root 'Saved\Itch\tmp') -Recurse -Force -ErrorAction SilentlyContinue
}
"files: $(Join-Path $Root 'Saved\Itch\upload')"
exit $code
