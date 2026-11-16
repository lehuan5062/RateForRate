# Level calibration loop for the MetaSound audio (no recording, no .wav): rebuild -> measure every sound with the in-graph meters -> adjust trims.
#   .\Tools\calibrate_audio.ps1                 up to 4 passes, stops when every sound is within 0.5 dB of its target (Tools\py\ms_targets.py)
#   .\Tools\calibrate_audio.ps1 -Reset          start from zero trims (deletes Tools\py\ms_trims.json first)
# The trims land in Tools\py\ms_trims.json and are applied by Tools\py\ms_sfx.py / ms_music.py on the next build.
param(
	[int]$MaxPasses = 4,
	[switch]$Reset
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Trims = Join-Path $Root 'Tools\py\ms_trims.json'
if ($Reset -and (Test-Path $Trims)) { Remove-Item $Trims -Force; Write-Host 'trims reset' }
$python = (Get-Command python -ErrorAction Stop).Source

for ($pass = 1; $pass -le $MaxPasses; $pass++) {
	Write-Host "=== calibration pass $pass of $MaxPasses ==="
	& (Join-Path $PSScriptRoot 'build_audio.ps1') | Out-Null
	if ($LASTEXITCODE -ne 0) { throw 'audio build failed' }
	& (Join-Path $PSScriptRoot 'run.ps1') -Sound -TimeoutSec 900 -Extra @('-ShiftAudioMeter=all', '-ShiftNoUI', '-ShiftNoTutorial') | Out-Null
	& $python (Join-Path $Root 'Tools\py\ms_calibrate.py') (Join-Path $Root 'Saved\run_last.log')
	if ($LASTEXITCODE -eq 0) { Write-Host "calibrated after $pass pass(es)"; exit 0 }
}
Write-Host 'not within 0.5 dB after the last pass; run again'
exit 1
