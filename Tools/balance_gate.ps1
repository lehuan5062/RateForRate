# The balance gate: runs the balance table and fails with a readable diff when a number leaves the acceptance band in Data\BalanceTargets.csv.
#   .\Tools\balance_gate.ps1                    100 runs per cell, all jams and bots (about 8 minutes)
#   .\Tools\balance_gate.ps1 -Runs 200          tighter (win rates carry about +-5 points of noise at 100 runs)
#   .\Tools\balance_gate.ps1 -Log Saved\x.txt   check a table that was already produced (the output of balance.ps1) without running anything
#   .\Tools\balance_gate.ps1 -Explore           also run the strategy search (Tools\explore.ps1, about 20 minutes) and gate its best response
#   .\Tools\balance_gate.ps1 -Set RallyCooldown=6   gate a tuning change before it is baked into the defaults
# Data\BalanceTargets.csv columns: Id,Jam,Bot,Metric,RelativeTo,Min,Max,Why. Metric is win_pct, dq_pct, share_pct or sock_pct. With RelativeTo set the checked value is the
# bot's metric minus that bot's (so "finisher - greedy <= 5"); the Explorer row compares the validated best response with greedy and is only checked with -Explore.
param(
	[int]$Runs = 100,
	[string]$Log = '',
	[string[]]$Set = @(),
	[switch]$Explore,
	[int]$ExploreSamples = 30,
	[int]$ExploreRuns = 40
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$targets = Import-Csv (Join-Path $Root 'Data\BalanceTargets.csv')

if ($Log) {
	$lines = Get-Content (Join-Path $Root $Log)
}
else {
	$lines = & (Join-Path $PSScriptRoot 'balance.ps1') -Runs $Runs -Tag gate -Set $Set
}

# jam=0 bot=greedy runs=100  win  88.0%  dq   0.0%  rank 1.12  share  41.0%  sock   0.0%  strikes ...
$cells = @{}
foreach ($l in $lines) {
	if ($l -match 'jam=(\d+) bot=(\S+)\s+runs=(\d+)\s+win\s+([\d.]+)%\s+dq\s+([\d.]+)%\s+rank\s+([\d.]+)\s+share\s+([\d.]+)%\s+sock\s+([\d.]+)%') {
		$cells["$($Matches[1])|$($Matches[2])"] = @{ win_pct = [double]$Matches[4]; dq_pct = [double]$Matches[5]; share_pct = [double]$Matches[7]; sock_pct = [double]$Matches[8]; runs = [int]$Matches[3] }
	}
}
if ($cells.Count -eq 0) { Write-Host 'no balance rows found'; exit 2 }

$explored = @{}
if ($Explore) {
	$exploreLines = & (Join-Path $PSScriptRoot 'explore.ps1') -Samples $ExploreSamples -Runs $ExploreRuns -Tag gate -Set $Set
	foreach ($l in $exploreLines) {
		# Explore: jam=2 best-response (preset:reactor, 48 configurations)  search win  50.0%  validated win  41.2% vs greedy  30.0% = +11.2 points  dq ...
		if ($l -match 'Explore: jam=(\d+) best-response .*= ([+-]?[\d.]+) points') { $explored[$Matches[1]] = [double]$Matches[2] }
	}
}

$fails = 0
$rows = foreach ($t in $targets) {
	$value = $null
	$what = ''
	if ($t.Bot -eq 'explorer') {
		if (-not $Explore) { continue }
		if (-not $explored.ContainsKey($t.Jam)) { $value = $null }
		else { $value = $explored[$t.Jam] }
		$what = 'best response - greedy (points)'
	}
	else {
		$cell = $cells["$($t.Jam)|$($t.Bot)"]
		if ($cell) {
			$value = $cell[$t.Metric]
			if ($t.RelativeTo) {
				$ref = $cells["$($t.Jam)|$($t.RelativeTo)"]
				$value = if ($ref) { $value - $ref[$t.Metric] } else { $null }
				$what = "$($t.Metric) - $($t.RelativeTo)"
			}
			else { $what = $t.Metric }
		}
	}
	$min = if ($t.Min -ne '') { [double]$t.Min } else { [double]::NegativeInfinity }
	$max = if ($t.Max -ne '') { [double]$t.Max } else { [double]::PositiveInfinity }
	$ok = ($null -ne $value) -and $value -ge $min -and $value -le $max
	if (-not $ok) { $fails++ }
	[pscustomobject]@{
		Result = $(if ($ok) { 'PASS' } else { 'FAIL' })
		Id     = $t.Id
		Jam    = $t.Jam
		Bot    = $t.Bot
		Value  = $(if ($null -ne $value) { '{0} {1}' -f ([double]$value).ToString('0.0', [cultureinfo]::InvariantCulture), $what } else { "missing $what" })
		Band   = '[{0}, {1}]' -f $(if ($t.Min -ne '') { $t.Min } else { '-inf' }), $(if ($t.Max -ne '') { $t.Max } else { 'inf' })
		Why    = $t.Why
	}
}
$rows | Format-Table -AutoSize -Wrap | Out-String | Write-Host
if ($fails -gt 0) { Write-Host "balance gate: $fails target(s) outside the band"; exit 1 }
Write-Host 'balance gate: all targets met'
exit 0
