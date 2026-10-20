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

# Reads a Mod Organizer 2 ModOrganizer.ini (QSettings): the game folder it manages, its mods and
# profiles folders, the selected profile and its executables (@{ Title; Binary }).
function Read-Mo2Ini([string] $IniText, [string] $InstanceDir) {
    $values = @{}
    $section = ''
    foreach ($line in ($IniText -split "`r?`n")) {
        if ($line -match '^\s*\[(.+)\]\s*$') { $section = $Matches[1]; continue }
        if ($line -match '^\s*([^=;#]+?)\s*=\s*(.*?)\s*$') {
            $key, $value = "$section/$($Matches[1])", $Matches[2]
            if ($value -match '^"(.*)"$') { $value = $Matches[1] }
            if ($value -match '^@ByteArray\((.*)\)$') { $value = $Matches[1] }
            $values[$key] = $value -replace '\\\\', '\'
        }
    }
    function Path-Of($value) { if ($value) { $value -replace '/', '\' } }
    $base = if ($values['Settings/base_directory']) { Path-Of $values['Settings/base_directory'] } else { $InstanceDir }
    $base = $base.TrimEnd('\')
    function Dir-Of($setting, $default) {
        $value = $values["Settings/$setting"]
        if ($value) { (Path-Of $value) -replace '%BASE_DIR%', $base } else { "$base\$default" }
    }
    $executables = @()
    $count = 0
    [void] [int]::TryParse($values['customExecutables/size'], [ref] $count)
    for ($i = 1; $i -le $count; $i++) {
        $title = $values["customExecutables/$i\title"]
        if ($title) { $executables += [pscustomobject] @{ Title = $title; Binary = Path-Of $values["customExecutables/$i\binary"] } }
    }
    $selected = $values['General/selected_profile']
    [pscustomobject] @{
        GamePath        = Path-Of $values['General/gamePath']
        ModsDir         = Dir-Of 'mod_directory' 'mods'
        ProfilesDir     = Dir-Of 'profiles_directory' 'profiles'
        SelectedProfile = if ($selected) { $selected } else { 'Default' }
        Executables     = $executables
    }
}

# The mods a profile's modlist.txt enables: "+Name" lines. "-Name" is a disabled mod, "*Name" an
# unmanaged one (DLC, loose files) with no folder of its own, "#" a comment.
function Get-Mo2EnabledMods([string] $ModlistText) {
    @(foreach ($line in ($ModlistText -split "`r?`n")) {
        if ($line -match '^\+(.+?)\s*$') { $Matches[1] }
    })
}

# The title of the instance's executable that runs this Skyrim's skse64_loader.exe ($null if none).
function Find-Mo2SkseExecutable($Executables, [string] $SkyrimPath) {
    $loader = "$(($SkyrimPath -replace '/', '\').TrimEnd('\'))\skse64_loader.exe"
    foreach ($exe in $Executables) {
        if (Test-SamePath $exe.Binary $loader) { return $exe.Title }
    }
    return $null
}

# MO2's shortcut link: moshortcut://<instance>:<executable title>. An empty instance name means
# the portable instance (MO2's MOShortcut::isForInstance).
function Get-Mo2ShortcutUri([string] $Instance, [string] $Title) {
    return "moshortcut://$($Instance):$Title"
}

# ModOrganizer.exe from a shell command such as the nxm:// handler's ("C:\MO2\nxmhandler.exe" "%1"):
# MO2's own nxmhandler.exe sits next to ModOrganizer.exe. $null for any other program.
function Get-Mo2ExeFromCommand([string] $Command) {
    if ($Command -notmatch '^\s*(?:"([^"]+)"|(\S+))') { return $null }
    $program = if ($Matches[1]) { $Matches[1] } else { $Matches[2] }
    $leaf = Split-Path -Leaf ($program -replace '/', '\')
    if ($leaf -notin 'nxmhandler.exe', 'ModOrganizer.exe') { return $null }
    $dir = $program.Substring(0, $program.Length - $leaf.Length).TrimEnd('\', '/')
    return "$dir\ModOrganizer.exe"
}

# Where Skyrim gets AhShitRim.dll from. Loose: the real Data\SKSE\Plugins has it. Instances:
# @{ Name; Enabled } per MO2 instance for this Skyrim that has a mod with it (Enabled: in the
# selected profile). An enabled MO2 mod wins over a loose copy, which then gets a warning (it
# loads whenever Skyrim starts outside MO2); several instances with it enabled: the first.
function Get-SkyrimPluginSource([bool] $Loose, $Instances) {
    $enabled = @($Instances | Where-Object { $_.Enabled })
    $disabled = @($Instances | Where-Object { -not $_.Enabled })
    $notes = @()
    if ($enabled.Count -gt 1) {
        $notes += "AhShitRim is enabled in several MO2 instances ($(($enabled | ForEach-Object { $_.Name }) -join ', ')), using '$($enabled[0].Name)'."
    }
    if ($enabled -and $Loose) {
        $notes += "A loose AhShitRim.dll is in Skyrim's own Data\SKSE\Plugins as well. It loads whenever Skyrim starts outside MO2, so delete it (and AhShitRim.pdb next to it) and keep the MO2 mod."
    }
    if (-not $enabled -and $disabled) {
        $notes += "AhShitRim is installed in MO2 ('$($disabled[0].Name)') but not enabled in its selected profile. Tick it in MO2's left pane$(if ($Loose) { '; until then the loose AhShitRim.dll in Data\SKSE\Plugins loads' })."
    }
    $mode = if ($enabled) { 'Mo2' } elseif ($Loose) { 'Loose' } else { 'None' }
    [pscustomobject] @{ Mode = $mode; Instance = if ($enabled) { $enabled[0].Name }; Warnings = $notes }
}

# The same folder, whatever the slashes, case or a trailing separator.
function Test-SamePath([string] $A, [string] $B) {
    if (-not $A -or -not $B) { return $false }
    $trim = { param($p) ($p -replace '/', '\').TrimEnd('\') }
    return (& $trim $A) -eq (& $trim $B)
}
