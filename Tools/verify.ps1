# One-command check of what this project claims, all headless (no window, no console, nothing audible):
#   .\Tools\verify.ps1                 everything below
#   .\Tools\verify.ps1 -SkipMeters     skip the ~3 minute audio meter run
#   .\Tools\verify.ps1 -Balance        also run the balance gate (Tools\balance_gate.ps1: every row of Data\BalanceTargets.csv on two seed sets, about 20 minutes)
#   .\Tools\verify.ps1 -Forecast       also run the forecast gate (Tools\forecast_check.ps1: every row of Data\ForecastTargets.csv, about 1.5 minutes)
#
#   0. sim core       the standalone harness copy (Tools\SimHarness\core) is identical to the numeric core the game compiles (Source\ShiftHappens\Sim)
#   1. audio assets   no .wav anywhere, no SoundWave assets or dependencies, every MetaSound source and patch present (editor, Tools\py\verify_audio.py)
#   2. behaviour      the full run on the real StateTrees: 10 instances, every kind of action happens (rival, other entries, mods, ambient events),
#                     and two identical runs give an identical balance table (determinism)
#   3. game boot      the real game starts, loads 26 of 26 effects and the music, binds the Harmonix clock and receives beat events, activates the
#                     three Game Feature jam packs
#   4. audio meters   every sound audible and below -1 dBFS, music day / night / tension as designed, Harmonix clock steady, every pattern lane and
#                     voice firing (the game plays them inaudibly and reads the in-graph meters; see UShiftAudioMeter). The run starts with a warm-up that is
#                     replayed until meter data really arrives, and a one-shot that loses its meter data to a busy game thread is retried before it fails
#   5b. lessons       both guided lessons (Learn to play) are played through by the QA autoplay, every step ended by its own proof (the sim or an event)
#   5. slow first start  the cold-start failure path forced on purpose (-ShiftAudioMeterDrop throws away the meter data of the first warm-up play and of the
#                     first real case, as a busy game thread does on a cold first run): the warm-up is replayed, the case retried, and both still pass
param(
	[switch]$SkipMeters,
	[switch]$Balance,
	[switch]$Forecast
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$results = New-Object System.Collections.Generic.List[object]

function Check([string]$Name, [bool]$Ok, [string]$Detail = '') {
	$results.Add([pscustomobject]@{ Check = $Name; Result = $(if ($Ok) { 'PASS' } else { 'FAIL' }); Detail = $Detail })
	Write-Host ("[{0}] {1} {2}" -f $(if ($Ok) { 'PASS' } else { 'FAIL' }), $Name, $Detail)
}

function LogHas([string]$Path, [string]$Pattern) { return [bool](Select-String -Path $Path -Pattern $Pattern -Quiet) }

# ---- 0. sim core -----------------------------------------------------------------------------------------------------------------
& (Join-Path $PSScriptRoot 'sync_sim_core.ps1') -Check | Out-Null
Check 'sim core: harness copy identical to Source\ShiftHappens\Sim' ($LASTEXITCODE -eq 0) ''

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
	$warmLine = Select-String -Path $runLog -Pattern 'AudioMeter: warm-up done \(.*\)' | Select-Object -First 1
	$meterDetail = @($(if ($warmLine) { $warmLine.Matches[0].Value } else { 'no warm-up line' }), $(if ($done) { $done.Matches[0].Value } else { 'no summary line' })) -join '; '
	Check 'audio meters: every sound, the music layers, the Harmonix clock, every lane' ($meterExit -eq 0 -and $fails -eq 0 -and $done -and $warmLine) $meterDetail

	# ---- 5. slow first start ---------------------------------------------------------------------------------------------------------
	# The failure the playtest hit, forced on purpose: the meter data of the first warm-up play (start 1) and of the first real case (start 3) is thrown away.
	& (Join-Path $PSScriptRoot 'run.ps1') -Sound -TimeoutSec 300 -Extra @('-ShiftAudioMeter=MS_Sfx_Click,MS_Sfx_Hover', '-ShiftAudioMeterDrop=1,3', '-ShiftNoUI', '-ShiftNoTutorial') | Out-Null
	$dropExit = $LASTEXITCODE
	$warmDrop = Select-String -Path $runLog -Pattern 'AudioMeter: warm-up done \((\d+) meter updates, (\d+) attempt\(s\), [\d.]+ s\)' | Select-Object -First 1
	$retried = LogHas $runLog 'AudioMeter: MS_Sfx_Click: no meter data, retry 1/2'
	$dropPasses = @(Select-String -Path $runLog -Pattern 'AudioMeter: \[PASS\]').Count
	$dropFails = @(Select-String -Path $runLog -Pattern 'AudioMeter: .*\[FAIL\]|AudioMeter: \[FAIL\]').Count
	$warmOk = $warmDrop -and [int]$warmDrop.Matches[0].Groups[1].Value -gt 0 -and [int]$warmDrop.Matches[0].Groups[2].Value -ge 2
	Check 'audio meters: slow first start (warm-up replayed, lost case retried)' ($dropExit -eq 0 -and $dropFails -eq 0 -and $dropPasses -eq 2 -and $warmOk -and $retried) $(if ($warmDrop) { $warmDrop.Matches[0].Value } else { 'no warm-up line' })
}

# ---- 6. balance gate (opt-in) ----------------------------------------------------------------------------------------------------
if ($Balance) {
	$gate = & (Join-Path $PSScriptRoot 'balance_gate.ps1') -Runs 100 -Seeds 1000,500000   # two disjoint seed sets: win rates carry about +-5 points of noise at 100 runs
	$gateExit = $LASTEXITCODE
	$gate | Write-Host
	$gateSummary = @($gate | Where-Object { $_ -match 'balance gate:' }) -join ' '
	Check 'balance gate: every target in Data\BalanceTargets.csv is met' ($gateExit -eq 0) $gateSummary
}
else {
	Write-Host '[skip] balance gate (-Balance)'
}

# ---- 7. forecast gate (opt-in) ---------------------------------------------------------------------------------------------------
if ($Forecast) {
	$fc = & (Join-Path $PSScriptRoot 'forecast_check.ps1')
	$fcExit = $LASTEXITCODE
	$fc | Write-Host
	$fcSummary = @($fc | Where-Object { $_ -match 'forecast gate:' }) -join ' '
	Check 'forecast gate: every target in Data\ForecastTargets.csv is met' ($fcExit -eq 0) $fcSummary
}
else {
	Write-Host '[skip] forecast gate (-Forecast)'
}

# ---- 5b. guided lessons ----------------------------------------------------------------------------------------------------------
foreach ($chapter in 1, 2) {
	& (Join-Path $PSScriptRoot 'run.ps1') -TimeoutSec 400 -Extra @("-ShiftLesson=$chapter", '-ShiftLessonAuto', '-ShiftSpeed=4') | Out-Null
	$lessonExit = $LASTEXITCODE
	Check "lessons: lesson $chapter completes by itself (every step proven by the sim or an event)" ($lessonExit -eq 0 -and (LogHas $runLog "Lesson $chapter autoplay: complete")) ''
}

Write-Host ''
$results | Format-Table -AutoSize | Out-String | Write-Host
$failed = @($results | Where-Object { $_.Result -eq 'FAIL' }).Count
if ($failed -gt 0) { Write-Host "$failed check(s) FAILED"; exit 1 }
Write-Host 'all checks passed'
exit 0
