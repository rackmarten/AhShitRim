<#
.SYNOPSIS
    Drives a running San Andreas without Skyrim: builds and runs protocol/tools/fake_skyrim, which
    links up as the Skyrim side, hands the player over, holds W and prints CJ's poses. Exits non-zero
    if CJ didn't walk. Start San Andreas first (scripts/launch-sa.ps1), and keep Skyrim closed.
.EXAMPLE
    scripts/fake-skyrim.ps1
    scripts/fake-skyrim.ps1 -WalkSeconds 5
#>
param([double] $WalkSeconds = 3)

. "$PSScriptRoot/common.ps1"

Enter-DevShell
Push-Location (Join-Path $RepoRoot 'protocol')
try {
    Invoke-Checked cmake @('--preset', 'tests')
    Invoke-Checked cmake @('--build', '--preset', 'tests', '--target', 'fake_skyrim')
} finally { Pop-Location }
Invoke-Checked (Join-Path $RepoRoot 'protocol/build/tests/fake_skyrim.exe') @("$WalkSeconds")
