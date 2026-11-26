# Fills the folder the trailer's Replays page lists (Saved\Trailer\recordings, passed to the game as -ShiftRecordingsDir) from Data\Trailer\replay_*.shiftrec.txt,
# with staggered file times so the page reads like a few days of play (newest first: the Level Playing Field run, then GAME OFF, then Tiny Jam).
# preview.ps1 and shoot.ps1 call it; your own recordings (Saved\Recordings) are never touched or shown.
#   Tools\trailer\stage_recordings.ps1            -> prints the folder
param([string]$Dir = '')
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $Dir) { $Dir = Join-Path $Root 'Saved\Trailer\recordings' }
New-Item -ItemType Directory -Force -Path $Dir | Out-Null
Get-ChildItem $Dir -Filter '*.shiftrec.txt' -ErrorAction SilentlyContinue | Remove-Item -Force
# file name -> when it was "played" (local time)
$when = [ordered]@{
	'replay_levelfield.shiftrec.txt' = [datetime]'2026-11-25 20:41:00'
	'replay_gameoff.shiftrec.txt'    = [datetime]'2026-11-24 22:14:00'
	'replay_tinyjam.shiftrec.txt'    = [datetime]'2026-11-24 18:02:00'
}
foreach ($name in $when.Keys) {
	$src = Join-Path $Root "Data\Trailer\$name"
	if (-not (Test-Path $src)) { Write-Warning "missing $src"; continue }
	$dst = Join-Path $Dir $name
	Copy-Item $src $dst -Force
	(Get-Item $dst).LastWriteTime = $when[$name]
}
$Dir
