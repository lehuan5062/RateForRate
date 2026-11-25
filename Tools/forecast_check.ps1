# The forecast gate: builds the build screen's forecast model for a set of worlds (Tools\forecast.ps1 -Model) and fails with a readable diff when a number leaves the band in
# Data\ForecastTargets.csv. The things the playtest found wrong, as numbers: the chart must not wander with the sliders (monotone, no jumps), it must agree with what the levers
# say (more Quality or Network is worth points), and it must be the same every time.
#   .\Tools\forecast_check.ps1                    20 worlds per jam (about 1 minute)
#   .\Tools\forecast_check.ps1 -Worlds 40 -Seed0 500000
#   .\Tools\forecast_check.ps1 -Csv Saved\Forecast\model_m.csv   check a table that was already produced, run nothing
# Data\ForecastTargets.csv columns: Id,Jam,Metric,Min,Max,Why (Jam 0..2 or all).
param(
	[int]$Worlds = 20,
	[int]$Seed0 = 1000,
	[string]$Csv = '',
	[string]$Tag = 'check'
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$inv = [cultureinfo]::InvariantCulture
function Num([string]$s) { return [double]::Parse($s, $inv) }
$targets = Import-Csv (Join-Path $Root 'Data\ForecastTargets.csv')

$ms = @()
$deterministic = $null
if (-not $Csv) {
	$out = & (Join-Path $PSScriptRoot 'forecast.ps1') -Model -Worlds $Worlds -Seed0 $Seed0 -Tag $Tag
	foreach ($l in $out) { if ($l -match 'plays in (\d+) ms') { $ms += [double]$Matches[1] } }
	$Csv = "Saved\Forecast\model_$Tag.csv"
	# the same worlds again: the table must be identical to the last digit, whatever the scheduling of the plays
	$first = (Get-FileHash (Join-Path $Root $Csv) -Algorithm SHA256).Hash
	& (Join-Path $PSScriptRoot 'forecast.ps1') -Model -Worlds $Worlds -Seed0 $Seed0 -Tag "$Tag-repeat" | Out-Null
	$second = (Get-FileHash (Join-Path $Root "Saved\Forecast\model_$Tag-repeat.csv") -Algorithm SHA256).Hash
	$deterministic = if ($first -eq $second) { 1 } else { 0 }
}
$rows = Import-Csv (Join-Path $Root $Csv)
if ($rows.Count -eq 0) { Write-Host 'no forecast rows found'; exit 2 }

# index the table: jam|world -> "q,t,n" -> share of every entry
$worldsIdx = @{}
foreach ($r in $rows) {
	$k = "$($r.jam)|$($r.world)"
	if (-not $worldsIdx.ContainsKey($k)) { $worldsIdx[$k] = @{} }
	$worldsIdx[$k]["$($r.q),$($r.t),$($r.n)"] = @([double]$r.s0, [double]$r.s1, [double]$r.s2, [double]$r.s3, [double]$r.s4, [double]$r.s5, [double]$r.s6, [double]$r.s7, [double]$r.s8)
}
function RankOf($v) { $n = 1; for ($i = 1; $i -lt 9; $i++) { if ($v[$i] -gt $v[0]) { $n++ } }; return $n }

$metrics = @{}   # jam -> metric -> value
foreach ($jam in 0, 1, 2, 3) {
	$drops = 0; $steps = 0; $worse = 0; $abs = New-Object System.Collections.Generic.List[double]
	$qOk = 0; $nOk = 0; $tSum = 0.0; $count = 0; $neutral = 0.0
	foreach ($k in $worldsIdx.Keys) {
		if (-not $k.StartsWith("$jam|")) { continue }
		$t = $worldsIdx[$k]
		$count++
		foreach ($key in $t.Keys) {
			$q, $tt, $n = $key -split ','
			$a = $t[$key]
			foreach ($lever in 0, 1, 2) {
				$nb = @([int]$q, [int]$tt, [int]$n); $nb[$lever]++
				$nk = $nb -join ','
				if (-not $t.ContainsKey($nk)) { continue }
				$b = $t[$nk]
				$steps++
				if ($b[0] - $a[0] -lt -0.0005) { $drops++ }
				if ((RankOf $b) -gt (RankOf $a)) { $worse++ }
				$abs.Add([math]::Abs($b[0] - $a[0]) * 100.0)
			}
		}
		$qOk += $(if (($t['8,2,2'][0] - $t['0,2,2'][0]) -ge 0.05) { 1 } else { 0 })
		$nOk += $(if (($t['2,2,8'][0] - $t['2,2,0'][0]) -ge 0.05) { 1 } else { 0 })
		$tSum += ($t['2,8,2'][0] - $t['2,0,2'][0]) * 100.0
		$neutral += $t['4,4,4'][0] * 100.0
	}
	$sorted = $abs | Sort-Object
	$metrics["$jam"] = @{
		monotone_drops        = $drops
		rank_worse_pct        = 100.0 * $worse / [math]::Max(1, $steps)
		quality_effect_pct    = 100.0 * $qOk / [math]::Max(1, $count)
		network_effect_pct    = 100.0 * $nOk / [math]::Max(1, $count)
		thumbnail_effect_mean = $tSum / [math]::Max(1, $count)
		step_p99              = $(if ($sorted.Count -gt 0) { $sorted[[int][math]::Floor(0.99 * ($sorted.Count - 1))] } else { 0 })
		neutral_share_mean    = $neutral / [math]::Max(1, $count)
	}
}
$metrics['all'] = @{
	ms_per_world_max = $(if ($ms.Count -gt 0) { ($ms | Measure-Object -Maximum).Maximum } else { $null })
	deterministic    = $deterministic
}

$fails = 0
$report = foreach ($t in $targets) {
	$value = $null
	if ($metrics.ContainsKey($t.Jam) -and $metrics[$t.Jam].ContainsKey($t.Metric)) { $value = $metrics[$t.Jam][$t.Metric] }
	$min = if ($t.Min -ne '') { Num $t.Min } else { [double]::NegativeInfinity }
	$max = if ($t.Max -ne '') { Num $t.Max } else { [double]::PositiveInfinity }
	if ($null -eq $value) { if ($Csv -and $t.Jam -eq 'all') { continue } }   # a table checked without running has no timing and no repeat
	$ok = ($null -ne $value) -and $value -ge $min -and $value -le $max
	if (-not $ok) { $fails++ }
	[pscustomobject]@{
		Result = $(if ($ok) { 'PASS' } else { 'FAIL' })
		Id     = $t.Id
		Jam    = $t.Jam
		Value  = $(if ($null -ne $value) { '{0} {1}' -f ([double]$value).ToString('0.0', $inv), $t.Metric } else { "missing $($t.Metric)" })
		Band   = '[{0}, {1}]' -f $(if ($t.Min -ne '') { $t.Min } else { '-inf' }), $(if ($t.Max -ne '') { $t.Max } else { 'inf' })
		Why    = $t.Why
	}
}
$report | Format-Table -AutoSize -Wrap | Out-String | Write-Host
if ($fails -gt 0) { Write-Host "forecast gate: $fails target(s) outside the band"; exit 1 }
Write-Host 'forecast gate: all targets met'
exit 0
