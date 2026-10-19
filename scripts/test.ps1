<#
.SYNOPSIS
    Runs the game-free unit tests: the protocol header layout and the link handshake
    (CMake + doctest, ctest). Needs neither game installed.
.EXAMPLE
    scripts/test.ps1
    powershell.exe -File scripts/test.ps1   # from WSL
#>

. "$PSScriptRoot/common.ps1"

Enter-DevShell
Push-Location (Join-Path $RepoRoot 'protocol')
try {
    Invoke-Checked cmake @('--preset', 'tests')
    Invoke-Checked cmake @('--build', '--preset', 'tests')
    Invoke-Checked ctest @('--preset', 'tests')
} finally { Pop-Location }
