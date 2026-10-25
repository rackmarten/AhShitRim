<#
.SYNOPSIS
    Runs the game-free unit tests: the protocol header layout and the link handshake
    (CMake + doctest, ctest), then the player launcher's checks (Pester 5, launcher/tests), in
    PowerShell 7 and in Windows PowerShell 5.1, which the launcher runs on.
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
    # Players run the launcher on Windows PowerShell 5.1 ("Start AhShitRim.bat"), so the tests run there
    # as well, with the same Pester by path and, as the .bat does, no module path inherited from here.
    $windowsPowerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    if ($PSVersionTable.PSEdition -eq 'Core' -and (Test-Path $windowsPowerShell)) {
        $pesterPath = (Get-Module Pester).Path
        $command = "Import-Module '$pesterPath'; `$c = New-PesterConfiguration; `$c.Run.Path = '$($pester.Run.Path.Value)'; `$c.Run.Exit = `$true; `$c.Output.Verbosity = 'Normal'; Invoke-Pester -Configuration `$c"
        $modulePath = $env:PSModulePath
        try { $env:PSModulePath = $null; & $windowsPowerShell -NoProfile -ExecutionPolicy Bypass -Command $command }
        finally { $env:PSModulePath = $modulePath }
        if ($LASTEXITCODE -ne 0) { throw "Launcher tests on Windows PowerShell 5.1: $LASTEXITCODE failed." }
    }
} elseif ($env:CI) {
    throw 'Pester 5 is not installed, so the launcher tests could not run.'
} else {
    Write-Host 'Launcher tests skipped: they need Pester 5 (Install-Module Pester -Scope CurrentUser -Force -SkipPublisherCheck).' -ForegroundColor Yellow
}
