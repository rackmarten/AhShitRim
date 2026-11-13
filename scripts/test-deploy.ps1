<#
.SYNOPSIS
    A test session's deploy and undeploy (skytest.psd1's Deploy and Undeploy; docs/DEV_SETUP.md, "Test sessions").
    "deploy" first keeps a copy of every file scripts/deploy-local.ps1 overwrites in the games (the player's own
    AhShitRim: the SKSE plugin, its .pdb and AhShitRim.ini, Sweetroll FM's tracks, the San Andreas ASI and its .pdb),
    notes the ones that aren't there, and moves the other mods named after -Aside out of the way (each one's SKSE plugin
    and its .pdb: two mods that both take over the player can't run at once), then runs deploy-local.ps1 and notes what
    it wrote. "restore" puts all of it back as it was, deletes what wasn't there and the folders the deploy made. The
    copies and their list are kept in %LOCALAPPDATA%\AhShitRim\test-session until a restore; a second deploy before it
    (a session restarted for a new build) keeps the first ones, so the player's own files are what comes back. Both
    refuse when the games' files changed since the session deployed (a session never stopped, and a new AhShitRim
    installed meanwhile): "restore-anyway" puts the old ones back all the same, "forget" drops the copies and leaves the
    games as they are. Run it with both games closed.
.EXAMPLE
    scripts/test-deploy.ps1 deploy -Aside PerkeleRim   # the build into the games, PerkeleRim's plugin aside
    scripts/test-deploy.ps1 restore                    # everything as before
    scripts/test-deploy.ps1 status
    scripts/test-deploy.ps1 forget                     # a kept session that's out of date: drop it, change nothing
#>
param(
    [ValidateSet('deploy', 'restore', 'restore-anyway', 'forget', 'status')] [string] $Mode = 'status',
    # Other mods whose SKSE plugin (<Name>.dll, <Name>.pdb in Data/SKSE/Plugins) stays out of the game meanwhile.
    [string[]] $Aside = @(),
    # deploy: this SKSE plugin instead of this checkout's build (scripts/load-test.ps1 -Dll), passed to deploy-local.ps1.
    [string] $SkseDll = ''
)

. "$PSScriptRoot/common.ps1"
. "$PSScriptRoot/test-session.ps1"
$config = Get-LocalConfig
$skyrim = Get-RequiredSetting $config 'SkyrimPath'
$sa = Get-RequiredSetting $config 'SaPath'
$store = Join-Path $env:LOCALAPPDATA 'AhShitRim\test-session'

# What deploy-local.ps1 writes (keep this in step with it).
$plugins = Join-Path $skyrim 'Data\SKSE\Plugins'
$music = Join-Path $skyrim 'Data\Music\AhShitRim'
$station = Join-Path $music 'SweetrollFM'
$tracks = @(Get-ChildItem -ErrorAction SilentlyContinue (Join-Path $RepoRoot 'music/sweetrollfm/*.xwm') | ForEach-Object { Join-Path $station $_.Name })
$ours = @(
    (Join-Path $plugins 'AhShitRim.dll'),
    (Join-Path $plugins 'AhShitRim.pdb'),
    # The mod's ini: skytest keeps and restores it too, but to what the deploy wrote (deploy-local.ps1 rewrites it,
    # dropping keys it doesn't know); the player's own comes back from here.
    (Join-Path $plugins 'AhShitRim.ini'),
    (Join-Path $sa 'scripts\AhShitRim.asi'),
    (Join-Path $sa 'scripts\AhShitRim.pdb')) + $(if ($tracks) { @($tracks) + (Join-Path $station 'CREDITS.md') })
$others = @($Aside | Where-Object { $_ } | ForEach-Object { (Join-Path $plugins "$_.dll"), (Join-Path $plugins "$_.pdb") })

switch ($Mode) {
    'deploy' {
        if (Save-SessionFiles $store -Keep $ours -Move $others -Dirs @($music, $station)) {
            Write-Host "Kept the games' own files in $store$(if ($others) { "; set aside $(@($Aside) -join ', ')" })."
        } else {
            Write-Host "The games' own files from before this session are still kept in $store."
        }
        $deployArguments = @{}
        if ($SkseDll) { $deployArguments['SkseDll'] = $SkseDll }
        & "$PSScriptRoot/deploy-local.ps1" @deployArguments
        Save-DeployStamps $store
    }
    { $_ -in 'restore', 'restore-anyway' } {
        $back = Restore-SessionFiles $store -Force:($Mode -eq 'restore-anyway')
        if ($back) { Write-Host "Put back the games' own files ($(@($back).Count))." } else { Write-Host 'Nothing kept to put back.' }
    }
    'forget' {
        if (Test-Path -LiteralPath $store) {
            Remove-Item -LiteralPath $store -Recurse -Force
            Write-Host "Dropped the kept files in $store; the games stay as they are."
        } else {
            Write-Host 'Nothing kept.'
        }
    }
    'status' {
        $list = Read-SessionList $store
        if (-not $list) { Write-Host 'No test session files kept.'; return }
        $changed = @(Get-ChangedSinceDeploy $store)
        if ($changed) { Write-Host "Changed since the session deployed: $($changed -join ', ')" -ForegroundColor Yellow }
        $list | ForEach-Object { [pscustomobject]@{ Path = $_.path; Was = $(if ($_.moved) { 'set aside' } elseif ($_.absent) { 'not there' } else { 'kept' }) } } | Format-Table -AutoSize
    }
}
