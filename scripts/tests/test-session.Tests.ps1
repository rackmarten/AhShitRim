# Pester 5 tests for scripts/test-session.ps1 (test sessions' file helpers), run by scripts/test.ps1.

BeforeAll {
    . (Join-Path $PSScriptRoot '..\test-session.ps1')
}

Describe 'Save-SessionFiles and Restore-SessionFiles' {
    BeforeEach {
        $game = Join-Path $TestDrive ([guid]::NewGuid())
        $store = Join-Path $game 'store'
        New-Item -ItemType Directory -Force (Join-Path $game 'Plugins') | Out-Null
        $ours = Join-Path $game 'Plugins\AhShitRim.dll'
        $absent = Join-Path $game 'Plugins\AhShitRim.pdb'
        $other = Join-Path $game 'Plugins\PerkeleRim.dll'
        $missingOther = Join-Path $game 'Plugins\PerkeleRim.pdb'
        [IO.File]::WriteAllText($ours, 'the player''s own')
        [IO.File]::WriteAllText($other, 'another mod')
    }

    It 'keeps, notes and sets aside, then puts everything back as it was' {
        Save-SessionFiles $store -Keep @($ours, $absent) -Move @($other, $missingOther) | Should -BeTrue
        Test-Path $other | Should -BeFalse  # set aside
        Test-Path $ours | Should -BeTrue    # only copied
        # The deploy writes over ours and adds the one that wasn't there.
        [IO.File]::WriteAllText($ours, 'the test build')
        [IO.File]::WriteAllText($absent, 'symbols')
        $back = Restore-SessionFiles $store
        $back.Count | Should -Be 3
        [IO.File]::ReadAllText($ours) | Should -Be 'the player''s own'
        Test-Path $absent | Should -BeFalse
        [IO.File]::ReadAllText($other) | Should -Be 'another mod'
        Test-Path $missingOther | Should -BeFalse
        Test-Path $store | Should -BeFalse
    }

    It 'keeps the first copies through a second deploy' {
        Save-SessionFiles $store -Keep @($ours) | Should -BeTrue
        [IO.File]::WriteAllText($ours, 'the test build')
        Save-SessionFiles $store -Keep @($ours) | Should -BeFalse
        [IO.File]::WriteAllText($ours, 'a newer test build')
        [void] (Restore-SessionFiles $store)
        [IO.File]::ReadAllText($ours) | Should -Be 'the player''s own'
    }

    It 'refuses to restore over files that changed since the deploy, unless forced' {
        Save-SessionFiles $store -Keep @($ours) | Should -BeTrue
        [IO.File]::WriteAllText($ours, 'the test build')
        Save-DeployStamps $store
        [IO.File]::WriteAllText($ours, 'a newer release the player installed')
        { Restore-SessionFiles $store } | Should -Throw '*changed since*'
        [IO.File]::ReadAllText($ours) | Should -Be 'a newer release the player installed'
        { Save-SessionFiles $store -Keep @($ours) } | Should -Throw '*changed since*'
        [void] (Restore-SessionFiles $store -Force)
        [IO.File]::ReadAllText($ours) | Should -Be 'the player''s own'
    }

    It 'goes on with a restore that stopped halfway' {
        Save-SessionFiles $store -Keep @($ours, $absent) | Should -BeTrue
        [IO.File]::WriteAllText($ours, 'the test build')
        [IO.File]::WriteAllText($absent, 'symbols')
        Save-DeployStamps $store
        # The first try put ours back and deleted the other before a locked file stopped it.
        [IO.File]::WriteAllText($ours, 'the player''s own')
        Remove-Item $absent
        @(Get-ChangedSinceDeploy $store).Count | Should -Be 0
        [void] (Restore-SessionFiles $store)
        [IO.File]::ReadAllText($ours) | Should -Be 'the player''s own'
        Test-Path $store | Should -BeFalse
    }

    It 'lets a deploy that stopped halfway try again, and restores after it' {
        Save-SessionFiles $store -Keep @($ours, $absent) | Should -BeTrue
        [IO.File]::WriteAllText($ours, 'build 1')
        Save-DeployStamps $store
        # A restart for build 2: its deploy writes ours, then stops on a locked file.
        Save-SessionFiles $store -Keep @($ours, $absent) | Should -BeFalse
        Set-DeployInFlight $store $true
        [IO.File]::WriteAllText($ours, 'build 2')
        { Save-SessionFiles $store -Keep @($ours, $absent) } | Should -Not -Throw
        @(Get-ChangedSinceDeploy $store).Count | Should -Be 0
        [void] (Restore-SessionFiles $store)
        [IO.File]::ReadAllText($ours) | Should -Be 'the player''s own'
    }

    It 'takes away a file it set aside whose delete failed' {
        Save-SessionFiles $store -Keep @($ours) -Move @($other) | Should -BeTrue
        [IO.File]::WriteAllText($other, 'another mod')  # the same file, still there: its delete failed
        Save-SessionFiles $store -Keep @($ours) -Move @($other) | Should -BeFalse
        Test-Path $other | Should -BeFalse
    }

    It 'refuses to go on when a file it set aside is back' {
        Save-SessionFiles $store -Keep @($ours) -Move @($other) | Should -BeTrue
        [IO.File]::WriteAllText($other, 'put back by someone')
        { Save-SessionFiles $store -Keep @($ours) -Move @($other) } | Should -Throw '*is back*'
    }

    It 'sets aside a file that turns up only by the second deploy' {
        Remove-Item $other
        Save-SessionFiles $store -Keep @($ours) -Move @($other) | Should -BeTrue
        [IO.File]::WriteAllText($other, 'deployed meanwhile')
        Save-SessionFiles $store -Keep @($ours) -Move @($other) | Should -BeFalse
        Test-Path $other | Should -BeFalse
        [void] (Restore-SessionFiles $store)
        [IO.File]::ReadAllText($other) | Should -Be 'deployed meanwhile'
    }

    It 'removes the folders the deploy made, once empty, and keeps ones that were there or still hold something' {
        $music = Join-Path $game 'Music\AhShitRim'
        $station = Join-Path $music 'SweetrollFM'
        $track = Join-Path $station 'track.xwm'
        $existing = Join-Path $game 'Plugins'
        Save-SessionFiles $store -Keep @($track) -Dirs @($music, $station, $existing) | Should -BeTrue
        New-Item -ItemType Directory -Force $station | Out-Null
        [IO.File]::WriteAllText($track, 'a track')
        Save-SessionFiles $store -Keep @($track) -Dirs @($music, $station, $existing) | Should -BeFalse  # a second deploy: the first list stands
        [void] (Restore-SessionFiles $store)
        Test-Path $music | Should -BeFalse
        Test-Path $existing | Should -BeTrue

        Save-SessionFiles $store -Keep @($track) -Dirs @($music, $station) | Should -BeTrue
        New-Item -ItemType Directory -Force $station | Out-Null
        [IO.File]::WriteAllText((Join-Path $station 'someone-elses.xwm'), 'not the deploy''s')
        [void] (Restore-SessionFiles $store)
        Test-Path (Join-Path $station 'someone-elses.xwm') | Should -BeTrue
    }

    It 'has nothing to put back without a list' {
        @(Restore-SessionFiles $store).Count | Should -Be 0
        Read-SessionList $store | Should -BeNullOrEmpty
    }
}
