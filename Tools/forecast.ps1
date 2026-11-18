# Headless runs of the build screen's forecast (Source\ShiftHappens\Game\ShiftForecast.h): the player's final share in a world, for a build, over sample variations.
#   .\Tools\forecast.ps1 -Study -Worlds 16 -K 16 -Tag a              every world under the reachable builds x variation modes -> Saved\Forecast\study_a.csv
#   .\Tools\forecast.ps1 -Model -Worlds 20 -Tag m                   the model the build screen uses, for every world: the smoothed share of every entry for every build -> Saved\Forecast\model_m.csv
#   .\Tools\forecast.ps1 -Jam 1 -Seed 1000 -Build 4,4,4 -Samples 8   one world and build: every sample's shares and their mean
# -Modes / -Build are comma separated lists, passed as string arrays on purpose (see balance.ps1). Variation modes are a bit mask: 1 dynamics luck, 2 start of the clicking, 4 the
# bot sometimes misses the best target.
param(
	[switch]$Study,
	[switch]$Model,
	[string]$Jam = 'all',
	[int]$Worlds = 16,
	[int]$K = 16,
	[int]$Seed0 = 1000,
	[int]$Seed = 1000,
	[int]$Samples = 8,
	[int]$Mode = 7,
	[double]$Noise = 0.3,
	[string[]]$Modes = @('1,2,4,6'),
	[string[]]$Build = @(),
	[string]$BuildSet = 'all',
	[string]$Tag = 'last',
	[string]$EngineRoot = $(if ($env:SHIFT_UE_ROOT) { $env:SHIFT_UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' })
)
$ErrorActionPreference = 'Stop'
$inv = [cultureinfo]::InvariantCulture
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Exe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$args = @("`"$(Join-Path $Root 'ShiftHappens.uproject')`"", '-run=ShiftForecast', "-Jam=$Jam", "-Tag=$Tag", '-unattended', '-nosplash', '-nop4', '-NoSound', '-stdout', '-FullStdOutLogOutput', '-nullrhi')
if ($Model) {
	$args += @('-Model', "-Worlds=$Worlds", "-Seed0=$Seed0")
}
elseif ($Study) {
	$modesArg = ($Modes | ForEach-Object { $_ -split ',' } | Where-Object { $_ }) -join ','
	$args += @('-Study', "-Worlds=$Worlds", "-K=$K", "-Seed0=$Seed0", "-Modes=$modesArg", "-Noise=$($Noise.ToString($inv))", "-BuildSet=$BuildSet")
}
else {
	$buildArg = ($Build | ForEach-Object { $_ -split ',' } | Where-Object { $_ }) -join ','
	$args += @("-Seed=$Seed", "-Samples=$Samples", "-Mode=$Mode", "-Noise=$($Noise.ToString($inv))")
	if ($buildArg) { $args += "-Build=$buildArg" }
}
$log = Join-Path $Root "Saved\forecast_$Tag.log"
New-Item -ItemType Directory -Force -Path (Split-Path $log) | Out-Null
$p = Start-Process -FilePath $Exe -ArgumentList $args -PassThru -NoNewWindow -RedirectStandardOutput $log -RedirectStandardError "$log.err"
$p.WaitForExit()
Select-String -Path $log -Pattern 'LogShift: (Display: )?(Forecast|Error)' | ForEach-Object { ($_.Line -replace '^\[[^\]]*\]\[[^\]]*\]LogShift: (Display: )?', '') }
exit $p.ExitCode
