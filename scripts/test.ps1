<#
.SYNOPSIS
    Runs the game-free unit tests: the protocol header layout and the link handshake
    (CMake + doctest, ctest), then the player launcher's checks (Pester 5, launcher/tests).
    Needs neither game installed; with SaPath in local.props the HUD tests also read that
    install's fonts and HUD textures (AHSR_SA_PATH).
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

# Pester 5 (GitHub's runners have it; Windows PowerShell itself ships only 3.4).
if (Get-Module -ListAvailable Pester | Where-Object { $_.Version -ge [version] '5.0' }) {
    Import-Module Pester -MinimumVersion 5.0
    $pester = New-PesterConfiguration
    $pester.Run.Path = Join-Path $RepoRoot 'launcher/tests'
    $pester.Run.Exit = $false
    $pester.Run.PassThru = $true
    $pester.Output.Verbosity = 'Detailed'
    $result = Invoke-Pester -Configuration $pester
    if ($result.Result -ne 'Passed') { throw "Launcher tests: $($result.FailedCount) failed." }
} elseif ($env:CI) {
    throw 'Pester 5 is not installed, so the launcher tests could not run.'
} else {
    Write-Host 'Launcher tests skipped: they need Pester 5 (Install-Module Pester -Scope CurrentUser -Force -SkipPublisherCheck).' -ForegroundColor Yellow
}
