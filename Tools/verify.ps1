# One-command check of what this project claims, all headless (no window, no console, nothing audible):
#   .\Tools\verify.ps1                 everything below
#   .\Tools\verify.ps1 -SkipMeters     skip the ~3 minute audio meter run
#
#   1. audio assets   no .wav anywhere, no SoundWave assets or dependencies, every MetaSound source and patch present (editor, Tools\py\verify_audio.py)
#   2. behaviour      the full run on the real StateTrees: 10 instances, every kind of action happens (rival, other entries, mods, ambient events),
#                     and two identical runs give an identical balance table (determinism)
#   3. game boot      the real game starts, loads 26 of 26 effects and the music, binds the Harmonix clock and receives beat events, activates the
#                     three Game Feature jam packs
#   4. audio meters   every sound audible and below -1 dBFS, music day / night / tension as designed, Harmonix clock steady, every pattern lane and
#                     voice firing (the game plays them inaudibly and reads the in-graph meters; see UShiftAudioMeter)
param(
	[switch]$SkipMeters
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$results = New-Object System.Collections.Generic.List[object]

function Check([string]$Name, [bool]$Ok, [string]$Detail = '') {
	$results.Add([pscustomobject]@{ Check = $Name; Result = $(if ($Ok) { 'PASS' } else { 'FAIL' }); Detail = $Detail })
	Write-Host ("[{0}] {1} {2}" -f $(if ($Ok) { 'PASS' } else { 'FAIL' }), $Name, $Detail)
}

function LogHas([string]$Path, [string]$Pattern) { return [bool](Select-String -Path $Path -Pattern $Pattern -Quiet) }

# ---- 1. audio assets -------------------------------------------------------------------------------------------------------------
& (Join-Path $PSScriptRoot 'run_py.ps1') -Script 'Tools\py\verify_audio.py' | Out-Null
$pyLog = Join-Path $Root 'Saved\py_verify_audio.log'
Check 'audio assets: MetaSound only, no SoundWave, no .wav' (LogHas $pyLog 'AUDIO ASSETS: PASS') ((Select-String -Path $pyLog -Pattern 'MetaSound assets: .*' | Select-Object -First 1).Matches.Value)

# ---- 2. behaviour on the StateTrees + determinism --------------------------------------------------------------------------------
$tables = @()
foreach ($pass in 1, 2) {
	& (Join-Path $PSScriptRoot 'balance.ps1') -Runs 12 -Jam 2 -Bot greedy -Build 4,4,4 | Out-Null
	$balLog = Join-Path $Root 'Saved\balance_last.log'
	$tables += , (@(Select-String -Path $balLog -Pattern '^.*LogShift: (Display: )?jam=' | ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '' }))
}
Check 'behaviour: no missing or uncompiled StateTree' (-not (LogHas $balLog 'StateTrees MISSING|not ready to run')) ''
$activity = Select-String -Path $balLog -Pattern 'Behaviour activity: (.*)' | Select-Object -First 1
$nums = if ($activity) { [regex]::Matches($activity.Matches[0].Groups[1].Value, '\d+') | ForEach-Object { [int]$_.Value } } else { @() }
# order: rival mega, defend, attack | others plugged | mods telegraphed, swept | ambient boosts, streamers, drama
$rivalActed = $nums.Count -ge 9 -and ($nums[1] + $nums[2]) -gt 0
$othersActed = $nums.Count -ge 9 -and $nums[3] -gt 0
$modsActed = $nums.Count -ge 9 -and $nums[4] -gt 0 -and $nums[5] -gt 0
$ambientActed = $nums.Count -ge 9 -and ($nums[6] + $nums[7] + $nums[8]) -gt 0
Check 'behaviour: the trees drive every kind of action' ($rivalActed -and $othersActed -and $modsActed -and $ambientActed) ($activity.Matches[0].Groups[1].Value)
Check 'behaviour: identical balance table on a repeat (deterministic)' (($tables[0] -join "`n") -eq ($tables[1] -join "`n") -and $tables[0].Count -gt 0) ($tables[0] -join ' ')

# ---- 3. game boot ----------------------------------------------------------------------------------------------------------------
$shot = Join-Path $Root 'Saved\Shots\verify.png'
New-Item -ItemType Directory -Force -Path (Split-Path $shot) | Out-Null
& (Join-Path $PSScriptRoot 'run.ps1') -Sound -Shot $shot -ShotDelay 6 -Quit -TimeoutSec 300 -Extra @('-ShiftMasterVolume=0', '-ShiftScreen=play', '-ShiftNoTutorial', '-ShiftNoAutoPause') | Out-Null
$runLog = Join-Path $Root 'Saved\run_last.log'
Check 'boot: 26 of 26 effects + music + Harmonix clock bound' (LogHas $runLog 'Audio: 26 of 26 sfx loaded, music playing, Harmonix clock bound') ''
$beats = Select-String -Path $runLog -Pattern 'Harmonix clock delivered (\d+) beat events' | Select-Object -First 1
Check 'boot: the Harmonix clock delivers beat events' ($beats -and [int]$beats.Matches[0].Groups[1].Value -ge 4) ($beats.Matches[0].Value)
Check 'boot: 10 StateTree instances run the behaviour' (LogHas $runLog 'Behaviour: 10 StateTree instances') ''
$packs = @(Select-String -Path $runLog -Pattern 'Jam pack activated').Count
Check 'boot: three Game Feature jam packs activated' ($packs -ge 3) "$packs activated"
Check 'boot: no StateTree fallback, no screenshot failure' (-not (LogHas $runLog 'StateTrees MISSING|not ready to run') -and (Test-Path $shot)) ''

# ---- 4. audio meters -------------------------------------------------------------------------------------------------------------
if ($SkipMeters) {
	Write-Host '[skip] audio meters (-SkipMeters)'
}
else {
	& (Join-Path $PSScriptRoot 'run.ps1') -Sound -TimeoutSec 900 -Extra @('-ShiftAudioMeter=all', '-ShiftNoUI', '-ShiftNoTutorial') | Out-Null
	$meterExit = $LASTEXITCODE
	$cases = @(Select-String -Path $runLog -Pattern 'AudioMeter: \[PASS\]').Count
	$fails = @(Select-String -Path $runLog -Pattern 'AudioMeter: .*\[FAIL\]|AudioMeter: \[FAIL\]').Count
	$done = Select-String -Path $runLog -Pattern 'AudioMeter: done, (\d+) case\(s\), (\d+) failure' | Select-Object -First 1
	Check 'audio meters: every sound, the music layers, the Harmonix clock, every lane' ($meterExit -eq 0 -and $fails -eq 0 -and $done) ($done.Matches[0].Value)
}

Write-Host ''
$results | Format-Table -AutoSize | Out-String | Write-Host
$failed = @($results | Where-Object { $_.Result -eq 'FAIL' }).Count
if ($failed -gt 0) { Write-Host "$failed check(s) FAILED"; exit 1 }
Write-Host 'all checks passed'
exit 0
