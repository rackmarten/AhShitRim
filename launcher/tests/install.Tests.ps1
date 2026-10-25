# Pester 5 tests for launcher/install.ps1, run by scripts/test.ps1. Everything happens in TestDrive.

BeforeAll {
    . (Join-Path $PSScriptRoot '..\install.ps1')
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    function New-TestZip([string] $Zip, [hashtable] $Files) {
        $stage = Join-Path ([IO.Path]::GetDirectoryName($Zip)) ([IO.Path]::GetFileNameWithoutExtension($Zip) + '-stage')
        foreach ($name in $Files.Keys) {
            $path = Join-Path $stage $name
            New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
            Set-Content -LiteralPath $path -Value $Files[$name] -NoNewline
        }
        [IO.Compression.ZipFile]::CreateFromDirectory($stage, $Zip)
        Remove-Item -Recurse -Force $stage
        return $Zip
    }
    # A zip with entries named as given, '..' and all, which CreateFromDirectory can't make.
    function New-RawZip([string] $Zip, [string[]] $Names) {
        $archive = [IO.Compression.ZipFile]::Open($Zip, 'Create')
        try { foreach ($name in $Names) { $writer = New-Object IO.StreamWriter ($archive.CreateEntry($name).Open()); $writer.Write($name); $writer.Dispose() } }
        finally { $archive.Dispose() }
        return $Zip
    }
}

Describe 'PinnedDownloads' {
    It 'pins an official GitHub release URL and a SHA-256 for each' {
        foreach ($download in $PinnedDownloads.Values) {
            $download.Url | Should -Match '^https://github\.com/[^/]+/[^/]+/releases/download/[^/]+/'
            $download.Sha256 | Should -Match '^[0-9a-f]{64}$'
            $download.Url | Should -BeLike "*/$($download.File)"
        }
    }
}

Describe 'Get-FolderFromRegistryValue' {
    It 'takes the folder of a quoted exe path' {
        Get-FolderFromRegistryValue '"C:\Games\San Andreas\gta_sa.exe"' | Should -Be 'C:\Games\San Andreas'
    }
    It 'keeps a folder, without its trailing separator' {
        Get-FolderFromRegistryValue 'D:\Steam\steamapps\common\Grand Theft Auto San Andreas\' | Should -Be 'D:\Steam\steamapps\common\Grand Theft Auto San Andreas'
    }
    It 'returns nothing for an empty value' {
        Get-FolderFromRegistryValue '' | Should -BeNullOrEmpty
    }
}

Describe 'A Steam library on a drive that is gone' {
    BeforeAll {
        # A drive letter this PC doesn't have, as when a library's disk was removed.
        $gone = [char[]] 'QRSTUVWXYZJKLMNOP' | Where-Object { -not (Test-Path "$($_):\") } | Select-Object -First 1
        $steam = New-Item -ItemType Directory "$TestDrive\Steam"
        New-Item -ItemType Directory "$steam\steamapps" | Out-Null
        Set-Content "$steam\steamapps\libraryfolders.vdf" @"
"libraryfolders"
{
    "0" { "path" "$("$steam" -replace '\\', '\\')" }
    "1" { "path" "$($gone):\\SteamLibrary" }
}
"@
    }
    It 'lists it without throwing' {
        $libraries = @(Get-SteamLibraries "$steam")
        $libraries | Should -Contain "$($gone):\SteamLibrary"
    }
    It 'guesses San Andreas there without throwing, and Select-SaInstalls drops it' {
        $guesses = @(Get-SaFolderGuesses (Get-SteamLibraries "$steam"))
        $guesses | Should -Contain "$($gone):\SteamLibrary\steamapps\common\Grand Theft Auto San Andreas"
        @(Select-SaInstalls $guesses | Where-Object { $_.Path -like "$($gone):*" }).Count | Should -Be 0
    }
    It 'takes registry values and paths on it without throwing' {
        Get-FolderFromRegistryValue "`"$($gone):\Games\SA\gta_sa.exe`"" | Should -Be "$($gone):\Games\SA"
        Join-Guess "$($gone):\Games" 'gta_sa.exe' | Should -Be "$($gone):\Games\gta_sa.exe"
        Join-Guess $null 'gta_sa.exe' | Should -BeNullOrEmpty
        @(Select-SaInstalls @("$($gone):\Games\SA", 'bad|name')).Count | Should -Be 0
    }
}

Describe 'Select-SaInstalls' {
    BeforeAll {
        $steam = New-Item -ItemType Directory (Join-Path $TestDrive 'steam')
        New-Item -ItemType File (Join-Path $steam 'gta-sa.exe') | Out-Null
        $copy = New-Item -ItemType Directory (Join-Path $TestDrive 'copy')
        New-Item -ItemType File (Join-Path $copy 'gta_sa.exe') | Out-Null
        New-Item -ItemType File (Join-Path $copy 'gta-sa.exe') | Out-Null
        New-Item -ItemType Directory (Join-Path $TestDrive 'empty') | Out-Null
    }
    It 'keeps folders with either exe, once each, and prefers gta_sa.exe' {
        $found = @(Select-SaInstalls @("$TestDrive\steam", "$TestDrive\copy", "$TestDrive\copy\", "$TestDrive\empty", "$TestDrive\missing", $null))
        $found.Count | Should -Be 2
        $found[0].Exe | Should -Be 'gta-sa.exe'
        $found[1].Exe | Should -Be 'gta_sa.exe'
    }
}

Describe 'Get-SaCopyDestination' {
    It 'uses a new or empty folder as it is' {
        Get-SaCopyDestination "$TestDrive\new" | Should -Be "$TestDrive\new"
        New-Item -ItemType Directory "$TestDrive\blank" | Out-Null
        Get-SaCopyDestination "$TestDrive\blank\" | Should -Be "$TestDrive\blank"
    }
    It 'makes a folder of its own in a drive root' {
        Get-SaCopyDestination 'D:\' | Should -Be 'D:\San Andreas (AhShitRim)'
        Get-SaCopyDestination 'D:' | Should -Be 'D:\San Andreas (AhShitRim)'
    }
    It 'makes a folder of its own inside one with files' {
        New-Item -ItemType File -Force "$TestDrive\games\other.txt" | Out-Null
        Get-SaCopyDestination "$TestDrive\games" | Should -Be "$TestDrive\games\San Andreas (AhShitRim)"
    }
}

Describe 'Get-SaCopyProblem' {
    It 'accepts an empty folder elsewhere with room' {
        Get-SaCopyProblem 'C:\Steam\SA' "$TestDrive\copy-ok" 1GB 10GB | Should -BeNullOrEmpty
    }
    It 'refuses a folder inside the original, or the other way round' {
        Get-SaCopyProblem 'C:\Steam\SA' 'C:\Steam\SA\copy' 1GB 10GB | Should -BeLike '*inside*'
        Get-SaCopyProblem 'C:\Steam\SA' 'C:\Steam' 1GB 10GB | Should -BeLike '*inside*'
    }
    It 'does not mistake a sibling with the same prefix for the original' {
        Get-SaCopyProblem 'C:\Steam\SA' "$TestDrive\SA2" 1GB 10GB | Should -BeNullOrEmpty
    }
    It 'refuses Program Files' {
        Get-SaCopyProblem 'D:\SA' (Join-Path ${env:ProgramFiles(x86)} 'SA copy') 1GB 10GB | Should -BeLike '*Program Files*'
    }
    It 'refuses a folder with files and a drive without room' {
        New-Item -ItemType File -Force "$TestDrive\full\x.txt" | Out-Null
        Get-SaCopyProblem 'C:\Steam\SA' "$TestDrive\full" 1GB 10GB | Should -BeLike '*already has files*'
        Get-SaCopyProblem 'C:\Steam\SA' "$TestDrive\roomless" 5GB 4GB | Should -BeLike '*free*'
    }
}

Describe 'Copy-GameFolder' {
    It 'copies every file and folder' {
        New-Item -ItemType File -Force "$TestDrive\src\gta-sa.exe" -Value 'exe' | Out-Null
        New-Item -ItemType File -Force "$TestDrive\src\models\gta3.img" -Value 'img' | Out-Null
        New-Item -ItemType Directory -Force "$TestDrive\src\empty" | Out-Null
        Copy-GameFolder "$TestDrive\src" "$TestDrive\dst"
        Get-Content "$TestDrive\dst\models\gta3.img" | Should -Be 'img'
        Get-Content "$TestDrive\dst\gta-sa.exe" | Should -Be 'exe'
        Test-Path "$TestDrive\dst\empty" | Should -BeTrue
    }
}

Describe 'Save-VerifiedDownload' {
    BeforeAll {
        $served = Join-Path $TestDrive 'served\tool.exe'
        New-Item -ItemType File -Force $served -Value 'the real tool' | Out-Null
        $hash = (Get-FileHash $served -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    It 'downloads a file whose SHA-256 matches' {
        $download = [pscustomobject] @{ Url = ([Uri] $served).AbsoluteUri; Sha256 = $hash; File = 'tool.exe' }
        $path = Save-VerifiedDownload $download "$TestDrive\tools-good"
        Get-Content $path | Should -Be 'the real tool'
    }
    It 'deletes a file whose SHA-256 is wrong, and throws' {
        $download = [pscustomobject] @{ Url = ([Uri] $served).AbsoluteUri; Sha256 = '0' * 64; File = 'tool.exe' }
        { Save-VerifiedDownload $download "$TestDrive\tools-bad" } | Should -Throw '*SHA-256*'
        @(Get-ChildItem "$TestDrive\tools-bad").Count | Should -Be 0
    }
    It 'reuses a verified copy without downloading' {
        New-Item -ItemType File -Force "$TestDrive\tools-kept\tool.exe" -Value 'the real tool' | Out-Null
        $download = [pscustomobject] @{ Url = 'https://invalid.invalid/tool.exe'; Sha256 = $hash; File = 'tool.exe' }
        Save-VerifiedDownload $download "$TestDrive\tools-kept" | Should -Be "$TestDrive\tools-kept\tool.exe"
    }
}

Describe 'Install-AsiLoader' {
    It 'puts dinput8.dll in San Andreas and makes the scripts folder' {
        $zip = New-TestZip "$TestDrive\ual.zip" @{ 'dinput8.dll' = 'loader' }
        New-Item -ItemType Directory "$TestDrive\sa" | Out-Null
        Install-AsiLoader $zip "$TestDrive\sa"
        Get-Content "$TestDrive\sa\dinput8.dll" | Should -Be 'loader'
        Test-Path "$TestDrive\sa\scripts" | Should -BeTrue
    }
    It 'takes only the dinput8.dll at the zip''s root, and keeps one that was there as .bak' {
        $zip = New-RawZip "$TestDrive\ual2.zip" @('x64/dinput8.dll', 'dinput8.dll')
        New-Item -ItemType File -Force "$TestDrive\sa2\dinput8.dll" -Value 'another mod' | Out-Null
        Install-AsiLoader $zip "$TestDrive\sa2"
        Get-Content "$TestDrive\sa2\dinput8.dll" | Should -Be 'dinput8.dll'
        Get-Content "$TestDrive\sa2\dinput8.dll.bak" | Should -Be 'another mod'
    }
}

Describe 'Test-DataRelativePath' {
    It 'takes paths inside Data only' {
        Test-DataRelativePath 'SKSE/Plugins/AhShitRim.dll' | Should -BeTrue
        Test-DataRelativePath 'Music\AhShitRim\a..b.xwm' | Should -BeTrue
        foreach ($bad in '..\x.dll', 'SKSE/../../x.dll', 'C:\Windows\x.dll', '\x.dll', 'C:x.dll', '') {
            Test-DataRelativePath $bad | Should -BeFalse
        }
    }
}

Describe 'Install-SkyrimPart and Uninstall-SkyrimPart' {
    BeforeEach {
        $data = Join-Path $TestDrive "data-$([guid]::NewGuid())"
        New-Item -ItemType File -Force "$data\SKSE\Plugins\versionlib-1-6-1170-0.bin" -Value 'address library' | Out-Null
        $zip1 = New-TestZip "$TestDrive\AhShitRim-Skyrim-0.1.0-$([guid]::NewGuid()).zip" @{
            'SKSE\Plugins\AhShitRim.dll' = 'dll 1'; 'SKSE\Plugins\AhShitRim.pdb' = 'pdb'; 'Music\AhShitRim\SweetrollFM\track.xwm' = 'music'
        }
    }
    It 'copies the zip into Data and records it as current' {
        $files = Install-SkyrimPart $zip1 $data
        $files.Count | Should -Be 3
        Get-Content "$data\SKSE\Plugins\AhShitRim.dll" | Should -Be 'dll 1'
        Get-Content "$data\Music\AhShitRim\SweetrollFM\track.xwm" | Should -Be 'music'
        Test-SkyrimPartCurrent $zip1 $data | Should -BeTrue
    }
    It 'notices a changed or missing file, and another zip' {
        Install-SkyrimPart $zip1 $data | Out-Null
        Set-Content "$data\SKSE\Plugins\AhShitRim.dll" 'tampered'
        Test-SkyrimPartCurrent $zip1 $data | Should -BeFalse
        Install-SkyrimPart $zip1 $data | Out-Null
        Remove-Item "$data\Music\AhShitRim\SweetrollFM\track.xwm"
        Test-SkyrimPartCurrent $zip1 $data | Should -BeFalse
        $zip2 = New-TestZip "$TestDrive\AhShitRim-Skyrim-0.2.0-$([guid]::NewGuid()).zip" @{ 'SKSE\Plugins\AhShitRim.dll' = 'dll 2' }
        Test-SkyrimPartCurrent $zip2 $data | Should -BeFalse
    }
    It 'is not current without a record' {
        Test-SkyrimPartCurrent $zip1 $data | Should -BeFalse
    }
    It 'drops the files a newer zip no longer has' {
        Install-SkyrimPart $zip1 $data | Out-Null
        $zip2 = New-TestZip "$TestDrive\AhShitRim-Skyrim-0.2.0-$([guid]::NewGuid()).zip" @{ 'SKSE\Plugins\AhShitRim.dll' = 'dll 2' }
        Install-SkyrimPart $zip2 $data | Out-Null
        Get-Content "$data\SKSE\Plugins\AhShitRim.dll" | Should -Be 'dll 2'
        Test-Path "$data\SKSE\Plugins\AhShitRim.pdb" | Should -BeFalse
        Test-Path "$data\Music\AhShitRim\SweetrollFM\track.xwm" | Should -BeFalse
        Test-SkyrimPartCurrent $zip2 $data | Should -BeTrue
    }
    It 'uninstalls every file and the folders it made, and leaves everything else' {
        Install-SkyrimPart $zip1 $data | Out-Null
        New-Item -ItemType File "$data\SKSE\Plugins\AhShitRim.ini" -Value 'launcher ini' | Out-Null
        $removed = Uninstall-SkyrimPart $data @('SKSE\Plugins\AhShitRim.ini')
        $removed.Count | Should -Be 4
        Test-Path "$data\SKSE\Plugins\AhShitRim.dll" | Should -BeFalse
        Test-Path "$data\SKSE\Plugins\AhShitRim.ini" | Should -BeFalse
        Test-Path "$data\$SkyrimPartRecord" | Should -BeFalse
        Test-Path "$data\Music" | Should -BeFalse
        Get-Content "$data\SKSE\Plugins\versionlib-1-6-1170-0.bin" | Should -Be 'address library'
    }
    It 'keeps a folder it made when something else is in it now' {
        Install-SkyrimPart $zip1 $data | Out-Null
        New-Item -ItemType File "$data\Music\AhShitRim\mine.xwm" -Value 'player''s' | Out-Null
        Uninstall-SkyrimPart $data | Out-Null
        Test-Path "$data\Music\AhShitRim\mine.xwm" | Should -BeTrue
        Test-Path "$data\Music\AhShitRim\SweetrollFM" | Should -BeFalse
    }
    It 'refuses a zip with a file outside Data, before copying anything' {
        $zip = New-RawZip "$TestDrive\evil-$([guid]::NewGuid()).zip" @('SKSE/Plugins/AhShitRim.dll', '../../evil.txt')
        { Install-SkyrimPart $zip $data } | Should -Throw '*outside*'
        Test-Path "$data\SKSE\Plugins\AhShitRim.dll" | Should -BeFalse
        Test-Path "$data\$SkyrimPartRecord" | Should -BeFalse
    }
    It 'ignores record lines that point outside Data' {
        New-Item -ItemType File -Force "$data\..\outside.txt" -Value 'keep me' | Out-Null
        Set-Content "$data\$SkyrimPartRecord" "file=$('A' * 64) ..\outside.txt", 'dir=..'
        Uninstall-SkyrimPart $data | Out-Null
        Get-Content "$data\..\outside.txt" | Should -Be 'keep me'
        Test-Path $data | Should -BeTrue
    }
    It 'removes only the extras when nothing was recorded' {
        New-Item -ItemType File "$data\SKSE\Plugins\AhShitRim.ini" -Value 'launcher ini' | Out-Null
        @(Uninstall-SkyrimPart $data @('SKSE\Plugins\AhShitRim.ini')).Count | Should -Be 1
        Test-Path "$data\SKSE\Plugins\versionlib-1-6-1170-0.bin" | Should -BeTrue
    }
}

Describe 'Find-SkyrimPartZip' {
    It 'finds the release''s Skyrim zip next to the launcher' {
        $dir = New-Item -ItemType Directory "$TestDrive\release"
        New-Item -ItemType File "$dir\AhShitRim-Skyrim-0.1.0.zip" | Out-Null
        Find-SkyrimPartZip $dir | Should -Be "$dir\AhShitRim-Skyrim-0.1.0.zip"
        Find-SkyrimPartZip "$TestDrive\nowhere" | Should -BeNullOrEmpty
    }
}

Describe 'Get-ModManager' {
    It 'takes the configured choice first, in any case' {
        Get-ModManager 'none' $true $true | Should -Be 'None'
        Get-ModManager 'Mo2' $false $false | Should -Be 'MO2'
        Get-ModManager 'VORTEX' $true $false | Should -Be 'Vortex'
    }
    It 'detects MO2, then Vortex, else none' {
        Get-ModManager '' $true $true | Should -Be 'MO2'
        Get-ModManager $null $false $true | Should -Be 'Vortex'
        Get-ModManager '' $false $false | Should -Be 'None'
    }
}
