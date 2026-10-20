<#
.SYNOPSIS
    The launcher's checks as pure functions: they get what the launcher read from the disk (file
    versions, file names, hashes, ini text) and return what's wrong, so scripts/test.ps1 covers
    them without either game installed (launcher/tests/checks.Tests.ps1). Dot-source it.
#>

$Links = @{
    Skse           = 'https://skse.silverlock.org'
    AddressLibrary = 'https://www.nexusmods.com/skyrimspecialedition/mods/32444'
    Downgrader     = 'https://github.com/xxanqw/gtasa-open-downgrader/releases/latest'
    Install        = 'https://github.com/rackmarten/AhShitRim/blob/main/docs/INSTALL.md'
}

# gta_sa.exe by MD5, as gtasa-open-downgrader identifies them. The ASI runs on 1.0 US only.
$SaExeVersions = @{
    '170b3a9108687b26da2d8901c6948a18' = @{ Name = '1.0 US (HOODLUM)'; Supported = $true }
    '2b5066bd4097ac2944ce6a9cf8fe5677' = @{ Name = '1.0 US (HOODLUM, 4GB flag)'; Supported = $true }
    '4e99d762f44b1d5e7652dfa7e73d6b6f' = @{ Name = '1.0 US'; Supported = $true }
    '667f799c4ba8c9e1054fccaea6d4259b' = @{ Name = '1.0 US (compact)'; Supported = $true }
    '6c6160da9b175b66cf9127c86be57bf7' = @{ Name = '1.0 EU'; Supported = $false }
    '49dd417760484a18017805df46b308b8' = @{ Name = '1.0 EU'; Supported = $false }
    '9f2d711dbf1fbbcda5ff9418a2cc1ef5' = @{ Name = '1.01 US'; Supported = $false }
    '25405921d1c47747fd01fd0bfe0a05ae' = @{ Name = '1.01 EU'; Supported = $false }
    '5bfd4dd83989a8264de4b8e771f237fd' = @{ Name = 'the current Steam version'; Supported = $false }
    'd9cb35c898d3298ca904a63e10ee18d7' = @{ Name = 'the current Steam version (German)'; Supported = $false }
    'd9cb35c898d3298ca904a63e10ee18d9' = @{ Name = 'the current Steam version (German)'; Supported = $false }
}

# The first Anniversary Edition runtime. Older ones are the Special Edition's.
$MinSkyrimVersion = [version] '1.6.317'

# Which gta_sa.exe an MD5 is ($null when it's none we know).
function Get-SaExeVersion([string] $Md5) {
    if (-not $Md5) { return $null }
    $known = $SaExeVersions[$Md5.ToLowerInvariant()]
    if ($known) { return [pscustomobject] $known }
    return $null
}

# SkyrimSE.exe's FileVersion ("1.6.1170.0") and the SKSE and Address Library files made for it.
function Get-SkyrimRuntime([string] $FileVersion) {
    if ($FileVersion -notmatch '^\s*(\d+)[.,]\s*(\d+)[.,]\s*(\d+)') { return $null }
    $version = [version] "$($Matches[1]).$($Matches[2]).$($Matches[3])"
    [pscustomobject] @{
        Version        = $version
        IsAnniversary  = $version -ge $MinSkyrimVersion
        SkseDll        = "skse64_$($version.Major)_$($version.Minor)_$($version.Build).dll"
        AddressLibrary = "versionlib-$($version.Major)-$($version.Minor)-$($version.Build)-0.bin"
    }
}

# What's wrong with Skyrim's side. GameFiles: file names in the Skyrim folder. PluginFiles: file
# names in Data\SKSE\Plugins, the real one and every mod manager mod's.
function Get-SkyrimProblems([string] $FileVersion, [string[]] $GameFiles, [string[]] $PluginFiles) {
    $runtime = Get-SkyrimRuntime $FileVersion
    if (-not $runtime) { return @("Can't read SkyrimSE.exe's version. Check the Skyrim folder in launcher.ini.") }
    $problems = @()
    if (-not $runtime.IsAnniversary) {
        $problems += "Skyrim $($runtime.Version) is a Special Edition runtime. AhShitRim needs Anniversary Edition, $MinSkyrimVersion or later: let Steam update Skyrim, then update SKSE and Address Library to match."
    }
    if ($GameFiles -notcontains 'skse64_loader.exe') {
        $problems += "SKSE64 is not installed. Get the build for Skyrim $($runtime.Version) from $($Links.Skse) and copy skse64_loader.exe and its DLLs into the Skyrim folder."
    } elseif ($GameFiles -notcontains $runtime.SkseDll) {
        $found = @($GameFiles | Where-Object { $_ -like 'skse64_1_*.dll' })
        $what = if ($found) { " (found $($found -join ', '))" } else { '' }
        $problems += "SKSE64 is for another Skyrim version$what, not $($runtime.Version) ($($runtime.SkseDll)). Get the matching build from $($Links.Skse)."
    }
    if ($PluginFiles -notcontains $runtime.AddressLibrary) {
        $problems += "Address Library for SKSE Plugins is not installed for Skyrim $($runtime.Version) ($($runtime.AddressLibrary)). Install the All in one (Anniversary Edition) file from $($Links.AddressLibrary)."
    }
    if ($PluginFiles -notcontains 'AhShitRim.dll') {
        $problems += "AhShitRim's Skyrim part is not installed (Data\SKSE\Plugins\AhShitRim.dll). Install AhShitRim-Skyrim-*.zip with your mod manager, see $($Links.Install)."
    }
    return $problems
}

# Which ASI loader San Andreas has. Files: @{ Name; Description } for the DLLs in its folder.
# Silent's (the downgrader's) keeps the real vorbisFile.dll as vorbisHooked.dll; the Ultimate
# ASI Loader names itself in its file description.
function Get-AsiLoader($Files) {
    $names = @($Files | ForEach-Object { $_.Name })
    if ($names -contains 'vorbisHooked.dll') { return "Silent's ASI Loader" }
    foreach ($file in $Files) {
        if ($file.Name -match '^(dinput8|vorbisFile|version|winmm|dsound|d3d9|xlive)\.dll$' -and $file.Description -match 'ASI Loader') {
            return "Ultimate ASI Loader ($($file.Name))"
        }
    }
    return $null
}

# What's wrong with San Andreas's side: Errors stop the launcher, Warnings don't. ExeMd5 is
# gta_sa.exe's ($null without one), GameFiles the file names in the folder.
function Get-SaProblems([string] $ExeMd5, [string[]] $GameFiles, [string] $AsiLoader) {
    $errors = @()
    $warnings = @()
    $downgrade = "Downgrade it to 1.0 US with $($Links.Downgrader) (see $($Links.Install))."
    if (-not $ExeMd5) {
        if ($GameFiles -contains 'gta-sa.exe') {
            $errors += "This San Andreas folder has only the Steam exe (gta-sa.exe). $downgrade"
        } else {
            $errors += "gta_sa.exe is not in the San Andreas folder. Check SaPath in launcher.ini."
        }
    } else {
        $version = Get-SaExeVersion $ExeMd5
        if (-not $version) {
            $warnings += "gta_sa.exe isn't one this launcher knows (MD5 $($ExeMd5.ToLowerInvariant())). AhShitRim runs on 1.0 US only: if San Andreas's AhShitRim.log says its version isn't supported, downgrade it with $($Links.Downgrader)."
        } elseif (-not $version.Supported) {
            $errors += "gta_sa.exe is $($version.Name), and AhShitRim runs on 1.0 US only. $downgrade"
        }
    }
    if (-not $AsiLoader) {
        $errors += "San Andreas has no ASI loader. Install the ASI Loader from the downgrader's mod list (or Silent's ASI Loader), see $($Links.Install)."
    }
    [pscustomobject] @{ Errors = $errors; Warnings = $warnings }
}

# Reads a Mod Organizer 2 ModOrganizer.ini: the game folder it manages and its mods folder.
function Read-Mo2Ini([string] $IniText, [string] $InstanceDir) {
    $values = @{}
    foreach ($line in ($IniText -split "`r?`n")) {
        if ($line -match '^\s*(gamePath|base_directory|mod_directory)\s*=\s*(.*?)\s*$') {
            $key, $value = $Matches[1], $Matches[2]
            if ($value -match '^@ByteArray\((.*)\)$') { $value = $Matches[1] }
            $values[$key] = ($value -replace '\\\\', '\') -replace '/', '\'
        }
    }
    $base = if ($values['base_directory']) { $values['base_directory'] } else { $InstanceDir }
    $mods = if ($values['mod_directory']) { $values['mod_directory'] -replace '%BASE_DIR%', $base } else { "$($base.TrimEnd('\'))\mods" }
    [pscustomobject] @{ GamePath = $values['gamePath']; ModsDir = $mods }
}

# The same folder, whatever the slashes, case or a trailing separator.
function Test-SamePath([string] $A, [string] $B) {
    if (-not $A -or -not $B) { return $false }
    $trim = { param($p) ($p -replace '/', '\').TrimEnd('\') }
    return (& $trim $A) -eq (& $trim $B)
}
