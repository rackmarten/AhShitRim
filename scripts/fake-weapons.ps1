<#
.SYNOPSIS
    Checks CJ's weapons against a running San Andreas without Skyrim: builds and runs
    protocol/tools/fake_weapons, which links up as the Skyrim side, hands the player over, gives the
    weapon sets, then switches through every weapon with the mouse wheel, aims and fires it, and
    reports the loadout, the weapon in his hand, its model and slots and the shots counted. Exits
    non-zero when a weapon is missing from the hand, doesn't fire, or a loadout doesn't come back as
    sent. Start San Andreas first (scripts/launch-sa.ps1), and keep Skyrim closed.
.EXAMPLE
    scripts/fake-weapons.ps1
#>
. "$PSScriptRoot/common.ps1"

Enter-DevShell
Push-Location (Join-Path $RepoRoot 'protocol')
try {
    Invoke-Checked cmake @('--preset', 'tests')
    Invoke-Checked cmake @('--build', '--preset', 'tests', '--target', 'fake_weapons')
} finally { Pop-Location }
Invoke-Checked (Join-Path $RepoRoot 'protocol/build/tests/fake_weapons.exe') @()
