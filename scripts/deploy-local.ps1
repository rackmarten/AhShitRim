<#
.SYNOPSIS
    Copies the built plugins into the local game installs (paths from local.props): the SKSE
    plugin into Skyrim's Data/SKSE/Plugins (with AhShitRim.ini pointing it at SaPath, where it reads
    San Andreas's fonts and HUD sprites, and keeping its [Input] KeyLayout), the ASI into San
    Andreas's scripts folder.
    Run scripts/build.ps1 first. Close both games before deploying.
#>
param(
    [ValidateSet('all', 'skse', 'sa')] [string] $Target = 'all',
    [ValidateSet('Release', 'Debug')] [string] $Configuration = 'Release'
)

. "$PSScriptRoot/common.ps1"
$config = Get-LocalConfig

function Copy-Built($source, $destDir) {
    if (-not (Test-Path $source)) { throw "$source not found. Run scripts/build.ps1 first." }
    New-Item -ItemType Directory -Force $destDir | Out-Null
    Copy-Item -Force $source $destDir
    Write-Host "Copied $(Split-Path -Leaf $source) -> $destDir"
}

# Copies a built binary and its .pdb, if there is one.
function Copy-WithPdb($binary, $destDir) {
    Copy-Built $binary $destDir
    $pdb = [IO.Path]::ChangeExtension($binary, '.pdb')
    if (Test-Path $pdb) { Copy-Built $pdb $destDir }
}

if ($Target -in 'all', 'skse') {
    $skyrim = Get-RequiredSetting $config 'SkyrimPath'
    $plugins = Join-Path $skyrim 'Data/SKSE/Plugins'
    Copy-WithPdb (Join-Path $RepoRoot "skse/build/$($Configuration.ToLower())/AhShitRim.dll") $plugins
    $ini = Join-Path $plugins 'AhShitRim.ini'
    # The key layout is the player's choice: a KeyLayout already in the ini stays.
    $layout = 'Modern'
    if (Test-Path $ini) {
        $kept = Select-String -Path $ini -Pattern '^\s*KeyLayout\s*=\s*(\S+)' | Select-Object -First 1
        if ($kept) { $layout = $kept.Matches[0].Groups[1].Value }
    }
    Set-Content -Path $ini -Encoding ascii -Value @(
        '[SanAndreas]', "Path=$(Get-RequiredSetting $config 'SaPath')", '',
        '[Input]', '; Modern (Shift sprints, Space jumps) or Classic (San Andreas''s own PC keys: Space sprints, Shift jumps)', "KeyLayout=$layout")
    Write-Host "Wrote $ini (KeyLayout=$layout)"
}

if ($Target -in 'all', 'sa') {
    $sa = Get-RequiredSetting $config 'SaPath'
    if (-not (Test-Path (Join-Path $sa 'gta_sa.exe'))) { throw "gta_sa.exe not found in SaPath ($sa)." }
    Copy-WithPdb (Join-Path $RepoRoot "sa/build/$Configuration/AhShitRim.asi") (Join-Path $sa 'scripts')
}
