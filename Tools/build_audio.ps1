# Rebuilds the game's audio: every sound is a MetaSound Source or Patch (Harmonix for the music clock), built by Python in the headless editor.
#   .\Tools\build_audio.ps1            clear Content\Shift\Audio\MS*.uasset, then run Tools\py\build_audio.py
#   .\Tools\build_audio.ps1 -Spike     the throw-away spike assets (*Spike*) with Tools\py\spike_audio.py
# An existing serialised MetaSound cannot be overwritten inside one editor session, so the old assets are removed first.
param(
	[switch]$Spike,
	[string]$Script = ''
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$AudioDir = Join-Path $Root 'Content\Shift\Audio'
$pattern = if ($Spike) { '*Spike*.uasset' } else { 'MS*.uasset' }
if (-not $Script) { $Script = if ($Spike) { 'Tools\py\spike_audio.py' } else { 'Tools\py\build_audio.py' } }

if (Test-Path $AudioDir) {
	$old = Get-ChildItem $AudioDir -Filter $pattern -File
	if ($old) { Write-Host "removing $($old.Count) old MetaSound asset(s)"; $old | Remove-Item -Force }
}
& (Join-Path $PSScriptRoot 'run_py.ps1') -Script $Script
exit $LASTEXITCODE
