# Makes the final trailer files from a recording of the scripted run (Tools\trailer\shoot.ps1): cuts it on the first beat of the music, brings the sound to
# -14 LUFS with one static gain and a limiter, encodes 1080p60 H.264 + AAC, and writes a poster frame. Everything goes to Saved\Trailer\Dist (git-ignored).
#   Tools\trailer\finalize.ps1                  the newest Saved\Trailer\raw\take_*.mkv
#   Tools\trailer\finalize.ps1 -Take <mkv>      another recording
# The cut is 23 bars (49.1 s) of the music plus the 0.4 s fade to black of the end cue; the sound fades out over the last 0.6 s. The picture and the sound
# keep the offset they have in the recording (the latency of the take was calibrated by shoot.ps1), both streams start on the same frame at or before
# the music's first beat. Nothing here writes a .wav: ffmpeg pipes raw samples to Python and encodes straight to the MP4.
param(
	[string]$Take = '',
	[double]$Bars = 23,
	[double]$Tail = 0.4,
	[double]$TargetLufs = -14,
	[double]$LimitDb = -2,
	[double]$PosterBar = 22.6,
	[int]$Crf = 20,   # 19 gave 30 MiB for 25 bars; the file has to stay under about 30 MiB
	[string]$Name = 'trailer45'
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$Work = Join-Path $Root 'Saved\Trailer'
$Dist = Join-Path $Work 'Dist'
$Review = Join-Path $Work 'review'
New-Item -ItemType Directory -Force -Path $Dist, $Review | Out-Null
$inv = [Globalization.CultureInfo]::InvariantCulture
function F([double]$v, [string]$fmt = '0.0000') { $v.ToString($fmt, $inv) }
$bar = 240.0 / 112.5

if (-not $Take) {
	$latest = Get-ChildItem (Join-Path $Work 'raw') -Filter 'take_*.mkv' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
	if (-not $latest) { throw 'no Saved\Trailer\raw\take_*.mkv: run Tools\trailer\shoot.ps1 first' }
	$Take = $latest.FullName
}
$Take = (Resolve-Path $Take).Path
"take: $Take"

# --- where the music starts, and where the video frames are
$onsetOut = & python (Join-Path $PSScriptRoot 'measure.py') onset $Take 2>&1
$onsetOut | ForEach-Object { "  $_" }
$hit = $onsetOut | Select-String -Pattern '^ONSET=(-?[0-9.]+)' | Select-Object -Last 1
if (-not $hit) { throw 'no music found in the recording' }
$onset = [double]::Parse($hit.Matches[0].Groups[1].Value, $inv)
$v0 = [double]::Parse((& ffprobe -v error -select_streams v:0 -show_entries stream=start_time -of 'default=nw=1:nk=1' $Take).Trim(), $inv)
# the start: the video frame at or before the first beat, for both streams
$start = $v0 + [Math]::Floor(($onset - $v0) * 60.0 + 1e-6) / 60.0 - 0.001
$len = $Bars * $bar + $Tail
"music starts at $(F $onset) s; the cut starts at $(F $start) s and lasts $(F $len) s ($Bars bars + $Tail s)"

# --- loudness of the cut sound (first pass), then the gain
$sel = "atrim=start=$(F $start):duration=$(F $len),asetpts=PTS-STARTPTS"
function Summary([string]$text, [string]$pattern) {
	# ebur128 logs a line every 100 ms, then a Summary block: read the numbers of the block
	$i = $text.LastIndexOf('Summary:')
	if ($i -lt 0) { return $null }
	$m = [regex]::Match($text.Substring($i), $pattern)
	if ($m.Success) { return [double]::Parse($m.Groups[1].Value, $inv) }
	return $null
}
$meas = & ffmpeg -hide_banner -nostats -copyts -i $Take -vn -af "$sel,ebur128=peak=true" -f null - 2>&1 | Out-String
$lufs = Summary $meas 'I:\s+(-?[0-9.]+) LUFS'
if ($null -eq $lufs) { throw "could not read the loudness:`n$meas" }
$gain = $TargetLufs - $lufs
$limit = [Math]::Pow(10.0, $LimitDb / 20.0)
"loudness before: $(F $lufs '0.0') LUFS -> gain $(F $gain '0.0') dB, limiter at $(F $LimitDb '0.0') dB"

# --- the encode
$fade = $len - 0.6
$fc = "[0:v]trim=start=$(F $start):duration=$(F $len),setpts=PTS-STARTPTS,fps=60,format=yuv420p[v];" +
	"[0:a]$sel,volume=$(F $gain '0.00')dB,alimiter=limit=$(F $limit '0.000'):level=0:attack=5:release=60,afade=t=out:st=$(F $fade):d=0.6[a]"
$mp4 = Join-Path $Dist "$Name.mp4"
& ffmpeg -hide_banner -nostats -loglevel warning -y -copyts -i $Take -filter_complex $fc -map '[v]' -map '[a]' `
	-c:v libx264 -preset slow -crf $Crf -profile:v high -pix_fmt yuv420p -g 120 -colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range tv `
	-c:a aac -b:a 256k -ar 48000 -movflags +faststart $mp4
if ($LASTEXITCODE -ne 0) { throw 'the encode failed' }

# --- the poster frame (a picture of the settled end card), and the GIF of the old footage if it is still in the old place
$poster = Join-Path $Dist "${Name}_poster.png"
& ffmpeg -hide_banner -nostats -loglevel warning -y -ss (F ($PosterBar * $bar)) -i $mp4 -frames:v 1 -update 1 $poster
$oldGif = Join-Path $Root 'Dist\Trailer\shift_flip.gif'
$gif = Join-Path $Dist 'shift_flip.gif'
if ((Test-Path $oldGif) -and -not (Test-Path $gif)) { Move-Item $oldGif $gif; "moved the GIF to $gif" }

# --- checks on the final file
"--- final file"
& ffprobe -v error -show_entries stream=codec_name,width,height,r_frame_rate,sample_rate,channels,duration -of compact $mp4
$check = & ffmpeg -hide_banner -nostats -i $mp4 -vn -af 'ebur128=peak=true' -f null - 2>&1 | Out-String
$finalLufs = Summary $check 'I:\s+(-?[0-9.]+) LUFS'
$finalPeak = Summary $check 'Peak:\s+(-?[0-9.]+) dBFS'
"integrated loudness: $finalLufs LUFS (target $TargetLufs +-1)"
"true peak: $finalPeak dBFS (must be at most -1)"
if ($null -eq $finalLufs -or [Math]::Abs($finalLufs - $TargetLufs) -gt 1.0) { Write-Warning 'the loudness is not within 1 LU of the target' }
if ($null -eq $finalPeak -or $finalPeak -gt -1.0) { Write-Warning 'the true peak is above -1 dBFS' }
$sheet = Join-Path $Review "${Name}_sheet.png"
& ffmpeg -hide_banner -nostats -y -i $mp4 -vf 'fps=1/2,scale=320:-1,tile=6x4' -frames:v 1 -update 1 $sheet 2>$null
"files:"
Get-ChildItem $Dist | ForEach-Object { '  {0}  {1:N1} MB' -f $_.FullName, ($_.Length / 1MB) }
"contact sheet: $sheet"
