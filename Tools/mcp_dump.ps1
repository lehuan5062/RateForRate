# Starts the headless MCP editor, dumps describe_toolset for the toolsets we care about into Saved\mcp\describe_<name>.json, stops it.
#   Tools\mcp_dump.ps1
param([string[]]$Toolsets = @(
	'state_tree_toolset.toolsets.state_tree.StateTreeTools',
	'editor_toolset.toolsets.material.MaterialTools',
	'editor_toolset.toolsets.data_table.DataTableTools',
	'editor_toolset.toolsets.curve_table.CurveTableTools',
	'editor_toolset.toolsets.data_asset.DataAssetTools',
	'editor_toolset.toolsets.asset.AssetTools',
	'editor_toolset.toolsets.scene.SceneTools',
	'editor_toolset.toolsets.actor.ActorTools',
	'editor_toolset.toolsets.object.ObjectTools',
	'editor_toolset.toolsets.programmatic.ProgrammaticToolset',
	'EditorToolset.EditorAppToolset',
	'EditorToolset.LogsToolset',
	'SlateInspectorToolset.SlateInspectorToolset',
	'GameFeaturesToolset.GameFeaturesToolset',
	'PluginToolset.PluginToolset',
	'AutomationTestToolset.AutomationTestToolset',
	'ConfigSettingsToolset.ConfigSettingsToolset',
	'DataRegistryToolset.DataRegistryTools'
))
$ErrorActionPreference = 'Continue'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Out = Join-Path $Root 'Saved\mcp'
New-Item -ItemType Directory -Force -Path $Out | Out-Null
try {
	& (Join-Path $PSScriptRoot 'mcp_editor.ps1') start -TimeoutSec 900
	foreach ($t in $Toolsets) {
		$file = Join-Path $Out ("describe_" + ($t -replace '[^A-Za-z0-9_.]', '_') + ".json")
		node (Join-Path $PSScriptRoot 'mcp_call.mjs') describe $t > $file 2> "$file.err"
		Write-Host ("{0,-70} exit {1}  {2} bytes" -f $t, $LASTEXITCODE, (Get-Item $file).Length)
	}
} catch {
	Write-Host "ERROR: $($_.Exception.Message)"
} finally {
	& (Join-Path $PSScriptRoot 'mcp_editor.ps1') stop
}
