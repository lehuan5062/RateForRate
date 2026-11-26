# Runs a Python script headless inside the UE 5.8 editor (UnrealEditor-Cmd) and prints its log.
#   Tools\run_py.ps1 -Script Tools\py\build_materials.py
# The script should call unreal.SystemLibrary.quit_editor() at the end (the helper in Tools\py\shift_common.py does).
param(
	[Parameter(Mandatory = $true)][string]$Script,
	[string[]]$Extra = @(),
	[int]$TimeoutSec = 2400,
	[string]$LogFile = '',
	[string]$EngineRoot = $(if ($env:SHIFT_UE_ROOT) { $env:SHIFT_UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' })
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Project = Join-Path $Root 'RateForRate.uproject'
$ScriptPath = (Resolve-Path $Script).Path
$Exe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path $Exe)) { throw "UnrealEditor-Cmd.exe not found at $Exe" }
if (-not $LogFile) { $LogFile = Join-Path $Root ("Saved\py_" + [IO.Path]::GetFileNameWithoutExtension($ScriptPath) + ".log") }
New-Item -ItemType Directory -Force -Path (Split-Path $LogFile) | Out-Null
$Err = "$LogFile.err"

# -SCCProvider=None: the project is a git repository and the editor would pick the Git provider, which cannot "check out" assets that are
# tracked but were just rewritten (build_to_asset then fails with 'CheckOut not supported').
$args = @("`"$Project`"", "-ExecutePythonScript=`"$ScriptPath`"", '-unattended', '-nosplash', '-nop4', '-SCCProvider=None', '-stdout', '-FullStdOutLogOutput', '-NoSound') + $Extra
Write-Host "$Exe $($args -join ' ')"
$p = Start-Process -FilePath $Exe -ArgumentList $args -PassThru -NoNewWindow -RedirectStandardOutput $LogFile -RedirectStandardError $Err
if (-not $p.WaitForExit($TimeoutSec * 1000)) {
	Write-Host "TIMEOUT after $TimeoutSec s, killing editor (pid $($p.Id))"
	Stop-Process -Id $p.Id -Force
	exit 124
}
Write-Host "Editor exit code: $($p.ExitCode). Log: $LogFile"
exit $p.ExitCode
