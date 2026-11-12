<#
.SYNOPSIS
    Test sessions (docs/DEV_SETUP.md, "Test sessions"): starts Skyrim hidden and never focused, straight into a test
    state, with San Andreas beside it, and stops both again. Forwards to skse-test-kit's own scripts/skytest.ps1 (the
    skse/extern/skse-test-kit submodule) with every argument; skytest.psd1 holds AhShitRim's settings.
    Build a dev build first (scripts/build.ps1 links the kit when the submodule is there).
.EXAMPLE
    scripts/skytest.ps1 start                         # Riverwood, CJ there once San Andreas has the player
    scripts/skytest.ps1 start -Start save:latest      # the most recent save instead (or save:<name>, cow:..., menu)
    scripts/skytest.ps1 start -NoPartner              # Skyrim alone
    scripts/skytest.ps1 status
    scripts/skytest.ps1 stop
#>
$kit = Join-Path (Split-Path -Parent $PSScriptRoot) 'skse/extern/skse-test-kit/scripts/skytest.ps1'
if (-not (Test-Path $kit)) {
    throw 'skse-test-kit is not checked out: git submodule update --init --checkout skse/extern/skse-test-kit (then scripts/build.ps1).'
}
if ($args -contains '-Mod') { & $kit @args } else { & $kit -Mod (Split-Path -Parent $PSScriptRoot) @args }
