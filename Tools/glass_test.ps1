# Visible-window test of the see-through window (Source\ShiftGlass). It opens a striped (or plain black / white) full-screen backdrop on a monitor (the SECONDARY one by default;
# pass -Monitor primary only when the user is away), starts the game windowed on top of it with the see-through window on, takes a screenshot of the desktop and compares the game
# window with the backdrop-only screenshot: a pixel that equals the backdrop is see-through. No sound, no keys, no mouse. About 30 seconds. The game and the backdrop are
# closed at the end. It needs an unlocked screen with the display on (a locked session captures black).
#   .\Tools\glass_test.ps1                         the letterboxed 1280 x 800 window (the engine's bars must show the desktop: about 10% of the client area equals the backdrop)
#   .\Tools\glass_test.ps1 -Opacity 0.4 -Tag half  the paper at 40% (needs the glass background material: Tools\py\build_materials.py)
#   .\Tools\glass_test.ps1 -Backdrop black         the same window over black and over white (-Backdrop white): paper pixels that differ between the two are translucent
# Output: Saved\Shots\glass_<tag>.png (the desktop with the game on it), glass_<tag>_later.png (6 s later), glass_<tag>_backdrop.png, glass_<tag>.log (the game log), a summary on
# the console.
param(
	[double]$Opacity = -1,
	[int]$Width = 1280,
	[int]$Height = 800,
	[int]$SettleSeconds = 6,
	[string]$Tag = 'test',
	[string[]]$Extra = @(),
	[string]$StartScreen = 'title',   # -ShiftScreen value: title, play, ...
	[switch]$Stress,   # after the first picture: resize the window, minimise and restore it, and report the see-through share after each step
	[switch]$Packaged,   # test Saved\PackageShipping\Windows\ShiftHappens.exe instead of the editor build (no log in Shipping: it waits a fixed time)
	[switch]$NoGlass,
	[ValidateSet('primary', 'secondary')][string]$Monitor = 'secondary',   # which monitor the backdrop and the game window use
	[ValidateSet('stripes', 'black', 'white')][string]$Backdrop = 'stripes',   # black and white: the same window over both tells transparent from opaque pixels
	[string]$EngineRoot = $(if ($env:SHIFT_UE_ROOT) { $env:SHIFT_UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' })
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Project = Join-Path $Root 'ShiftHappens.uproject'
$Ue = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$Shots = Join-Path $Root 'Saved\Shots'
New-Item -ItemType Directory -Force -Path $Shots | Out-Null
$Scratch = Join-Path $env:TEMP 'shift_glass_test'
New-Item -ItemType Directory -Force -Path $Scratch | Out-Null

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
$drawingRefs = if ($PSVersionTable.PSEdition -eq 'Core') { @('System.Drawing.Common', 'System.Drawing.Primitives', 'System.Runtime.InteropServices', 'System.Runtime') } else { @('System.Drawing') }
Add-Type -ReferencedAssemblies $drawingRefs -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class GlassWin
{
	[StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
	[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
	[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
	[DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
	[DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref Point p);
	[DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
	[DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
	[DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int cx, uint flags);
	[DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
	[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
	public static int ZIndex(IntPtr h) { int n = 0; IntPtr w = GetWindow(h, 3); while (w != IntPtr.Zero && n < 5000) { n++; w = GetWindow(w, 3); } return n; }   // windows above h (GW_HWNDPREV)
	[DllImport("user32.dll")] static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
	public static bool PrintInto(IntPtr h, IntPtr dc) { return PrintWindow(h, dc, 2); }   // PW_RENDERFULLCONTENT: the window's own composed pixels
	[DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr h);
	[DllImport("user32.dll")] static extern int ReleaseDC(IntPtr h, IntPtr dc);
	[DllImport("gdi32.dll")] static extern bool BitBlt(IntPtr dst, int x, int y, int w, int h, IntPtr src, int sx, int sy, int rop);
	public static bool CaptureInto(IntPtr dst, int sx, int sy, int w, int h)
	{
		IntPtr screen = GetDC(IntPtr.Zero);
		bool ok = BitBlt(dst, 0, 0, w, h, screen, sx, sy, 0x40CC0020);   // SRCCOPY | CAPTUREBLT
		ReleaseDC(IntPtr.Zero, screen);
		return ok;
	}
	[DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int attr, out int value, int size);
	[DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")] public static extern IntPtr GetWindowLongPtr(IntPtr h, int index);
	[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);

	// how many pixels of the region equal the baseline (within tol per channel), per horizontal band
	public static int[] Compare(Bitmap game, Bitmap baseline, int x0, int y0, int w, int h, int tol, int bands)
	{
		Rectangle rect = new Rectangle(0, 0, game.Width, game.Height);
		BitmapData a = game.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
		BitmapData b = baseline.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
		byte[] pa = new byte[a.Stride * a.Height];
		byte[] pb = new byte[b.Stride * b.Height];
		Marshal.Copy(a.Scan0, pa, 0, pa.Length);
		Marshal.Copy(b.Scan0, pb, 0, pb.Length);
		int stride = a.Stride;
		game.UnlockBits(a);
		baseline.UnlockBits(b);
		int[] same = new int[bands];
		for (int y = 0; y < h; y++)
		{
			int row = (y0 + y) * stride + x0 * 4;
			int band = Math.Min(bands - 1, y * bands / h);
			for (int x = 0; x < w; x++)
			{
				int i = row + x * 4;
				if (Math.Abs(pa[i] - pb[i]) <= tol && Math.Abs(pa[i + 1] - pb[i + 1]) <= tol && Math.Abs(pa[i + 2] - pb[i + 2]) <= tol) { same[band]++; }
			}
		}
		return same;
	}
}
'@
[GlassWin]::SetProcessDPIAware() | Out-Null
$Target = [System.Windows.Forms.Screen]::AllScreens | Where-Object { $Monitor -eq 'primary' -eq $_.Primary } | Select-Object -First 1
if (-not $Target) { $Target = [System.Windows.Forms.Screen]::PrimaryScreen }
$Screen = $Target.Bounds
Write-Host ('monitor: {0} {1},{2} {3}x{4}' -f $Target.DeviceName, $Screen.X, $Screen.Y, $Screen.Width, $Screen.Height)

function Take-Screen([string]$Path) {
	$bmp = New-Object System.Drawing.Bitmap($Screen.Width, $Screen.Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
	$g = [System.Drawing.Graphics]::FromImage($bmp)
	$hdc = $g.GetHdc()
	[GlassWin]::CaptureInto($hdc, $Screen.X, $Screen.Y, $Screen.Width, $Screen.Height) | Out-Null   # BitBlt with CAPTUREBLT: layered and composition surfaces too
	$g.ReleaseHdc($hdc)
	$g.Dispose()
	$bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
	return $bmp
}

# the backdrop: six vertical stripes over the whole primary monitor, in another process so it cannot take the game's focus
$BackdropScript = Join-Path $Scratch 'backdrop.ps1'
$BackdropText = @'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
$f = New-Object System.Windows.Forms.Form
$f.FormBorderStyle = 'None'
$f.StartPosition = 'Manual'
$f.ShowInTaskbar = $false
$f.Bounds = New-Object System.Drawing.Rectangle(BACKDROPBOUNDS)
$f.Add_Paint({
	param($s, $e)
	$cols = @(BACKDROPCOLORS)
	$w = [int][Math]::Ceiling($f.ClientSize.Width / $cols.Count)
	for ($i = 0; $i -lt $cols.Count; $i++) {
		$br = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml($cols[$i]))
		$e.Graphics.FillRectangle($br, $i * $w, 0, $w + 1, $f.ClientSize.Height)
	}
})
[System.Windows.Forms.Application]::Run($f)
'@
$Colors = switch ($Backdrop) { 'black' { "'#000000'" } 'white' { "'#FFFFFF'" } default { "'#E53935', '#43A047', '#1E88E5', '#FDD835', '#8E24AA', '#00ACC1'" } }
(($BackdropText -replace 'BACKDROPCOLORS', $Colors) -replace 'BACKDROPBOUNDS', ('{0}, {1}, {2}, {3}' -f $Screen.X, $Screen.Y, $Screen.Width, $Screen.Height)) | Set-Content -Path $BackdropScript -Encoding UTF8

$game = $null
$backdropProc = $null
try {
	$backdropProc = Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoProfile', '-WindowStyle', 'Hidden', '-File', "`"$BackdropScript`"") -PassThru
	Start-Sleep -Seconds 4
	$baselinePath = Join-Path $Shots "glass_${Tag}_backdrop.png"
	$baseline = Take-Screen $baselinePath

	$log = Join-Path $Scratch 'game.log'
	Remove-Item $log -ErrorAction SilentlyContinue
	$ini = '-ini:Engine:[SystemSettings]:r.PostProcessing.PropagateAlpha=1,[SystemSettings]:r.TransparentViewportClear=1,[SystemSettings]:r.DefaultBackBufferPixelFormat=0,[SystemSettings]:r.D3D12.UseAllowTearing=0'
	$gameArgs = @("`"$Project`"", '-game', "-ResX=$Width", "-ResY=$Height", '-windowed', '-nosplash', '-nop4', '-unattended', '-NoSound', "-WinX=$($Screen.X + 60)", "-WinY=$($Screen.Y + 20)", "-abslog=`"$log`"", "-ShiftScreen=$StartScreen")
	if (-not $NoGlass) {
		$gameArgs += $ini
		$gameArgs += $(if ($Opacity -ge 0) { "-ShiftGlass=$Opacity" } else { '-ShiftGlass' })
	}
	$gameArgs += $Extra
	if ($Packaged) {
		$Ue = Join-Path $Root 'Saved\PackageShipping\Windows\ShiftHappens.exe'
		$gameArgs = @($gameArgs | Where-Object { $_ -ne '-game' -and $_ -ne "`"$Project`"" -and $_ -notlike '-abslog=*' })
	}
	Write-Host "$Ue $($gameArgs -join ' ')"
	$game = Start-Process -FilePath $Ue -ArgumentList $gameArgs -PassThru

	# wait for the run to start, then let the picture settle
	$deadline = (Get-Date).AddSeconds(120)
	while ((Get-Date) -lt $deadline) {
		if ($game.HasExited) { throw "the game exited early (code $($game.ExitCode)); log: $log" }
		if ($Packaged -and ((Get-Date) -gt $deadline.AddSeconds(-90))) { break }   # no log in Shipping: 30 s
		if ((Test-Path $log) -and (Select-String -Path $log -Pattern 'Run started' -Quiet -ErrorAction SilentlyContinue)) { break }
		Start-Sleep -Milliseconds 500
	}
	if ($Packaged) {   # the launcher exe hands over to the real process, which owns the window
		$real = Get-Process -Name 'ShiftHappens-Win64-Shipping' -ErrorAction SilentlyContinue | Select-Object -First 1
		if (-not $real) { throw 'the packaged game is not running' }
		$game = $real
	}
	# Windows does not let a new process take the focus from the backdrop, and then puts its window behind it: put the game window above everything (z-order only, no input)
	$game.Refresh()
	[GlassWin]::SetWindowPos($game.MainWindowHandle, [IntPtr](-1), 0, 0, 0, 0, 0x0013) | Out-Null   # HWND_TOPMOST, NOMOVE | NOSIZE | NOACTIVATE
	Start-Sleep -Seconds $SettleSeconds

	$shotPath = Join-Path $Shots "glass_$Tag.png"
	$game.Refresh()
	$backdropProc.Refresh()
	Write-Host ("windows above the game: {0}, above the backdrop: {1}" -f [GlassWin]::ZIndex($game.MainWindowHandle), [GlassWin]::ZIndex($backdropProc.MainWindowHandle))
	$fg = [GlassWin]::GetForegroundWindow()
	Write-Host ("foreground: {0} (game window {1}), iconic {2}, visible {3}" -f $fg, $game.MainWindowHandle, [GlassWin]::IsIconic($game.MainWindowHandle), [GlassWin]::IsWindowVisible($game.MainWindowHandle))
	$cloaked = 0
	[GlassWin]::DwmGetWindowAttribute($game.MainWindowHandle, 14, [ref]$cloaked, 4) | Out-Null   # DWMWA_CLOAKED
	Write-Host ("cloaked {0}, style 0x{1:X}, exstyle 0x{2:X}" -f $cloaked, [GlassWin]::GetWindowLongPtr($game.MainWindowHandle, -16).ToInt64(), [GlassWin]::GetWindowLongPtr($game.MainWindowHandle, -20).ToInt64())
	$shot = Take-Screen $shotPath

	$game.Refresh()
	$r = New-Object GlassWin+RECT
	$ok = $game.MainWindowHandle -ne [IntPtr]::Zero -and [GlassWin]::GetWindowRect($game.MainWindowHandle, [ref]$r)
	if (-not $ok) { throw 'could not find the game window' }
	$c = New-Object GlassWin+RECT
	[GlassWin]::GetClientRect($game.MainWindowHandle, [ref]$c) | Out-Null
	$pt = New-Object System.Drawing.Point(0, 0)
	[GlassWin]::ClientToScreen($game.MainWindowHandle, [ref]$pt) | Out-Null
	$cw = $c.Right - $c.Left
	$ch = $c.Bottom - $c.Top
	Write-Host ("window {0},{1} {2}x{3}; client at {4},{5} {6}x{7}" -f $r.Left, $r.Top, ($r.Right - $r.Left), ($r.Bottom - $r.Top), $pt.X, $pt.Y, $cw, $ch)

	$bands = 10
	$regionW = [Math]::Min($cw, $Screen.Width - ($pt.X - $Screen.X))
	$regionH = [Math]::Min($ch, $Screen.Height - ($pt.Y - $Screen.Y))
	$total = $regionW * $regionH
	$same = [GlassWin]::Compare($shot, $baseline, [Math]::Max(0, $pt.X - $Screen.X), [Math]::Max(0, $pt.Y - $Screen.Y), $regionW, $regionH, 4, $bands)
	$perBand = [Math]::Max(1, [int]($total / $bands))
	Write-Host 'share of the client area that equals the backdrop (see-through), top to bottom in 10 bands:'
	Write-Host (($same | ForEach-Object { '{0,4:P0}' -f ($_ / $perBand) }) -join ' ')
	Write-Host ('whole client area: {0:P1}' -f (($same | Measure-Object -Sum).Sum / [double]$total))
	if ($Stress) {
		function Share([string]$Name) {
			$g = [GlassWin+RECT]::new()
			[GlassWin]::GetClientRect($game.MainWindowHandle, [ref]$g) | Out-Null
			$p = New-Object System.Drawing.Point(0, 0)
			[GlassWin]::ClientToScreen($game.MainWindowHandle, [ref]$p) | Out-Null
			$w = [Math]::Min($g.Right - $g.Left, $Screen.Width - ($p.X - $Screen.X)); $h = [Math]::Min($g.Bottom - $g.Top, $Screen.Height - ($p.Y - $Screen.Y))
			$img = Take-Screen (Join-Path $Shots "glass_${Tag}_$Name.png")
			$eq = [GlassWin]::Compare($img, $baseline, [Math]::Max(0, $p.X - $Screen.X), [Math]::Max(0, $p.Y - $Screen.Y), $w, $h, 4, 1)
			Write-Host ('stress {0,-10} client {1}x{2}: equal to the backdrop {3:P1}' -f $Name, ($g.Right - $g.Left), ($g.Bottom - $g.Top), ($eq[0] / [double][Math]::Max(1, $w * $h)))
		}
		$h = $game.MainWindowHandle
		[GlassWin]::SetWindowPos($h, [IntPtr](-1), $Screen.X + 40, $Screen.Y + 10, 1000, 700, 0x0010) | Out-Null   # resize (no activate)
		Start-Sleep -Seconds 4; Share 'resized'
		[GlassWin]::SetWindowPos($h, [IntPtr](-1), $Screen.X + 20, $Screen.Y + 10, 1500, 850, 0x0010) | Out-Null
		Start-Sleep -Seconds 4; Share 'bigger'
		[GlassWin]::ShowWindow($h, 6) | Out-Null   # SW_MINIMIZE
		Start-Sleep -Seconds 3
		[GlassWin]::ShowWindow($h, 9) | Out-Null   # SW_RESTORE
		[GlassWin]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 0x0013) | Out-Null
		Start-Sleep -Seconds 4; Share 'restored'
	}
	Start-Sleep -Seconds 6
	$later = Take-Screen (Join-Path $Shots "glass_${Tag}_later.png")
	$sameLater = [GlassWin]::Compare($later, $baseline, [Math]::Max(0, $pt.X - $Screen.X), [Math]::Max(0, $pt.Y - $Screen.Y), $regionW, $regionH, 4, $bands)
	Write-Host ('6 s later, whole client area equal to the backdrop: {0:P1}' -f (($sameLater | Measure-Object -Sum).Sum / [double]$total))
	Write-Host "screenshots: $shotPath , $baselinePath"
	Copy-Item -Path $log -Destination (Join-Path $Shots "glass_$Tag.log") -ErrorAction SilentlyContinue
	if (Test-Path $log) {
		Select-String -Path $log -Pattern 'Glass:' | ForEach-Object { Write-Host $_.Line.Trim() }
	}
}
finally {
	if ($game -and -not $game.HasExited) { Stop-Process -Id $game.Id -Force -ErrorAction SilentlyContinue }
	if ($Packaged) { Stop-Process -Name 'ShiftHappens-Win64-Shipping' -Force -ErrorAction SilentlyContinue }   # the launcher exe starts the real process
	if ($backdropProc -and -not $backdropProc.HasExited) { Stop-Process -Id $backdropProc.Id -Force -ErrorAction SilentlyContinue }
}
