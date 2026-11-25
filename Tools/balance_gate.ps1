# The balance gate: runs the balance table and fails with a readable diff when a number leaves the acceptance band in Data\BalanceTargets.csv.
#   .\Tools\balance_gate.ps1                    100 runs per cell, only the bots and builds the targets need (about 8 minutes)
#   .\Tools\balance_gate.ps1 -Runs 200 -Seeds 1000,500000   tighter: pooled over two seed sets (win rates carry about +-5 points of noise at 100 runs)
#   .\Tools\balance_gate.ps1 -Log Saved\x.txt   check a table that was already produced (the output of balance.ps1, default build) without running anything
#   .\Tools\balance_gate.ps1 -Explore           also run the strategy search (Tools\explore.ps1, about 20 minutes) and gate its best response
#   .\Tools\balance_gate.ps1 -Set RallyCooldown=6   gate a tuning change before it is baked into the defaults
# Data\BalanceTargets.csv columns: Id,Jam,Bot,Build,Metric,RelativeTo,RelativeBuild,Min,Max,Why.
#   Build / RelativeBuild: Q/T/N hours (empty = the neutral 4/4/4). With RelativeTo set the checked value is the bot's metric minus RelativeTo's (in RelativeBuild, default Build).
#   Metric: win_pct, dq_pct, share_pct, sock_pct (the jam= row), shield_blocked, parried, posts_rally, posts_parry (per run, the "Balance: flips" line), mega_swing
#   (the player's share at the end minus at the start of the rival's final push, in points) and lead_kept (runs that led at the push's start and still led, percent of those).
#   From the "Balance: focus" line (where the rival's attacks went, per run): focus_attacks, focus_voters (different voters attacked), focus_max (the most attacks on one voter),
#   focus_top3_pct (the three most attacked voters' share of the attacks) and focus_heavy_pct (runs that put 10 or more on one voter, percent). From "Balance: field" (the board at
#   the end): rival_end_pct, best_other_pct (the best other entry's share), others_top3 (other entries holding a top-3 place) and other_leads_pct (runs an other entry won, percent).
#   From "Balance: flow": others_from_rival (voters per run other entries' plugs took from the rival) and others_from_rival_pts (their weight, points of the vote).
#   The Explorer row (Bot explorer, Metric best_response) compares the validated best response with greedy and is only checked with -Explore.
param(
	[int]$Runs = 100,
	[int[]]$Seeds = @(1000),
	[string]$Log = '',
	[string[]]$Set = @(),
	[switch]$Explore,
	[int]$ExploreSamples = 30,
	[int]$ExploreRuns = 40,
	[string]$Jam = ''           # only this jam's targets (and only this jam is played); empty = every jam
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$inv = [cultureinfo]::InvariantCulture
$targets = Import-Csv (Join-Path $Root 'Data\BalanceTargets.csv')
if ($Jam -ne '') { $targets = @($targets | Where-Object { $_.Jam -eq $Jam }) }
function NormBuild([string]$b) { if ([string]::IsNullOrWhiteSpace($b)) { return '4/4/4' } return $b.Trim() }
function Num([string]$s) { return [double]::Parse($s, $inv) }

# Which bots each build needs (the explorer row has no balance cell).
$need = @{}
foreach ($t in $targets) {
	if ($t.Bot -eq 'explorer') { continue }
	$b = NormBuild $t.Build
	$rb = if ($t.RelativeBuild) { NormBuild $t.RelativeBuild } else { $b }
	if (-not $need.ContainsKey($b)) { $need[$b] = New-Object System.Collections.Generic.HashSet[string] }
	[void]$need[$b].Add($t.Bot)
	if ($t.RelativeTo) {
		if (-not $need.ContainsKey($rb)) { $need[$rb] = New-Object System.Collections.Generic.HashSet[string] }
		[void]$need[$rb].Add($t.RelativeTo)
	}
}

# cells[build|jam|bot] = metrics averaged over the seed sets
$cells = @{}
function Parse-Lines($lines, [string]$build, [hashtable]$acc) {
	foreach ($l in $lines) {
		if ($l -match 'jam=(\d+) bot=(\S+)\s+runs=(\d+)\s+win\s+([\d.]+)%\s+dq\s+([\d.]+)%\s+rank\s+([\d.]+)\s+share\s+([\d.]+)%\s+sock\s+([\d.]+)%') {
			$k = "$build|$($Matches[1])|$($Matches[2])"
			if (-not $acc.ContainsKey($k)) { $acc[$k] = @{} }
			foreach ($m in @(@('win_pct', 4), @('dq_pct', 5), @('share_pct', 7), @('sock_pct', 8))) {
				$acc[$k][$m[0]] = @($acc[$k][$m[0]]) + (Num $Matches[$m[1]])
			}
		}
		elseif ($l -match 'Balance: flips jam=(\d+) bot=(\S+)\s.*shield-blocked ([\d.]+) parried ([\d.]+)\)\s+posts: rally ([\d.]+) parry ([\d.]+)') {
			$k = "$build|$($Matches[1])|$($Matches[2])"
			if (-not $acc.ContainsKey($k)) { $acc[$k] = @{} }
			$acc[$k]['shield_blocked'] = @($acc[$k]['shield_blocked']) + (Num $Matches[3])
			$acc[$k]['parried'] = @($acc[$k]['parried']) + (Num $Matches[4])
			$acc[$k]['posts_rally'] = @($acc[$k]['posts_rally']) + (Num $Matches[5])
			$acc[$k]['posts_parry'] = @($acc[$k]['posts_parry']) + (Num $Matches[6])
		}
		elseif ($l -match 'Balance: focus jam=(\d+) bot=(\S+)\s+per run: rival attacks ([\d.]+) on ([\d.]+) different voters, the most on one voter ([\d.]+), the three most ([\d.]+) \((\d+)% of its attacks\)\s+runs with 10 or more on one voter (\d+)') {
			$k = "$build|$($Matches[1])|$($Matches[2])"
			if (-not $acc.ContainsKey($k)) { $acc[$k] = @{} }
			$acc[$k]['focus_attacks'] = @($acc[$k]['focus_attacks']) + (Num $Matches[3])
			$acc[$k]['focus_voters'] = @($acc[$k]['focus_voters']) + (Num $Matches[4])
			$acc[$k]['focus_max'] = @($acc[$k]['focus_max']) + (Num $Matches[5])
			$acc[$k]['focus_top3_pct'] = @($acc[$k]['focus_top3_pct']) + (Num $Matches[7])
			$acc[$k]['focus_heavy_runs'] = @($acc[$k]['focus_heavy_runs']) + (Num $Matches[8])
		}
		elseif ($l -match 'Balance: field jam=(\d+) bot=(\S+)\s+final share: you ([\d.]+)%\s+rival ([\d.]+)%\s+best other ([\d.]+)%\s+other entries in the top 3: ([\d.]+)\s+an other entry leads: (\d+) of (\d+) runs') {
			$k = "$build|$($Matches[1])|$($Matches[2])"
			if (-not $acc.ContainsKey($k)) { $acc[$k] = @{} }
			$acc[$k]['rival_end_pct'] = @($acc[$k]['rival_end_pct']) + (Num $Matches[4])
			$acc[$k]['best_other_pct'] = @($acc[$k]['best_other_pct']) + (Num $Matches[5])
			$acc[$k]['others_top3'] = @($acc[$k]['others_top3']) + (Num $Matches[6])
			$acc[$k]['other_leads_pct'] = @($acc[$k]['other_leads_pct']) + (100.0 * (Num $Matches[7]) / [math]::Max(1.0, (Num $Matches[8])))
			$acc[$k]['focus_heavy_pct'] = @($acc[$k]['focus_heavy_pct']) + $(if ($acc[$k].ContainsKey('focus_heavy_runs')) { 100.0 * ($acc[$k]['focus_heavy_runs'] | Select-Object -Last 1) / [math]::Max(1.0, (Num $Matches[8])) } else { 0.0 })
		}
		elseif ($l -match "Balance: flow jam=(\d+) bot=(\S+)\s+per run, voters \(points of the vote\): other entries' plugs took from you ([\d.]+) \(([\d.]+)\), from the rival ([\d.]+) \(([\d.]+)\)") {
			$k = "$build|$($Matches[1])|$($Matches[2])"
			if (-not $acc.ContainsKey($k)) { $acc[$k] = @{} }
			$acc[$k]['others_from_rival'] = @($acc[$k]['others_from_rival']) + (Num $Matches[5])
			$acc[$k]['others_from_rival_pts'] = @($acc[$k]['others_from_rival_pts']) + (Num $Matches[6])
		}
		elseif ($l -match 'Balance: mega jam=(\d+) bot=(\S+)\s+share at start ([\d.]+)% at end ([\d.]+)% swing ([+-]?[\d.]+)\s+led at start (\d+), kept (\d+)') {
			$k = "$build|$($Matches[1])|$($Matches[2])"
			if (-not $acc.ContainsKey($k)) { $acc[$k] = @{} }
			$acc[$k]['mega_swing'] = @($acc[$k]['mega_swing']) + (Num $Matches[5])
			$led = [int]$Matches[6]
			if ($led -gt 0) { $acc[$k]['lead_kept'] = @($acc[$k]['lead_kept']) + (100.0 * [int]$Matches[7] / $led) }
		}
	}
}
$acc = @{}
if ($Log) {
	Parse-Lines (Get-Content (Join-Path $Root $Log)) '4/4/4' $acc
}
else {
	foreach ($b in $need.Keys) {
		$q, $t, $n = $b -split '/'
		$botArg = ($need[$b] | Sort-Object) -join ','
		foreach ($seed in $Seeds) {
			$lines = & (Join-Path $PSScriptRoot 'balance.ps1') -Runs $Runs -Bot $botArg -Build @($q, $t, $n) -Seed0 $seed -Tag gate -Set $Set -Jam $(if ($Jam -ne '') { $Jam } else { 'all' })
			Parse-Lines $lines $b $acc
		}
	}
}
foreach ($k in $acc.Keys) {
	$cells[$k] = @{}
	foreach ($m in $acc[$k].Keys) { $cells[$k][$m] = (($acc[$k][$m] | Measure-Object -Average).Average) }
}
if ($cells.Count -eq 0) { Write-Host 'no balance rows found'; exit 2 }

$explored = @{}
if ($Explore) {
	$exploreLines = & (Join-Path $PSScriptRoot 'explore.ps1') -Samples $ExploreSamples -Runs $ExploreRuns -Tag gate -Set $Set
	foreach ($l in $exploreLines) {
		# Explore: jam=2 best-response (preset:reactor, 48 configurations)  search win  50.0%  validated win  41.2% vs greedy  30.0% = +11.2 points  dq ...
		if ($l -match 'Explore: jam=(\d+) best-response .*= ([+-]?[\d.]+) points') { $explored[$Matches[1]] = Num $Matches[2] }
	}
}

$fails = 0
$rows = foreach ($t in $targets) {
	$value = $null
	$what = ''
	$b = NormBuild $t.Build
	if ($t.Bot -eq 'explorer') {
		if (-not $Explore) { continue }
		if ($explored.ContainsKey($t.Jam)) { $value = $explored[$t.Jam] }
		$what = 'best response - greedy (points)'
	}
	else {
		$cell = $cells["$b|$($t.Jam)|$($t.Bot)"]
		if ($cell -and $cell.ContainsKey($t.Metric)) {
			$value = $cell[$t.Metric]
			if ($t.RelativeTo) {
				$rb = if ($t.RelativeBuild) { NormBuild $t.RelativeBuild } else { $b }
				$ref = $cells["$rb|$($t.Jam)|$($t.RelativeTo)"]
				$value = if ($ref -and $ref.ContainsKey($t.Metric)) { $value - $ref[$t.Metric] } else { $null }
				$what = "$($t.Metric) - $($t.RelativeTo)" + $(if ($rb -ne $b) { "@$rb" } else { '' })
			}
			else { $what = $t.Metric }
		}
		elseif ($Log -and $b -ne '4/4/4') { continue }   # a -Log table only holds the neutral build
	}
	$min = if ($t.Min -ne '') { Num $t.Min } else { [double]::NegativeInfinity }
	$max = if ($t.Max -ne '') { Num $t.Max } else { [double]::PositiveInfinity }
	$ok = ($null -ne $value) -and $value -ge $min -and $value -le $max
	if (-not $ok) { $fails++ }
	[pscustomobject]@{
		Result = $(if ($ok) { 'PASS' } else { 'FAIL' })
		Id     = $t.Id
		Jam    = $t.Jam
		Bot    = $t.Bot
		Build  = $b
		Value  = $(if ($null -ne $value) { '{0} {1}' -f ([double]$value).ToString('0.0', $inv), $what } else { "missing $what" })
		Band   = '[{0}, {1}]' -f $(if ($t.Min -ne '') { $t.Min } else { '-inf' }), $(if ($t.Max -ne '') { $t.Max } else { 'inf' })
		Why    = $t.Why
	}
}
$rows | Format-Table -AutoSize -Wrap | Out-String | Write-Host
if ($fails -gt 0) { Write-Host "balance gate: $fails target(s) outside the band"; exit 1 }
Write-Host 'balance gate: all targets met'
exit 0
