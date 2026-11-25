# Headless picture of the see-through window (Source\ShiftGlass): no window, no swap chain, no sound, nothing on the desktop. The game renders as it does in the see-through window
# (-ShiftGlassSim: the translucent paper, the alpha-aware renderer settings, the scene alpha) into the off-screen window, and takes the pixels with their alpha, which a normal
# screenshot throws away. It saves what the desktop compositor would be given, and that picture over a striped, a black and a white desktop:
#   Saved\Shots\<Tag>.png            the normal screenshot (colour premultiplied by the alpha, alpha forced to 255)
#   Saved\Shots\<Tag>_rgb.png        the same colour, from the window's pixels
#   Saved\Shots\<Tag>_alpha.png      the alpha as grey (white = opaque)
#   Saved\Shots\<Tag>_stripes.png / _black.png / _white.png   the see-through window over those desktops
#   .\Tools\glass_sim.ps1                               opacity 70%, the QA play screen
#   .\Tools\glass_sim.ps1 -Opacity 0.4 -Tag half
#   .\Tools\glass_sim.ps1 -ResX 1000 -ResY 900 -Extra @('-ShiftNight=1')
# Not a replacement for Tools\glass_test.ps1 (the real compositor, a visible window): this checks what the game hands to it.
param(
	[double]$Opacity = 0.7,
	[string]$Tag = 'sim',
	[int]$ResX = 1280,
	[int]$ResY = 720,
	[double]$ShotDelay = 3,
	[string[]]$Extra = @('-ShiftSeed=7', '-ShiftJam=2'),
	[string]$OutDir = '',
	[int]$TimeoutSec = 280
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $OutDir) { $OutDir = Join-Path $Root 'Saved\Shots' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$ini = '-ini:Engine:[SystemSettings]:r.PostProcessing.PropagateAlpha=1,[SystemSettings]:r.TransparentViewportClear=1,[SystemSettings]:r.DefaultBackBufferPixelFormat=0,[SystemSettings]:r.D3D12.UseAllowTearing=0'
$opacityText = $Opacity.ToString('0.###', [Globalization.CultureInfo]::InvariantCulture)   # this PC may use a decimal comma
$args = @("-ShiftGlassSim=$opacityText", "-ShiftGlassDump=$(Join-Path $OutDir $Tag)", $ini) + $Extra
& (Join-Path $PSScriptRoot 'run.ps1') -Shot (Join-Path $OutDir "$Tag.png") -ShotDelay $ShotDelay -Quit -ResX $ResX -ResY $ResY -Extra $args -TimeoutSec $TimeoutSec
$code = $LASTEXITCODE
$log = Join-Path $Root 'Saved\run_last.log'
if (Test-Path $log) { Select-String -Path $log -Pattern 'Glass:' | ForEach-Object { Write-Host $_.Line.Trim() } }
exit $code
