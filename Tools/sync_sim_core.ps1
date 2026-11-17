# Keeps the standalone harness core (Tools\SimHarness\core) a verbatim copy of the engine-independent numeric core the game compiles (Source\ShiftHappens\Sim).
# The core includes its siblings by bare name and nothing from Unreal, so the same files build in both places.
#   .\Tools\sync_sim_core.ps1            copy Source\ShiftHappens\Sim -> Tools\SimHarness\core
#   .\Tools\sync_sim_core.ps1 -Check     only compare; exit code 1 when they differ (Tools\verify.ps1 runs this)
param(
	[switch]$Check
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Src = Join-Path $Root 'Source\ShiftHappens\Sim'
$Dst = Join-Path $Root 'Tools\SimHarness\core'
New-Item -ItemType Directory -Force -Path $Dst | Out-Null

$differ = New-Object System.Collections.Generic.List[string]
foreach ($f in (Get-ChildItem $Src -File | Where-Object { $_.Extension -in '.h', '.cpp' })) {
	$target = Join-Path $Dst $f.Name
	$same = (Test-Path $target) -and ((Get-FileHash $f.FullName -Algorithm SHA256).Hash -eq (Get-FileHash $target -Algorithm SHA256).Hash)
	if (-not $same) {
		$differ.Add($f.Name)
		if (-not $Check) { Copy-Item $f.FullName $target -Force }
	}
}
# files that only exist in the harness core are stale
foreach ($f in (Get-ChildItem $Dst -File | Where-Object { $_.Extension -in '.h', '.cpp' })) {
	if (-not (Test-Path (Join-Path $Src $f.Name))) { $differ.Add("$($f.Name) (only in the harness core)") }
}

if ($Check) {
	if ($differ.Count -gt 0) { Write-Host "harness core differs from Source\ShiftHappens\Sim: $($differ -join ', ')  (run Tools\sync_sim_core.ps1)"; exit 1 }
	Write-Host 'harness core is identical to Source\ShiftHappens\Sim'
	exit 0
}
if ($differ.Count -gt 0) { Write-Host "synced: $($differ -join ', ')" } else { Write-Host 'already in sync' }
