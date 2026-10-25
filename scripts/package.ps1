<#
.SYNOPSIS
    Builds the player release zip, out/release/AhShitRim-<version>.zip:
      - AhShitRim-Skyrim-<version>.zip, the Skyrim part to install with a mod manager (MO2,
        Vortex): SKSE/Plugins/AhShitRim.dll (+ .pdb when built) and, when they're encoded, Sweetroll FM's
        music/sweetrollfm/*.xwm at Music/AhShitRim/SweetrollFM/ with music/CREDITS.md
      - San Andreas/scripts/AhShitRim.asi, which the launcher copies into San Andreas
      - the launcher (launcher/: "Start AhShitRim.bat", "Uninstall AhShitRim.bat", AhShitRim.ps1, checks.ps1,
        install.ps1, wizard.ps1) and INSTALL.md
      - radar_map.exe, which the launcher runs to paint the radar's map from the player's own plugins
    Run scripts/build.ps1 first; this builds radar_map itself (the tools preset). The zip's version is the SKSE plugin's (skse/CMakeLists.txt).
.EXAMPLE
    scripts/package.ps1
#>
param([string] $Version)

. "$PSScriptRoot/common.ps1"

if (-not $Version) {
    $Version = (Select-String -Path (Join-Path $RepoRoot 'skse/CMakeLists.txt') -Pattern '^\s*VERSION\s+(\S+)').Matches[0].Groups[1].Value
}
$out = Join-Path $RepoRoot 'out/release'
$stage = Join-Path $out "AhShitRim-$Version"
$skyrimStage = Join-Path $out "AhShitRim-Skyrim-$Version"
$zip = "$stage.zip"
foreach ($path in $stage, $skyrimStage, $zip) { if (Test-Path $path) { Remove-Item -Recurse -Force $path } }

function Add-File($source, $root, $destDir) {
    if (-not (Test-Path $source)) { throw "$source not found. Run scripts/build.ps1 first." }
    $dir = Join-Path $root $destDir
    New-Item -ItemType Directory -Force $dir | Out-Null
    Copy-Item $source $dir
}

# ZipFile writes / separators; Windows PowerShell's Compress-Archive writes \, which some unzippers keep in the names.
function New-Zip($sourceDir, $destZip, $withFolder) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($sourceDir, $destZip, [IO.Compression.CompressionLevel]::Optimal, $withFolder)
}

# --- The Skyrim part: an archive whose root is Skyrim's Data folder ---
Add-File (Join-Path $RepoRoot 'skse/build/release/AhShitRim.dll') $skyrimStage 'SKSE/Plugins'
# The .pdb only when the build wrote one (the SKSE Release build may not).
$pdb = Join-Path $RepoRoot 'skse/build/release/AhShitRim.pdb'
if (Test-Path $pdb) { Add-File $pdb $skyrimStage 'SKSE/Plugins' } else { Write-Host 'No AhShitRim.pdb next to the SKSE plugin, the Skyrim part ships without it.' -ForegroundColor Yellow }
$music = Join-Path $RepoRoot 'music'
# music/CREDITS.md is committed before the tracks are encoded (scripts/encode-music.ps1), so no .xwm is a warning.
$tracks = @(Get-ChildItem -File (Join-Path $music 'sweetrollfm') -Filter *.xwm -ErrorAction SilentlyContinue)
if ($tracks) {
    foreach ($track in $tracks) { Add-File $track.FullName $skyrimStage 'Music/AhShitRim/SweetrollFM' }
    Add-File (Join-Path $music 'CREDITS.md') $skyrimStage 'Music/AhShitRim'
    Write-Host "Sweetroll FM: $($tracks.Count) tracks and CREDITS.md in the Skyrim part."
} else {
    Write-Host 'No music/sweetrollfm/*.xwm (scripts/encode-music.ps1 makes them): the Skyrim part has no Sweetroll FM music.' -ForegroundColor Yellow
}
New-Item -ItemType Directory -Force $stage | Out-Null
New-Zip $skyrimStage (Join-Path $stage "AhShitRim-Skyrim-$Version.zip") $false
Remove-Item -Recurse -Force $skyrimStage

# --- The San Andreas part, the launcher and the guide ---
Add-File (Join-Path $RepoRoot 'sa/build/Release/AhShitRim.asi') $stage 'San Andreas/scripts'
foreach ($file in 'Start AhShitRim.bat', 'Uninstall AhShitRim.bat', 'AhShitRim.ps1', 'checks.ps1', 'install.ps1', 'wizard.ps1') { Add-File (Join-Path $RepoRoot "launcher/$file") $stage '.' }
Enter-DevShell
Push-Location (Join-Path $RepoRoot 'protocol')
try {
    Invoke-Checked cmake @('--preset', 'tools')
    Invoke-Checked cmake @('--build', '--preset', 'tools', '--target', 'radar_map')
} finally { Pop-Location }
Add-File (Join-Path $RepoRoot 'protocol/build/tools/radar_map.exe') $stage '.'
Add-File (Join-Path $RepoRoot 'docs/INSTALL.md') $stage '.'

New-Zip $stage $zip $true
Write-Host "Packaged $zip"
