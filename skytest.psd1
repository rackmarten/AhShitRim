# Test sessions with skse-test-kit (scripts/skytest.ps1, docs/DEV_SETUP.md "Test sessions"). Paths come from
# local.props: SkyrimPath, and GamesLock for the folder every session on this machine locks the games with.
@{
    Name    = 'AhShitRim'
    Ini     = 'Data/SKSE/Plugins/AhShitRim.ini'
    # Copies what scripts/build.ps1 built last (both plugins); skytest starts nothing until it's done.
    Deploy  = @{ Script = 'scripts/deploy-local.ps1' }
    # What a build changes: skytest session starts again when these differ from the running session's.
    Build   = @('skse/build/release/AhShitRim.dll', 'sa/build/Release/AhShitRim.asi')
    # San Andreas, hidden as scripts/restart-games.ps1 starts it, beside Skyrim. CJ=1 only while it runs: the session
    # is ready once San Andreas has the player.
    Partner = @{ Script = 'scripts/launch-sa.ps1'; Processes = @('gta_sa'); Test = @{ CJ = '1' } }
    # A new character in Riverwood, with CJ there once San Andreas has the player.
    Start   = 'coc:Riverwood'
}
