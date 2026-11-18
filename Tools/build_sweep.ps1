# Build-lever sweep: how much does the "Build your entry" split (Quality/Thumbnail/Network hours) move the bots? One balance run per build, jam and seed set, one after
# the other (parallel editor starts hang), averaged over the seed sets, one line per build with win % (share %) per jam and bot.
#   .\Tools\build_sweep.ps1                                          the default grid for greedy, 100 runs, seed set 1000
#   .\Tools\build_sweep.ps1 -Builds 4/4/4,0/0/0,8/4/0 -Bot greedy,reactor -Runs 200 -Seeds 1000,500000
#   .\Tools\build_sweep.ps1 -Set RallyCooldown=6                     the same grid under a tuning change
# A build is Q/T/N hours (the build screen allows 0..8 each and at most 12 in total; the commandlet does not clamp). 4/4/4 is the neutral split.
param(
	[string[]]$Builds = @('4/4/4', '0/0/0', '0/4/4', '4/0/4', '4/4/0', '8/4/0', '4/8/0', '4/0/8'),
	[string[]]$Bot = @('greedy'),
	[int]$Runs = 100,
	[string]$Jam = 'all',
	[int[]]$Seeds = @(1000),
	[string[]]$Set = @()
)
$ErrorActionPreference = 'Stop'
$inv = [cultureinfo]::InvariantCulture
$jams = if ($Jam -eq 'all') { 0, 1, 2 } else { @([int]$Jam) }
$botArg = ($Bot | ForEach-Object { $_ -split ',' } | Where-Object { $_ }) -join ','
$Builds = @($Builds | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
foreach ($b in $Builds) {
	$q, $t, $n = $b -split '/'
	$line = '{0,-8}' -f $b
	foreach ($j in $jams) {
		$sum = @{}
		$cnt = @{}
		foreach ($seed in $Seeds) {
			$out = & (Join-Path $PSScriptRoot 'balance.ps1') -Runs $Runs -Jam $j -Bot $botArg -Build @($q, $t, $n) -Seed0 $seed -Set $Set -Tag bsweep
			foreach ($l in $out) {
				if ($l -match 'jam=\d bot=(\S+)\s+runs=\d+\s+win\s+([\d.]+)%\s+dq\s+([\d.]+)%\s+rank\s+([\d.]+)\s+share\s+([\d.]+)%') {
					$k = $Matches[1]
					if (-not $sum.ContainsKey($k)) { $sum[$k] = @(0.0, 0.0, 0.0); $cnt[$k] = 0 }
					$sum[$k][0] += [double]::Parse($Matches[2], $inv)
					$sum[$k][1] += [double]::Parse($Matches[3], $inv)
					$sum[$k][2] += [double]::Parse($Matches[5], $inv)
					$cnt[$k]++
				}
			}
		}
		$cells = foreach ($k in $sum.Keys | Sort-Object) {
			$c = [double]$cnt[$k]
			'{0} {1,5} ({2,4}%)' -f $k, ($sum[$k][0] / $c).ToString('0.0', $inv), ($sum[$k][2] / $c).ToString('0', $inv)
		}
		$line += ' | J' + $j + ' ' + ($cells -join '  ')
	}
	Write-Host $line
}
