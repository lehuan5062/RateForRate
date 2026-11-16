# Starts or stops a headless UE 5.8 editor that hosts the Unreal MCP server (loopback only, no authentication).
#   Tools\mcp_editor.ps1 start            -> launches the editor off-screen and waits until the port answers
#   Tools\mcp_editor.ps1 stop             -> kills the editor started by 'start'
#   Tools\mcp_editor.ps1 start -Visible   -> same, but with a visible editor window
# Then talk to it with:  node Tools\mcp_call.mjs toolsets
param(
	[Parameter(Mandatory = $true, Position = 0)][ValidateSet('start', 'stop', 'status')][string]$Action,
	[int]$Port = 8000,
	[int]$TimeoutSec = 600,
	[switch]$Visible,
	[string]$EngineRoot = $(if ($env:SHIFT_UE_ROOT) { $env:SHIFT_UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' })
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Project = Join-Path $Root 'ShiftHappens.uproject'
$PidFile = Join-Path $Root 'Saved\mcp_editor.pid'
$LogFile = Join-Path $Root 'Saved\mcp_editor.log'

function Test-Port([int]$P) {
	try {
		$c = New-Object System.Net.Sockets.TcpClient
		$iar = $c.BeginConnect('127.0.0.1', $P, $null, $null)
		$ok = $iar.AsyncWaitHandle.WaitOne(500) -and $c.Connected
		$c.Close()
		return $ok
	} catch { return $false }
}

if ($Action -eq 'status') {
	$alive = $false
	if (Test-Path $PidFile) { $alive = [bool](Get-Process -Id ([int](Get-Content $PidFile)) -ErrorAction SilentlyContinue) }
	Write-Host ("editor alive: {0}, port {1} open: {2}" -f $alive, $Port, (Test-Port $Port))
	exit 0
}

if ($Action -eq 'stop') {
	if (Test-Path $PidFile) {
		$id = [int](Get-Content $PidFile)
		$p = Get-Process -Id $id -ErrorAction SilentlyContinue
		if ($p) { Stop-Process -Id $id -Force; Write-Host "stopped editor pid $id" } else { Write-Host "no editor with pid $id" }
		Remove-Item $PidFile -ErrorAction SilentlyContinue
	} else { Write-Host 'no pid file' }
	exit 0
}

# start
if (Test-Port $Port) { throw "port $Port is already in use; stop the other editor first" }
$exeName = if ($Visible) { 'UnrealEditor.exe' } else { 'UnrealEditor-Cmd.exe' }
$Exe = Join-Path $EngineRoot "Engine\Binaries\Win64\$exeName"
if (-not (Test-Path $Exe)) { throw "$Exe not found" }
$args = @("`"$Project`"", '-ModelContextProtocolStartServer', "-ModelContextProtocolPort=$Port", '-unattended', '-nosplash', '-nop4', '-NoSound', '-stdout', '-FullStdOutLogOutput')
if (-not $Visible) { $args += '-RenderOffScreen' }
New-Item -ItemType Directory -Force -Path (Split-Path $LogFile) | Out-Null
$p = Start-Process -FilePath $Exe -ArgumentList $args -PassThru -WindowStyle Hidden -RedirectStandardOutput $LogFile -RedirectStandardError "$LogFile.err"
Set-Content -Path $PidFile -Value $p.Id
Write-Host "started $exeName pid $($p.Id), waiting for port $Port (log: $LogFile)"

$deadline = (Get-Date).AddSeconds($TimeoutSec)
while ((Get-Date) -lt $deadline) {
	if ($p.HasExited) { throw "editor exited early with code $($p.ExitCode); see $LogFile" }
	if (Test-Port $Port) { Write-Host "MCP port $Port is open"; exit 0 }
	Start-Sleep -Seconds 2
}
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
throw "timed out after $TimeoutSec s waiting for port $Port"
