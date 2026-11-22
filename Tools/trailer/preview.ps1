# Authoring aid for the scripted trailer run (Data/Trailer.csv): plays the run headless (no window, no sound) and takes a screenshot at each film time,
# then tiles them into one contact sheet with the time on each tile. One editor start for all the times.
#   Tools\trailer\preview.ps1 -Bars 0.5,1.6,2.6,3.6        film positions in bars of the music (1 bar = 2.1333 s)
#   Tools\trailer\preview.ps1 -Seconds 1,5,9 -Cols 3       or in seconds
# In a headless run there is no music clock to wait for, so the film time is the game time (plus a few milliseconds). Output: Saved\Trailer\auth\sheet.png
param(
	[double[]]$Bars = @(),
	[double[]]$Seconds = @(),
	[int]$Cols = 4,
	[int]$TileWidth = 480,
	[string]$Name = 'sheet',
	[string]$Cues = ''
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$Out = Join-Path $Root 'Saved\Trailer\auth'
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$inv = [Globalization.CultureInfo]::InvariantCulture
$bar = 240.0 / 112.5
$times = @($Seconds) + @($Bars | ForEach-Object { $_ * $bar })
if ($times.Count -eq 0) { throw 'give -Bars or -Seconds' }
$times = @($times | Sort-Object)
$prefix = Join-Path $Out $Name
Get-ChildItem $Out -Filter "${Name}_*.png" -ErrorAction SilentlyContinue | ForEach-Object { Remove-Item $_.FullName -Force }
$list = ($times | ForEach-Object { [Math]::Max(0.1, $_).ToString('0.###', $inv) }) -join ','
$trailerFlag = if ($Cues) { "-ShiftTrailer=$Cues" } else { '-ShiftTrailer' }
$sw = [Diagnostics.Stopwatch]::StartNew()
& (Join-Path $Root 'Tools\run.ps1') -Shot "$prefix.png" -Quit -ResX 1920 -ResY 1080 -LogFile (Join-Path $Out "$Name.log") -TimeoutSec 600 `
	-Extra @($trailerFlag, '-UseFixedTimeStep', '-FPS=60', "-ShiftShotTimes=$list") | Out-Null
"run took $([int]$sw.Elapsed.TotalSeconds)s (exit $LASTEXITCODE)"

# the contact sheet (a font copy sits next to the pictures so ffmpeg can use a relative path)
$font = Join-Path $Out 'Roboto-Bold.ttf'
if (-not (Test-Path $font)) { Copy-Item 'C:\Program Files\Epic Games\UE_5.8\Engine\Content\Slate\Fonts\Roboto-Bold.ttf' $font }
Push-Location $Out
try {
	$inputs = @(); $f = @(); $l = @(); $k = 0
	foreach ($t in $times) {
		$file = "{0}_{1:0000}.png" -f $Name, [int][Math]::Round([Math]::Max(0.1, $t) * 100)
		if (-not (Test-Path $file)) { Write-Warning "missing $file"; continue }
		$inputs += @('-i', $file)
		$label = ('{0:0.00}s  bar {1:0.00}' -f $t, ($t / $bar))
		$f += "[$k]scale=${TileWidth}:-1,drawtext=fontfile=Roboto-Bold.ttf:text='$label':x=6:y=6:fontsize=20:fontcolor=yellow:box=1:boxcolor=black@0.7[t$k]"
		$l += "[t$k]"; $k++
	}
	if ($k -eq 0) { throw 'no screenshots were taken: see the log' }
	$h = [int]($TileWidth * 9 / 16)
	$lay = (0..($k - 1) | ForEach-Object { "$(($_ % $Cols) * $TileWidth)_$([int]([Math]::Floor($_ / $Cols) * $h))" }) -join '|'
	if ($k -eq 1) { $fc = ($f -join ';').Replace('[t0]', '[out]') } else { $fc = ($f -join ';') + ';' + ($l -join '') + "xstack=inputs=${k}:layout=$lay[out]" }
	& ffmpeg -v error -y @inputs -filter_complex $fc -map '[out]' -frames:v 1 "$Name.png"
	"sheet: $(Join-Path $Out "$Name.png")"
}
finally { Pop-Location }
