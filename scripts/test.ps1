<#
.SYNOPSIS
    Runs the game-free unit tests: the protocol header layout and the link handshake
    (CMake + doctest, ctest). Needs neither game installed; with SaPath in local.props the HUD
    tests also read that install's fonts and HUD textures (AHSR_SA_PATH).
.EXAMPLE
    scripts/test.ps1
    powershell.exe -File scripts/test.ps1   # from WSL
#>

. "$PSScriptRoot/common.ps1"

Enter-DevShell
$localProps = Join-Path $RepoRoot 'local.props'
if (Test-Path $localProps) { $env:AHSR_SA_PATH = (Get-LocalConfig)['SaPath'] }
Push-Location (Join-Path $RepoRoot 'protocol')
try {
    Invoke-Checked cmake @('--preset', 'tests')
    Invoke-Checked cmake @('--build', '--preset', 'tests')
    Invoke-Checked ctest @('--preset', 'tests')
} finally { Pop-Location }
