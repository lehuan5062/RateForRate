# Tuning sweep helper: runs the balance table for a few bots under several "-Set" variants, one after the other (parallel editor starts hang), and prints one
# line per variant and jam with the win % of each bot, so a rule change can be read at a glance.
#   .\Tools\sweep.ps1 -Variants 'base=','regen=Regen=0.45','rally=RallyCooldown=5,RallyLock=4' -Bot greedy,reactor -Runs 100
# A variant is Label=Name=Value,Name=Value (the label is only printed; an empty rest means no override). Per-jam variants: put "@0:" / "@1:" / "@2:" in front of the Sets
# of a jam, e.g. 'x=@0:Regen=0.45 @1:Regen=0.47 @2:Regen=0.5' (the jam's Posts per second is spelled Regen).
param(
	[string[]]$Variants,
	[string[]]$Bot = @('greedy', 'reactor'),
	[int]$Runs = 100,
	[string]$Jam = 'all',
	[int]$Seed0 = 1000            # first seed of the runs (a second, disjoint set such as 500000 checks a setting is not fitted to one)
)
$ErrorActionPreference = 'Stop'
$jams = if ($Jam -eq 'all') { 0, 1, 2 } else { @([int]$Jam) }
$botArg = ($Bot | ForEach-Object { $_ -split ',' } | Where-Object { $_ }) -join ','
foreach ($v in $Variants) {
	$label, $rest = $v -split '=', 2
	$line = "{0,-34}" -f $label
	foreach ($j in $jams) {
		$sets = @()
		foreach ($tok in ($rest -split '\s+' | Where-Object { $_ })) {
			if ($tok -match '^@(\d):(.*)$') { if ([int]$Matches[1] -eq $j) { $sets += $Matches[2] } }
			else { $sets += $tok }
		}
		$out = & (Join-Path $PSScriptRoot 'balance.ps1') -Runs $Runs -Jam $j -Bot $botArg -Set $sets -Seed0 $Seed0 -Tag sweep
		$cells = @()
		$swing = @{}
		foreach ($l in $out) {
			if ($l -match 'Balance: mega jam=\d bot=(\S+)\s+share at start [\d.]+% at end [\d.]+% swing ([+-]?[\d.]+)') {
				$swing[$Matches[1]] = [double]::Parse($Matches[2], [cultureinfo]::InvariantCulture)
			}
		}
		foreach ($l in $out) {
			if ($l -match 'jam=\d bot=(\S+)\s+runs=\d+\s+win\s+([\d.]+)%\s+dq\s+([\d.]+)%') {
				$win = ([double]::Parse($Matches[2], [cultureinfo]::InvariantCulture)).ToString('0.0', [cultureinfo]::InvariantCulture)
				$dq = [double]::Parse($Matches[3], [cultureinfo]::InvariantCulture)
				$sw = if ($swing.ContainsKey($Matches[1])) { ' sw' + $swing[$Matches[1]].ToString('+0.0;-0.0', [cultureinfo]::InvariantCulture) } else { '' }
				$cells += ('{0} {1,5}{2}{3}' -f $Matches[1], $win, $(if ($dq -gt 0) { "(dq$($dq.ToString('0', [cultureinfo]::InvariantCulture)))" } else { '' }), $sw)
			}
		}
		$line += ' | J' + $j + ' ' + ($cells -join ' ')
	}
	Write-Host $line
}
