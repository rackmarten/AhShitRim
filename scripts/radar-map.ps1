<#
.SYNOPSIS
    Builds protocol/tools/radar_map (Release) and runs it on the Skyrim install in local.props: it
    paints the radar's San Andreas-style map of Tamriel from your own plugins (the load order in
    plugins.txt) into Documents\My Games\AhShitRim\radar-map.png, with radar-map.json beside it.
    Skips the painting when the map there was made from the same plugins, unless -Force. Needs
    neither game running. Never commit the image: it's made from Bethesda's data.
.EXAMPLE
    scripts/radar-map.ps1
    scripts/radar-map.ps1 -Force -NoMods      # the base game and its DLC only, painted again
    scripts/radar-map.ps1 -Out C:\Temp\radar  # somewhere else
#>
param(
    [switch] $Force,
    [switch] $NoMods,
    [string] $Out
)

. "$PSScriptRoot/common.ps1"

Enter-DevShell
Push-Location (Join-Path $RepoRoot 'protocol')
try {
    Invoke-Checked cmake @('--preset', 'tools')
    Invoke-Checked cmake @('--build', '--preset', 'tools', '--target', 'radar_map')
} finally { Pop-Location }

$skyrim = Get-RequiredSetting (Get-LocalConfig) 'SkyrimPath'
$arguments = @('--skyrim', $skyrim)
if ($Force) { $arguments += '--force' }
if ($NoMods) { $arguments += '--no-mods' }
if ($Out) { $arguments += @('--out', $Out) }
Invoke-Checked (Join-Path $RepoRoot 'protocol/build/tools/radar_map.exe') $arguments
