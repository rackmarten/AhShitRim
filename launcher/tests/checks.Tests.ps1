# Pester 5 tests for launcher/checks.ps1, run by scripts/test.ps1.

BeforeAll {
    . (Join-Path $PSScriptRoot '..\checks.ps1')
    $readyPlugins = @('versionlib-1-6-1170-0.bin', 'AhShitRim.dll')
    $readyGame = @('SkyrimSE.exe', 'skse64_loader.exe', 'skse64_1_6_1170.dll')
}

Describe 'Get-SaExeVersion' {
    It 'knows every 1.0 US exe as supported' {
        foreach ($md5 in '170b3a9108687b26da2d8901c6948a18', '2b5066bd4097ac2944ce6a9cf8fe5677', '4e99d762f44b1d5e7652dfa7e73d6b6f', '667f799c4ba8c9e1054fccaea6d4259b') {
            (Get-SaExeVersion $md5).Supported | Should -BeTrue
        }
    }
    It 'reads Get-FileHash''s upper case' {
        (Get-SaExeVersion '170B3A9108687B26DA2D8901C6948A18').Name | Should -Be '1.0 US (HOODLUM)'
    }
    It 'knows the Steam, 1.01 and EU exes as unsupported' {
        foreach ($md5 in '5bfd4dd83989a8264de4b8e771f237fd', '9f2d711dbf1fbbcda5ff9418a2cc1ef5', '6c6160da9b175b66cf9127c86be57bf7') {
            (Get-SaExeVersion $md5).Supported | Should -BeFalse
        }
    }
    It 'returns nothing for an unknown or missing hash' {
        Get-SaExeVersion '00000000000000000000000000000000' | Should -BeNullOrEmpty
        Get-SaExeVersion $null | Should -BeNullOrEmpty
    }
}

Describe 'Get-SkyrimRuntime' {
    It 'names the SKSE and Address Library files for an AE version' {
        $runtime = Get-SkyrimRuntime '1.6.1170.0'
        $runtime.Version | Should -Be ([version] '1.6.1170')
        $runtime.IsAnniversary | Should -BeTrue
        $runtime.SkseDll | Should -Be 'skse64_1_6_1170.dll'
        $runtime.AddressLibrary | Should -Be 'versionlib-1-6-1170-0.bin'
    }
    It 'takes comma separated versions' {
        (Get-SkyrimRuntime '1, 6, 640, 0').Version | Should -Be ([version] '1.6.640')
    }
    It 'counts 1.6.317 as the first AE runtime and 1.5.97 as SE' {
        (Get-SkyrimRuntime '1.6.317.0').IsAnniversary | Should -BeTrue
        (Get-SkyrimRuntime '1.5.97.0').IsAnniversary | Should -BeFalse
    }
    It 'returns nothing for an unreadable version' {
        Get-SkyrimRuntime '' | Should -BeNullOrEmpty
        Get-SkyrimRuntime 'unknown' | Should -BeNullOrEmpty
    }
}

Describe 'Get-SkyrimProblems' {
    It 'is happy with a ready install' {
        Get-SkyrimProblems '1.6.1170.0' $readyGame $readyPlugins | Should -BeNullOrEmpty
    }
    It 'matches file names whatever their case' {
        Get-SkyrimProblems '1.6.1170.0' @('SKSE64_Loader.exe', 'SKSE64_1_6_1170.DLL') @('VersionLib-1-6-1170-0.bin', 'ahshitrim.dll') | Should -BeNullOrEmpty
    }
    It 'stops the Special Edition runtime' {
        $problems = @(Get-SkyrimProblems '1.5.97.0' @('skse64_loader.exe', 'skse64_1_5_97.dll') @('versionlib-1-5-97-0.bin', 'AhShitRim.dll'))
        $problems | Should -HaveCount 1
        $problems[0] | Should -BeLike '*Special Edition runtime*'
    }
    It 'asks for SKSE without the loader' {
        $problems = @(Get-SkyrimProblems '1.6.1170.0' @('SkyrimSE.exe') $readyPlugins)
        $problems | Should -HaveCount 1
        $problems[0] | Should -BeLike 'SKSE64 is not installed*skse.silverlock.org*'
    }
    It 'names an SKSE built for another Skyrim version' {
        $problems = @(Get-SkyrimProblems '1.6.1170.0' @('skse64_loader.exe', 'skse64_1_6_640.dll') $readyPlugins)
        $problems | Should -HaveCount 1
        $problems[0] | Should -BeLike '*another Skyrim version (found skse64_1_6_640.dll)*skse64_1_6_1170.dll*'
    }
    It 'asks for the Address Library file of this version' {
        $problems = @(Get-SkyrimProblems '1.6.1170.0' $readyGame @('versionlib-1-6-640-0.bin', 'AhShitRim.dll'))
        $problems | Should -HaveCount 1
        $problems[0] | Should -BeLike '*Address Library*versionlib-1-6-1170-0.bin*nexusmods.com*'
    }
    It 'asks for the Skyrim part of AhShitRim' {
        $problems = @(Get-SkyrimProblems '1.6.1170.0' $readyGame @('versionlib-1-6-1170-0.bin'))
        $problems | Should -HaveCount 1
        $problems[0] | Should -BeLike '*AhShitRim.dll*mod manager*INSTALL.md*'
    }
    It 'lists every problem at once' {
        Get-SkyrimProblems '1.6.1170.0' @() @() | Should -HaveCount 3
    }
    It 'says when it cannot read the version' {
        Get-SkyrimProblems '' $readyGame $readyPlugins | Should -BeLike "Can't read SkyrimSE.exe's version*"
    }
}

Describe 'Get-AsiLoader' {
    It "finds Silent's ASI Loader by vorbisHooked.dll" {
        Get-AsiLoader @(@{ Name = 'vorbisFile.dll'; Description = '' }, @{ Name = 'vorbisHooked.dll'; Description = 'Vorbis' }) | Should -Be "Silent's ASI Loader"
    }
    It 'finds the Ultimate ASI Loader by its description' {
        Get-AsiLoader @(@{ Name = 'dinput8.dll'; Description = 'Ultimate ASI Loader' }) | Should -Be 'Ultimate ASI Loader (dinput8.dll)'
    }
    It 'does not take any dinput8.dll for a loader' {
        Get-AsiLoader @(@{ Name = 'dinput8.dll'; Description = 'Microsoft DirectInput' }) | Should -BeNullOrEmpty
        Get-AsiLoader @() | Should -BeNullOrEmpty
    }
}

Describe 'Get-SaProblems' {
    It 'is happy with 1.0 US and a loader' {
        $result = Get-SaProblems '170b3a9108687b26da2d8901c6948a18' @('gta_sa.exe') "Silent's ASI Loader"
        $result.Errors | Should -BeNullOrEmpty
        $result.Warnings | Should -BeNullOrEmpty
    }
    It 'stops the Steam exe with a downgrade link' {
        $result = Get-SaProblems '5bfd4dd83989a8264de4b8e771f237fd' @('gta_sa.exe') "Silent's ASI Loader"
        $result.Errors | Should -HaveCount 1
        $result.Errors[0] | Should -BeLike '*the current Steam version*1.0 US only*gtasa-open-downgrader*'
    }
    It 'tells a folder with only gta-sa.exe to downgrade' {
        $result = Get-SaProblems $null @('gta-sa.exe') "Silent's ASI Loader"
        $result.Errors[0] | Should -BeLike '*only the Steam exe (gta-sa.exe)*Downgrade*'
    }
    It 'says when gta_sa.exe is missing' {
        (Get-SaProblems $null @() "Silent's ASI Loader").Errors[0] | Should -BeLike 'gta_sa.exe is not in*'
    }
    It 'only warns about an unknown exe' {
        $result = Get-SaProblems '0123456789abcdef0123456789abcdef' @('gta_sa.exe') "Silent's ASI Loader"
        $result.Errors | Should -BeNullOrEmpty
        $result.Warnings[0] | Should -BeLike '*0123456789abcdef0123456789abcdef*'
    }
    It 'asks for an ASI loader' {
        $result = Get-SaProblems '170b3a9108687b26da2d8901c6948a18' @('gta_sa.exe') $null
        $result.Errors | Should -HaveCount 1
        $result.Errors[0] | Should -BeLike '*no ASI loader*'
    }
}

Describe 'Read-Mo2Ini' {
    It 'reads the game path and the default mods folder' {
        $ini = "[General]`r`ngamePath=@ByteArray(C:\\Steam\\steamapps\\common\\Skyrim Special Edition)`r`n"
        $mo2 = Read-Mo2Ini $ini 'C:\Users\Player\AppData\Local\ModOrganizer\Skyrim Special Edition'
        $mo2.GamePath | Should -Be 'C:\Steam\steamapps\common\Skyrim Special Edition'
        $mo2.ModsDir | Should -Be 'C:\Users\Player\AppData\Local\ModOrganizer\Skyrim Special Edition\mods'
    }
    It 'follows base_directory and mod_directory' {
        $ini = "[General]`ngamePath=@ByteArray(D:\\Skyrim)`n[Settings]`nbase_directory=E:/MO2`nmod_directory=%BASE_DIR%/my mods`n"
        (Read-Mo2Ini $ini 'C:\instance').ModsDir | Should -Be 'E:\MO2\my mods'
    }
}

Describe 'Test-SamePath' {
    It 'ignores slashes, case and a trailing separator' {
        Test-SamePath 'C:/Games/Skyrim/' 'c:\games\skyrim' | Should -BeTrue
        Test-SamePath 'C:\Games\Skyrim' 'C:\Games\Skyrim2' | Should -BeFalse
        Test-SamePath $null 'C:\Games' | Should -BeFalse
    }
}
