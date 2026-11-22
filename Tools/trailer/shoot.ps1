# Records the scripted trailer run with OBS from the PACKAGED game (Tools\package.ps1, Development build): it plays Data\Trailer.csv by itself
# (Source\ShiftHappens\Trailer), OBS captures that window and the game's own audio (window capture + application audio capture): the game records
# nothing and no .wav is ever made. It is meant to run while somebody works at the PC:
#   * nothing is injected: no key presses, no mouse movement. OBS is started and stopped through its own WebSocket server (local, password protected),
#     and the game's trailer cursor is painted by the game itself;
#   * the game window is sent to the back of the window stack as soon as it is on screen (primary monitor), so it does not cover the work; OBS captures
#     it anyway (Windows Graphics Capture records covered windows). A second of flicker at the start of each run while the window appears;
#   * the game plays quietly (-ShiftMasterVolume, 25% by default; finalize.ps1 brings the file to -14 LUFS anyway). Muting the game in Windows is not an option: it
#     silences OBS's application audio capture too.
#   Tools\trailer\shoot.ps1                  calibrate the picture against the sound (a 10 s run), then record the trailer with that latency
#   Tools\trailer\shoot.ps1 -Latency 0.03    skip the calibration and record with this -ShiftTrailerLatency
#   Tools\trailer\shoot.ps1 -Calibrate       only the calibration run (prints the latency)
#   Tools\trailer\shoot.ps1 -Cues <csv>      another cue file for the take instead of Data\Trailer.csv
# OBS itself must not be running (this script starts its own with the ShiftTrailer profile). Create Saved\Trailer\STOP to end it after the current run.
# Raw recordings: Saved\Trailer\raw\calib_<time>.mkv and take_<time>.mkv; Tools\trailer\finalize.ps1 makes the final files from the take.
param(
	[switch]$Calibrate,
	[double]$Latency = [double]::NaN,
	[string]$Cues = '',
	[int]$Gap = 2,
	[string]$Exe = '',
	[double]$Volume = 0.25,          # how loud the game plays while it is recorded
	[string[]]$GameArgs = @(),
	[double]$MaxDriftSeconds = 0.05   # the take is refused when the game clock slipped against wall time this much more than in the calibration run
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$Work = Join-Path $Root 'Saved\Trailer'
if (-not $Exe) { $Exe = Join-Path $Root 'Saved\PackageDev\Windows\ShiftHappens\Binaries\Win64\ShiftHappens.exe' }
$ObsExe = 'C:\Program Files\obs-studio\bin\64bit\obs64.exe'
$RecDir = Join-Path $Work 'obs'
$RawDir = Join-Path $Work 'raw'
$LogDir = Join-Path $Work 'logs'
$Stop = Join-Path $Work 'STOP'
foreach ($d in $RecDir, $RawDir, $LogDir) { New-Item -ItemType Directory -Force -Path $d | Out-Null }
if (Test-Path $Stop) { Remove-Item $Stop -Force }
if (-not (Test-Path $Exe)) { throw "$Exe not found: run Tools\package.ps1 first (the Development package)" }
$TakeCues = if ($Cues) { (Resolve-Path $Cues).Path } else { Join-Path $Root 'Data\Trailer.csv' }
$CalibCues = Join-Path $PSScriptRoot 'calibrate.csv'
if (Get-Process obs64 -ErrorAction SilentlyContinue) { throw 'OBS is already running: close it first (this script starts its own)' }
if (Get-Process ShiftHappens -ErrorAction SilentlyContinue) { throw 'a ShiftHappens game is already running: close it first (OBS would match its window too)' }

if (-not ('Trailer.Win' -as [type])) {
	Add-Type -AssemblyName System.Windows.Forms
	Add-Type -Namespace Trailer -Name Win -MemberDefinition @'
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
[StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
[DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
[DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
public delegate bool EnumProc(IntPtr h, IntPtr l);
'@
}

# --- the game's audio session in the Windows volume mixer (Core Audio): only used to clear a mute Windows remembered from an earlier run
if (-not ('Trailer.Mixer' -as [type])) {
	Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace Trailer
{
	[ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] class MMDeviceEnumeratorCom { }
	[ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
	interface IMMDeviceEnumerator
	{
		int EnumAudioEndpoints(int dataFlow, int stateMask, out IntPtr devices);
		int GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDevice device);
	}
	[ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
	interface IMMDevice
	{
		int Activate(ref Guid iid, int clsCtx, IntPtr activationParams, [MarshalAs(UnmanagedType.IUnknown)] out object iface);
	}
	[ComImport, Guid("77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
	interface IAudioSessionManager2
	{
		int GetAudioSessionControl(IntPtr sessionGuid, int flags, out IntPtr control);
		int GetSimpleAudioVolume(IntPtr sessionGuid, int flags, out IntPtr volume);
		int GetSessionEnumerator(out IAudioSessionEnumerator enumerator);
	}
	[ComImport, Guid("E2F5BB11-0570-40CA-ACDD-3AA01277DEE8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
	interface IAudioSessionEnumerator
	{
		int GetCount(out int count);
		int GetSession(int index, out IAudioSessionControl2 session);
	}
	[ComImport, Guid("BFB7FF88-7239-4FC9-8FA2-07C950BE9C6D"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
	interface IAudioSessionControl2
	{
		int GetState(out int state);
		int GetDisplayName([MarshalAs(UnmanagedType.LPWStr)] out string name);
		int SetDisplayName([MarshalAs(UnmanagedType.LPWStr)] string name, ref Guid context);
		int GetIconPath([MarshalAs(UnmanagedType.LPWStr)] out string path);
		int SetIconPath([MarshalAs(UnmanagedType.LPWStr)] string path, ref Guid context);
		int GetGroupingParam(out Guid param);
		int SetGroupingParam(ref Guid param, ref Guid context);
		int RegisterAudioSessionNotification(IntPtr client);
		int UnregisterAudioSessionNotification(IntPtr client);
		int GetSessionIdentifier([MarshalAs(UnmanagedType.LPWStr)] out string id);
		int GetSessionInstanceIdentifier([MarshalAs(UnmanagedType.LPWStr)] out string id);
		int GetProcessId(out uint pid);
		int IsSystemSoundsSession();
		int SetDuckingPreference(bool optOut);
	}
	[ComImport, Guid("87CE5498-68D6-44E5-9215-6DA47EF883D8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
	interface ISimpleAudioVolume
	{
		int SetMasterVolume(float level, ref Guid context);
		int GetMasterVolume(out float level);
		int SetMute(bool mute, ref Guid context);
		int GetMute(out bool mute);
	}
	public static class Mixer
	{
		/// <summary>Sets the mute flag of the process's audio session on the default render device; false when it has no session (yet).</summary>
		public static bool SetMute(uint pid, bool mute)
		{
			IMMDeviceEnumerator enumerator = (IMMDeviceEnumerator)new MMDeviceEnumeratorCom();
			IMMDevice device;
			if (enumerator.GetDefaultAudioEndpoint(0, 1, out device) != 0) { return false; }
			Guid iid = new Guid("77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F");
			object manager;
			if (device.Activate(ref iid, 23, IntPtr.Zero, out manager) != 0) { return false; }
			IAudioSessionEnumerator sessions;
			if (((IAudioSessionManager2)manager).GetSessionEnumerator(out sessions) != 0) { return false; }
			int count;
			sessions.GetCount(out count);
			bool done = false;
			for (int i = 0; i < count; ++i)
			{
				IAudioSessionControl2 session;
				if (sessions.GetSession(i, out session) != 0) { continue; }
				uint sessionPid;
				session.GetProcessId(out sessionPid);
				if (sessionPid == pid)
				{
					Guid context = Guid.Empty;
					((ISimpleAudioVolume)session).SetMute(mute, ref context);
					done = true;
				}
			}
			return done;
		}
	}
}
'@
}

function Fmt([double]$v) { $v.ToString('0.000', [Globalization.CultureInfo]::InvariantCulture) }

# --- OBS is started and stopped through its WebSocket server (obs-websocket 5, built into OBS 28+): a local port with a random password for this run.
# The server's settings live in OBS's own plugin_config; they are put back exactly as they were (or removed) when the script ends.
$ObsWs = Join-Path $env:APPDATA 'obs-studio\plugin_config\obs-websocket\config.json'
$WsBackupFile = Join-Path $Work 'obs_websocket_config.backup'
# the backup goes to disk first (a crash cannot lose it); a backup left by an interrupted run is the real original, so it wins
if (-not (Test-Path $WsBackupFile)) { if (Test-Path $ObsWs) { Copy-Item $ObsWs $WsBackupFile -Force } else { Set-Content -Path $WsBackupFile -Value '' -NoNewline } }
$WsBackup = Get-Content $WsBackupFile -Raw
$WsPort = 4455
$WsPassword = [Convert]::ToBase64String((1..18 | ForEach-Object { Get-Random -Maximum 256 }) -as [byte[]]).Replace('+', 'a').Replace('/', 'b').Replace('=', 'c')

function Receive-Json($ws) {
	$buf = New-Object byte[] 65536
	$ms = New-Object System.IO.MemoryStream
	do {
		$seg = New-Object System.ArraySegment[byte] -ArgumentList @(, $buf)
		$r = $ws.ReceiveAsync($seg, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
		$ms.Write($buf, 0, $r.Count)
	} until ($r.EndOfMessage)
	return [Text.Encoding]::UTF8.GetString($ms.ToArray()) | ConvertFrom-Json
}
function Send-Json($ws, $obj) {
	$bytes = [Text.Encoding]::UTF8.GetBytes(($obj | ConvertTo-Json -Depth 6 -Compress))
	$seg = New-Object System.ArraySegment[byte] -ArgumentList @(, $bytes)
	$ws.SendAsync($seg, [System.Net.WebSockets.WebSocketMessageType]::Text, $true, [Threading.CancellationToken]::None).GetAwaiter().GetResult() | Out-Null
}
function Connect-Obs() {
	for ($try = 0; $try -lt 60; $try++) {
		try {
			$ws = New-Object System.Net.WebSockets.ClientWebSocket
			$ws.ConnectAsync([Uri]"ws://127.0.0.1:$WsPort", [Threading.CancellationToken]::None).GetAwaiter().GetResult() | Out-Null
			$hello = Receive-Json $ws
			$ident = @{ op = 1; d = @{ rpcVersion = 1 } }
			if ($hello.d.authentication) {
				$sha = [Security.Cryptography.SHA256]::Create()
				$secret = [Convert]::ToBase64String($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($WsPassword + $hello.d.authentication.salt)))
				$ident.d.authentication = [Convert]::ToBase64String($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($secret + $hello.d.authentication.challenge)))
			}
			Send-Json $ws $ident
			$ok = Receive-Json $ws
			if ($ok.op -eq 2) { return $ws }
		} catch { }
		Start-Sleep -Milliseconds 700
	}
	throw 'could not reach the OBS WebSocket server (is the ShiftTrailer profile loaded? is port 4455 free?)'
}
function Invoke-Obs($ws, [string]$Type) {
	$id = [guid]::NewGuid().ToString()
	Send-Json $ws @{ op = 6; d = @{ requestType = $Type; requestId = $id } }
	for ($i = 0; $i -lt 50; $i++) {
		$m = Receive-Json $ws
		if ($m.op -eq 7 -and $m.d.requestId -eq $id) {
			if (-not $m.d.requestStatus.result) { throw "OBS refused $Type : $($m.d.requestStatus.comment)" }
			return $m.d.responseData
		}
	}
	throw "no answer from OBS to $Type"
}

# --- remember what OBS will change in its global config (the last profile and collection) so it can be put back; nothing else of it is read
$ObsGlobal = Join-Path $env:APPDATA 'obs-studio\global.ini'
$obsKeys = 'Profile', 'ProfileDir', 'SceneCollection', 'SceneCollectionFile'
$saved = @{}
if (Test-Path $ObsGlobal) {
	$section = ''
	foreach ($line in Get-Content $ObsGlobal) {
		if ($line -match '^\[(.+)\]\s*$') { $section = $Matches[1]; continue }
		if ($section -eq 'Basic') { foreach ($k in $obsKeys) { if ($line -match "^$k=") { $saved[$k] = $line } } }
	}
}

# One run: recording on, the game plays the cue file and quits by itself, recording off. Returns the raw recording.
function Record-Run([string]$Name, [string]$CueFile, [double]$Lat, [int]$LimitSec) {
	$log = Join-Path $LogDir "$Name.log"
	if (Test-Path $log) { Remove-Item $log -Force }
	$before = @(Get-ChildItem $RecDir -Filter *.mkv -ErrorAction SilentlyContinue | ForEach-Object Name)
	$ws = Connect-Obs
	Invoke-Obs $ws 'StartRecord' | Out-Null
	$new = $null
	for ($i = 0; $i -lt 60 -and -not $new; $i++) {
		Start-Sleep -Milliseconds 150
		$new = Get-ChildItem $RecDir -Filter *.mkv -ErrorAction SilentlyContinue | Where-Object { $before -notcontains $_.Name } | Select-Object -First 1
	}
	if (-not $new) { throw "OBS did not start a recording for '$Name': check the ShiftTrailer profile" }
	Start-Sleep -Milliseconds 600

	$a = @('-windowed', '-ResX=1920', '-ResY=1080', '-WinX=0', '-WinY=0', '-ForceRes', '-nosplash', '-unattended',
		"-abslog=`"$log`"", "-ShiftTrailer=`"$CueFile`"", "-ShiftTrailerLatency=$(Fmt $Lat)", '-ExecCmds="r.SetRes 1920x1080wf,t.MaxFPS 60"', '-ShiftSfxOff=Blame,ModSweep,NightOff,NightOn', "-ShiftMasterVolume=$(Fmt $Volume)") + $GameArgs
	$launched = Get-Date
	$p = Start-Process -FilePath $Exe -ArgumentList $a -PassThru -WorkingDirectory (Split-Path $Exe)
	try { $p.PriorityClass = 'High' } catch { }
	$h = [IntPtr]::Zero
	for ($i = 0; $i -lt 900 -and $h -eq [IntPtr]::Zero -and -not $p.HasExited; $i++) { Start-Sleep -Milliseconds 100; $p.Refresh(); $h = $p.MainWindowHandle }
	$client = ''
	$title = ''
	if ($h -ne [IntPtr]::Zero) {
		$title = $p.MainWindowTitle
		# a plain window is cut down to the work area (the taskbar, the title bar): the game switches itself to borderless full screen (r.SetRes ..wf),
		# so wait for the client area to become exactly 1920x1080, then send the window to the back
		$r = New-Object Trailer.Win+RECT
		$pt = New-Object Trailer.Win+POINT
		for ($i = 0; $i -lt 900 -and -not $p.HasExited; $i++) {
			[Trailer.Win]::GetClientRect($h, [ref]$r) | Out-Null
			$pt.X = 0; $pt.Y = 0; [Trailer.Win]::ClientToScreen($h, [ref]$pt) | Out-Null
			if ($r.R -eq 1920 -and $r.B -eq 1080) { break }
			Start-Sleep -Milliseconds 100
		}
		$client = "$($r.R)x$($r.B)@$($pt.X),$($pt.Y)"
		Write-Host "  game window '$title' client $client"
		if ($r.R -ne 1920 -or $r.B -ne 1080 -or $pt.X -ne 0 -or $pt.Y -ne 0) { Stop-Process -Id $p.Id -Force; throw "$Name : the client area is $client, not 1920x1080 at 0,0: the recording would be the wrong size" }
		# HWND_BOTTOM = 1; SWP_NOSIZE | SWP_NOMOVE = 0x3: the next window in line takes the focus
		[Trailer.Win]::SetWindowPos($h, [IntPtr]1, 0, 0, 0, 0, 0x3) | Out-Null
	}
	# the audio session exists once the game has opened its audio device
	$muted = $false
	$deadline = (Get-Date).AddSeconds($LimitSec)
	$mutedUntil = (Get-Date).AddSeconds(30)
	while (-not $p.HasExited -and (Get-Date) -lt $deadline) {
		# Windows remembers a program's mute between runs and a muted game records as silence: make sure it is not muted
		if (-not $muted -and (Get-Date) -lt $mutedUntil) { $muted = [Trailer.Mixer]::SetMute([uint32]$p.Id, $false) }
		Start-Sleep -Milliseconds 250
	}
	if (-not $muted) { Write-Warning "$Name : the game's audio session was not found: its mute state was not cleared (the recording is silent if Windows remembered a mute)" }
	if (-not $p.HasExited) { Write-Warning "$Name : the game did not quit in time, killing it"; Stop-Process -Id $p.Id -Force }
	$p.WaitForExit(15000) | Out-Null
	Start-Sleep -Milliseconds 800
	$stopped = Invoke-Obs $ws 'StopRecord'
	$ws.Dispose()
	$prev = -1; for ($i = 0; $i -lt 60; $i++) { Start-Sleep -Milliseconds 250; $len = (Get-Item $new.FullName).Length; if ($len -eq $prev -and $len -gt 0) { break }; $prev = $len }
	Start-Sleep -Milliseconds 500
	$dest = Join-Path $RawDir ("{0}_{1}.mkv" -f $Name, (Get-Date -Format 'yyyyMMdd_HHmmss'))
	Move-Item $new.FullName $dest -Force
	# did the game's clock keep up with the real time (and so with the music)?
	$end = if (Test-Path $log) { Select-String -Path $log -Pattern 'Trailer: the end' -SimpleMatch | Select-Object -Last 1 } else { $null }
	$drift = ''
	$slip = [double]::NaN
	if ($end -and $end.Line -match 'clock ([0-9.]+) s = ([0-9.]+) bars, wall ([0-9.]+) s') {
		$drift = "clock $($Matches[1]) s, wall $($Matches[3]) s"
		$slip = [double]::Parse($Matches[3], [Globalization.CultureInfo]::InvariantCulture) - [double]::Parse($Matches[1], [Globalization.CultureInfo]::InvariantCulture)
	}
	Write-Host ("  {0}: {1} ({2} MB) exit {3} {4}" -f $Name, (Split-Path $dest -Leaf), [math]::Round((Get-Item $dest).Length / 1MB, 1), $p.ExitCode, $drift)
	if ($end -eq $null) { Write-Warning "$Name : the log has no 'Trailer: the end' line: the run did not play to its end ($log)" }
	return [pscustomobject]@{ Name = $Name; File = $dest; Log = $log; Title = $title; Client = $client; Latency = $Lat; Drift = $drift; Slip = $slip; ExitCode = $p.ExitCode; Launched = $launched.ToString('o') }
}

$obsProc = $null
$manifest = @()
try {
	$secs = if ($Calibrate) { 120 } else { 120 + 300 }
	& (Join-Path $PSScriptRoot 'setup_obs.ps1') -Force -RecordSeconds ([int]($secs + 240)) | Out-Null
	New-Item -ItemType Directory -Force -Path (Split-Path $ObsWs) | Out-Null
	(@{ alerts_enabled = $false; auth_required = $true; first_load = $false; server_enabled = $true; server_password = $WsPassword; server_port = $WsPort } | ConvertTo-Json) | Set-Content -Path $ObsWs -Encoding UTF8
	$obsProc = Start-Process -FilePath $ObsExe -WorkingDirectory (Split-Path $ObsExe) -PassThru -ArgumentList @('--collection', 'ShiftTrailer', '--profile', 'ShiftTrailer', '--scene', '"ShiftTrailer scene"', '--disable-shutdown-check', '--disable-updater', '--minimize-to-tray')
	Start-Sleep -Seconds 6
	if ($obsProc.HasExited) { throw 'OBS exited at start' }

	$lat = $Latency
	$calibSlip = [double]::NaN
	if ([double]::IsNaN($lat) -or $Calibrate) {
		Write-Host 'calibration run (flashes on the bar lines, latency 0):'
		$c = Record-Run 'calib' $CalibCues 0.0 120
		$manifest += $c
		$calibSlip = $c.Slip
		$out = & python (Join-Path $PSScriptRoot 'measure.py') calib $c.File --used 0 2>&1
		$out | ForEach-Object { Write-Host "  $_" }
		$hit = $out | Select-String -Pattern '^LATENCY=(-?[0-9.]+)' | Select-Object -Last 1
		if (-not $hit) { throw 'the calibration found no picture/sound offset (see above): nothing is wrong with the take yet, nothing was recorded for it' }
		$lat = [double]::Parse($hit.Matches[0].Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
		Write-Host "calibrated latency: $(Fmt $lat) s"
	}
	if (-not $Calibrate -and -not (Test-Path $Stop)) {
		Start-Sleep -Seconds $Gap
		Write-Host "recording the trailer (latency $(Fmt $lat) s, cues $TakeCues):"
		$t = Record-Run 'take' $TakeCues $lat 300
		$manifest += $t
		# the film clock is the game's accumulated frame time: when the PC was busy and the game hitched, it slipped against the music's wall clock
		if (-not [double]::IsNaN($calibSlip) -and -not [double]::IsNaN($t.Slip) -and [math]::Abs($t.Slip - $calibSlip) -gt $MaxDriftSeconds) {
			Write-Warning ("the take's game clock slipped {0} s against wall time compared with the calibration run (limit {1} s): the picture is off the music, record it again when the PC is quieter" -f (Fmt ($t.Slip - $calibSlip)), (Fmt $MaxDriftSeconds))
		}
	}
}
finally {
	if ($obsProc -and -not $obsProc.HasExited) {
		# close OBS politely: WM_CLOSE to its (tray-hidden) top-level windows; the recording is already stopped, so no confirmation dialog
		$cb = [Trailer.Win+EnumProc]{ param($hw, $l) $procId = 0; [Trailer.Win]::GetWindowThreadProcessId($hw, [ref]$procId) | Out-Null; if ($procId -eq $obsProc.Id) { [Trailer.Win]::PostMessage($hw, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null }; $true }
		[Trailer.Win]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
		if (-not $obsProc.WaitForExit(20000)) { Write-Warning 'OBS did not close by itself: killing it'; Stop-Process -Id $obsProc.Id -Force }
	}
	# put OBS's own settings back: the WebSocket server config and the last profile / collection
	try {
		if ($WsBackup) { Copy-Item $WsBackupFile $ObsWs -Force } elseif (Test-Path $ObsWs) { Remove-Item $ObsWs -Force }
		Remove-Item $WsBackupFile -Force
	} catch { Write-Warning "could not put the OBS WebSocket settings back: $_ (the original is in $WsBackupFile)" }
	if ($saved.Count -gt 0 -and (Test-Path $ObsGlobal)) {
		$section = ''
		$out = foreach ($line in Get-Content $ObsGlobal) {
			if ($line -match '^\[(.+)\]\s*$') { $section = $Matches[1]; $line; continue }
			$r = $line
			if ($section -eq 'Basic') { foreach ($k in $obsKeys) { if ($line -match "^$k=" -and $saved.ContainsKey($k)) { $r = $saved[$k] } } }
			$r
		}
		Set-Content -Path $ObsGlobal -Value $out -Encoding UTF8
	}
	if ($manifest.Count -gt 0) { $manifest | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $RawDir ("manifest_{0}.json" -f (Get-Date -Format 'yyyyMMdd_HHmmss'))) -Encoding UTF8 }
}
$manifest | Format-Table Name, Latency, Client, Drift, ExitCode -AutoSize
# did the encoder keep up? (OBS counts the frames it had to skip)
$obsLog = Get-ChildItem (Join-Path $env:APPDATA 'obs-studio\logs') -Filter *.txt -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
if ($obsLog) {
	Select-String -Path $obsLog.FullName -Pattern 'Number of lagged frames|skipped frames due to encoding lag|settings:|rate_control|bitrate:|preset:' | ForEach-Object { 'OBS: ' + ($_.Line -replace '^[0-9:.]+: ', '').Trim() }
}
