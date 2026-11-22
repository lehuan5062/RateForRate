# Creates the OBS profile and scene collection "ShiftTrailer" used by shoot.ps1 (window capture of the packaged game's window + the game's own audio only).
# It only ever writes those two new names under %APPDATA%\obs-studio\basic; every other profile and collection is left alone, and the
# OBS global config (where the WebSocket server lives) is never read or written here.
#   Tools\trailer\setup_obs.ps1            create them (refuses to overwrite)
#   Tools\trailer\setup_obs.ps1 -Force     rewrite the two ShiftTrailer files (e.g. after changing -RecordDir or the bitrate)
# The window is matched as "title:class:program" like OBS does; the title is tried first and the program (ShiftHappens.exe, the Development package) is the
# fallback. The Unreal Editor is another program, so it can be open meanwhile. No hotkeys are set: shoot.ps1 drives OBS through its WebSocket server.
param(
	[string]$Window = 'ShiftHappens (64-bit Development PCD3D_SM6) :UnrealWindow:ShiftHappens.exe',
	[string]$RecordDir = '',
	[int]$BitrateKbps = 12000,
	[int]$RecordSeconds = 120,
	[switch]$Software,   # x264 instead of the AMD AMF H.265 encoder
	[switch]$Force
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $RecordDir) { $RecordDir = Join-Path $Root 'Saved\Trailer\obs' }
New-Item -ItemType Directory -Force -Path $RecordDir | Out-Null
$Name = 'ShiftTrailer'
$Basic = Join-Path $env:APPDATA 'obs-studio\basic'
$ProfileDir = Join-Path $Basic "profiles\$Name"
$SceneFile = Join-Path $Basic "scenes\$Name.json"
if ((Test-Path $ProfileDir) -or (Test-Path $SceneFile)) {
	if (-not $Force) { throw "$Name already exists under $Basic (use -Force to rewrite it)" }
}
New-Item -ItemType Directory -Force -Path $ProfileDir | Out-Null

# --- profile: 1080p60, one audio track, Matroska (survives a killed OBS), AMD AMF H.265 (HEVC) at 12 Mbps (the H.264 encoder at 40 Mbps skipped 1-2% of the frames as 'encoding lag' while the game used the same GPU)
$rec = $RecordDir.Replace('\', '/')
$enc = if ($Software) { 'obs_x264' } else { 'h265_texture_amf' }
$ini = @"
[General]
Name=$Name

[Output]
Mode=Advanced

[AdvOut]
ApplyServiceSettings=false
UseRescale=false
TrackIndex=1
Encoder=$enc
RecType=Standard
RecFilePath=$rec
RecFormat2=mkv
RecUseRescale=false
RecTracks=1
RecEncoder=$enc
AudioEncoder=ffmpeg_aac
RecAudioEncoder=ffmpeg_aac
Track1Bitrate=320
Track1Name=Game

[Video]
BaseCX=1920
BaseCY=1080
OutputCX=1920
OutputCY=1080
FPSType=2
FPSCommon=60
FPSInt=60
FPSNum=60
FPSDen=1
ScaleType=bicubic
ColorFormat=NV12
ColorSpace=709
ColorRange=Partial

[Audio]
SampleRate=48000
ChannelSetup=Stereo
"@
Set-Content -Path (Join-Path $ProfileDir 'basic.ini') -Value $ini -Encoding UTF8
if ($Software) {
	$encJson = '{"rate_control":"CRF","crf":12,"preset":"veryfast","profile":"high","keyint_sec":1}'
} else {
	$encJson = "{`"rate_control`":`"VBR_LAT`",`"bitrate`":$BitrateKbps,`"keyint_sec`":1,`"preset`":`"balanced`"}"
}
Set-Content -Path (Join-Path $ProfileDir 'recordEncoder.json') -Value $encJson -Encoding UTF8
Set-Content -Path (Join-Path $ProfileDir 'streamEncoder.json') -Value $encJson -Encoding UTF8

# --- scene collection: the game window (Windows Graphics Capture, client area, no cursor) and the game's audio (application audio capture)
$canvas = '6c69626f-6273-4c00-9d88-c5136d61696e'
$videoId = [guid]::NewGuid().ToString()
$audioId = [guid]::NewGuid().ToString()
$sceneId = [guid]::NewGuid().ToString()
function New-Item2([string]$name, [string]$uuid, [int]$id) {
	[ordered]@{
		name = $name; source_uuid = $uuid; visible = $true; locked = $false; rot = 0.0; scale_ref = [ordered]@{ x = 1920.0; y = 1080.0 }
		align = 0; bounds_type = 2; bounds_align = 0; bounds_crop = $false; crop_left = 0; crop_top = 0; crop_right = 0; crop_bottom = 0
		id = $id; group_item_backup = $false; pos = [ordered]@{ x = 960.0; y = 540.0 }; pos_rel = [ordered]@{ x = 0.0; y = 0.0 }
		scale = [ordered]@{ x = 1.0; y = 1.0 }; scale_rel = [ordered]@{ x = 1.0; y = 1.0 }
		bounds = [ordered]@{ x = 1920.0; y = 1080.0 }; bounds_rel = [ordered]@{ x = 3.5555556; y = 2.0 }
		scale_filter = 'disable'; blend_method = 'default'; blend_type = 'normal'
		show_transition = [ordered]@{ duration = 0 }; hide_transition = [ordered]@{ duration = 0 }; private_settings = [ordered]@{}
	}
}
function New-Source([string]$name, [string]$uuid, [string]$kind, $settings, [int]$mixers) {
	[ordered]@{
		prev_ver = 553648129; name = $name; uuid = $uuid; id = $kind; versioned_id = $kind; settings = $settings
		mixers = $mixers; sync = 0; flags = 0; volume = 1.0; balance = 0.5; enabled = $true; muted = $false
		'push-to-mute' = $false; 'push-to-mute-delay' = 0; 'push-to-talk' = $false; 'push-to-talk-delay' = 0
		hotkeys = [ordered]@{ 'libobs.mute' = @(); 'libobs.unmute' = @(); 'libobs.monitor-on' = @(); 'libobs.monitor-off' = @(); 'libobs.push-to-mute' = @(); 'libobs.push-to-talk' = @() }
		deinterlace_mode = 0; deinterlace_field_order = 0; monitoring_type = 0; monitoring_enabled = $false; private_settings = [ordered]@{}
	}
}
$video = New-Source 'Game window' $videoId 'window_capture' ([ordered]@{ priority = 2; window = $Window; method = 2; cursor = $false; client_area = $true }) 255
$audio = New-Source 'Game audio' $audioId 'wasapi_process_output_capture' ([ordered]@{ priority = 2; window = $Window }) 1
$scene = New-Source 'ShiftTrailer scene' $sceneId 'scene' ([ordered]@{ id_counter = 2; custom_size = $false; items = @((New-Item2 'Game window' $videoId 1), (New-Item2 'Game audio' $audioId 2)) }) 0
$scene.Remove('settings'); $scene['settings'] = [ordered]@{ id_counter = 2; custom_size = $false; items = @((New-Item2 'Game window' $videoId 1), (New-Item2 'Game audio' $audioId 2)) }
$scene['canvas_uuid'] = $canvas
$scene.hotkeys = [ordered]@{ 'OBSBasic.SelectScene' = @(); 'libobs.show_scene_item.1' = @(); 'libobs.hide_scene_item.1' = @(); 'libobs.show_scene_item.2' = @(); 'libobs.hide_scene_item.2' = @() }
$h = [int][Math]::Floor($RecordSeconds / 3600); $m = [int][Math]::Floor(($RecordSeconds % 3600) / 60); $s = [int]($RecordSeconds % 60)
$collection = [ordered]@{
	name = $Name; sources = @($video, $audio, $scene); groups = @()
	scene_order = @([ordered]@{ name = 'ShiftTrailer scene' }); current_scene = 'ShiftTrailer scene'; current_program_scene = 'ShiftTrailer scene'
	canvases = @(); current_transition = 'Fade'; transition_duration = 300; transitions = @()
	quick_transitions = @([ordered]@{ name = 'Cut'; duration = 300; hotkeys = @(); id = 4; fade_to_black = $false }, [ordered]@{ name = 'Fade'; duration = 300; hotkeys = @(); id = 5; fade_to_black = $false })
	saved_projectors = @(); preview_locked = $false; scaling_enabled = $false; scaling_level = -69; scaling_off_x = 0.0; scaling_off_y = 0.0
	'virtual-camera' = [ordered]@{ type2 = 3 }
	modules = [ordered]@{
		'scripts-tool' = @()
		'output-timer' = [ordered]@{ streamTimerHours = 0; streamTimerMinutes = 0; streamTimerSeconds = 30; recordTimerHours = $h; recordTimerMinutes = $m; recordTimerSeconds = $s; autoStartStreamTimer = $false; autoStartRecordTimer = $true; pauseRecordTimer = $true }
		'auto-scene-switcher' = [ordered]@{ interval = 300; non_matching_scene = ''; switch_if_not_matching = $false; active = $false; switches = @() }
		captions = [ordered]@{ source = ''; enabled = $false; lang_id = 1033; provider = 'mssapi' }
	}
	resolution = [ordered]@{ x = 1920; y = 1080 }; version = 2
}
($collection | ConvertTo-Json -Depth 12) | Set-Content -Path $SceneFile -Encoding UTF8
"profile:    $ProfileDir"
"collection: $SceneFile"
"captures:   $Window"
"records to: $RecordDir  (encoder $enc, record timer ${RecordSeconds}s as a safety net)"
