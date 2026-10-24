<#
.SYNOPSIS
    AhShitRim player launcher (ships in the release zip; "Start AhShitRim.bat" runs it).
    Finds Skyrim and the downgraded San Andreas (launcher.ini next to this script, else the Steam
    libraries, else asks once), checks Skyrim's version, SKSE, Address Library and AhShitRim's
    SKSE plugin (in Skyrim's Data folder or a Mod Organizer 2 mod), gta_sa.exe being 1.0 US and
    the ASI loader. Then it copies AhShitRim.asi into San Andreas's scripts folder, points the
    SKSE plugin at San Andreas (Data\SKSE\Plugins\AhShitRim.ini), starts San Andreas hidden, then
    Skyrim: through MO2 (a moshortcut:// link to the instance's SKSE executable) when AhShitRim is
    an enabled MO2 mod, which wins over a loose copy in Data, else through skse64_loader.exe.
    The checks themselves are in checks.ps1.

    -NoStart checks and installs, and starts nothing.
#>
param([switch] $NoStart)

$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
$ConfigPath = Join-Path $Root 'launcher.ini'
. (Join-Path $Root 'checks.ps1')

function Fail($lines) {
    Write-Host ''
    foreach ($line in @($lines)) { Write-Host $line -ForegroundColor Red }
    exit 1
}

# --- Game folders ---------------------------------------------------------------------------

function Read-Config {
    $config = @{}
    if (Test-Path $ConfigPath) {
        foreach ($line in Get-Content $ConfigPath) {
            if ($line -match '^\s*(\w+)\s*=\s*(.*?)\s*$') { $config[$Matches[1]] = $Matches[2] }
        }
    }
    return $config
}

function Get-SteamLibraries {
    $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
    if (-not $steam) { return @() }
    $libraries = @($steam)
    $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
    if (Test-Path $vdf) {
        foreach ($match in [regex]::Matches((Get-Content -Raw $vdf), '"path"\s+"([^"]+)"')) {
            $libraries += $match.Groups[1].Value -replace '\\\\', '\'
        }
    }
    return $libraries | Select-Object -Unique
}

function Find-Game($name, $folder, $exe, $configured, $hint) {
    if ($configured -and (Test-Path (Join-Path $configured $exe))) { return $configured }
    if ($folder) {
        foreach ($library in Get-SteamLibraries) {
            $path = Join-Path $library "steamapps\common\$folder"
            if (Test-Path (Join-Path $path $exe)) { return (Resolve-Path $path).Path }
        }
    }
    Write-Host "Pick the $name folder (the one with $exe)$hint."
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = "$name folder (the one with $exe)"
    if ($dialog.ShowDialog() -ne 'OK') { Fail "No $name folder picked." }
    if (-not (Test-Path (Join-Path $dialog.SelectedPath $exe))) {
        if ($exe -eq 'gta_sa.exe' -and (Test-Path (Join-Path $dialog.SelectedPath 'gta-sa.exe'))) {
            Fail "$($dialog.SelectedPath) has only the Steam exe (gta-sa.exe). Copy the folder and downgrade the copy to 1.0 US first: $($Links.Install)"
        }
        Fail "$exe is not in $($dialog.SelectedPath)."
    }
    return $dialog.SelectedPath
}

$config = Read-Config
$skyrim = Find-Game 'Skyrim Special Edition' 'Skyrim Special Edition' 'SkyrimSE.exe' $config['SkyrimPath'] ''
# The downgraded copy lives outside Steam (docs/INSTALL.md), so no Steam lookup for it.
$sa = Find-Game 'San Andreas' $null 'gta_sa.exe' $config['SaPath'] ', downgraded to 1.0 US'
function Save-Config {
    $lines = @('; Settings for the AhShitRim launcher. Delete this file to pick the folders again.')
    foreach ($key in 'SkyrimPath', 'SaPath', 'Mo2Path', 'Mo2Exe') { if ($config[$key]) { $lines += "$key=$($config[$key])" } }
    Set-Content -Path $ConfigPath -Encoding utf8 -Value $lines
}
if ($skyrim -ne $config['SkyrimPath'] -or $sa -ne $config['SaPath']) {
    $config['SkyrimPath'] = $skyrim
    $config['SaPath'] = $sa
    Save-Config
}
Write-Host "Skyrim:      $skyrim"
Write-Host "San Andreas: $sa"

# --- Skyrim ---------------------------------------------------------------------------------

function Get-FileNames($dir) {
    if (Test-Path $dir) { @(Get-ChildItem -File $dir | ForEach-Object Name) } else { @() }
}

# Mod Organizer 2 keeps mods in its own folder and shows them to Skyrim only when Skyrim starts
# from MO2. Its global instances live in %LOCALAPPDATA%\ModOrganizer; a portable one is Mo2Path
# in launcher.ini. Per instance for this Skyrim: whether a mod has AhShitRim.dll, whether one
# that has it is enabled in the selected profile, the enabled mods' SKSE plugin files and the
# executable that runs this Skyrim's skse64_loader.exe.
function Get-Mo2Instances {
    $dirs = @()
    $local = Join-Path $env:LOCALAPPDATA 'ModOrganizer'
    if (Test-Path $local) { $dirs += Get-ChildItem -Directory $local | ForEach-Object FullName }
    if ($config['Mo2Path']) { $dirs += $config['Mo2Path'] }
    foreach ($dir in $dirs) {
        $ini = Join-Path $dir 'ModOrganizer.ini'
        if (-not (Test-Path $ini)) { continue }
        $mo2 = Read-Mo2Ini (Get-Content -Raw $ini) $dir
        if (-not (Test-SamePath $mo2.GamePath $skyrim) -or -not (Test-Path $mo2.ModsDir)) { continue }
        $modlist = Join-Path $mo2.ProfilesDir "$($mo2.SelectedProfile)\modlist.txt"
        $enabled = if (Test-Path $modlist) { @(Get-Mo2EnabledMods (Get-Content -Raw $modlist)) } else { @() }
        $withDll = @(Get-ChildItem -Directory $mo2.ModsDir | Where-Object { Test-Path (Join-Path $_.FullName 'SKSE\Plugins\AhShitRim.dll') } | ForEach-Object Name)
        $portable = Test-Path (Join-Path $dir 'portable.txt')
        [pscustomobject] @{
            Name         = if ($portable) { "portable instance in $dir" } else { Split-Path -Leaf $dir }
            ShortcutName = if ($portable) { '' } else { Split-Path -Leaf $dir }
            Dir          = $dir
            Profile      = $mo2.SelectedProfile
            HasDll       = $withDll.Count -gt 0
            Enabled      = @($withDll | Where-Object { $enabled -contains $_ }).Count -gt 0
            Plugins      = @(foreach ($mod in $enabled) { Get-FileNames (Join-Path $mo2.ModsDir "$mod\SKSE\Plugins") })
            SkseTitle    = Find-Mo2SkseExecutable $mo2.Executables $skyrim
        }
    }
}

$realPlugins = Join-Path $skyrim 'Data\SKSE\Plugins'
$pluginFiles = Get-FileNames $realPlugins
$instances = @(Get-Mo2Instances)
$source = Get-SkyrimPluginSource ($pluginFiles -contains 'AhShitRim.dll') @($instances | Where-Object { $_.HasDll })
$mo2 = if ($source.Mode -eq 'Mo2') { $instances | Where-Object { $_.Name -eq $source.Instance } | Select-Object -First 1 }
$mo2Plugins = @($instances | ForEach-Object { $_.Plugins })

$skyrimExe = Join-Path $skyrim 'SkyrimSE.exe'
$problems = @(Get-SkyrimProblems (Get-Item $skyrimExe).VersionInfo.FileVersion (Get-FileNames $skyrim) ($pluginFiles + $mo2Plugins))

# --- San Andreas ----------------------------------------------------------------------------

$saExe = Join-Path $sa 'gta_sa.exe'
$md5 = if (Test-Path $saExe) { (Get-FileHash -Algorithm MD5 $saExe).Hash }
$dlls = @(Get-ChildItem -File $sa -Filter *.dll | ForEach-Object { @{ Name = $_.Name; Description = $_.VersionInfo.FileDescription } })
$saCheck = Get-SaProblems $md5 (Get-FileNames $sa) (Get-AsiLoader $dlls)
$problems += $saCheck.Errors

# The release zip carries the ASI under "San Andreas"; a repo checkout takes it from the build output.
$asi = Join-Path $Root 'San Andreas\scripts\AhShitRim.asi'
if (-not (Test-Path $asi)) { $asi = Join-Path (Split-Path $Root) 'sa\build\Release\AhShitRim.asi' }
$asiDest = Join-Path $sa 'scripts\AhShitRim.asi'
if (-not (Test-Path $asi) -and -not (Test-Path $asiDest)) {
    $problems += "AhShitRim.asi is missing: it should be in 'San Andreas\scripts' next to this launcher. Extract the whole release zip again."
}

foreach ($warning in @($source.Warnings) + @($saCheck.Warnings)) { Write-Host $warning -ForegroundColor Yellow }
if ($problems) {
    Fail (@('Not ready yet:') + ($problems | ForEach-Object { " - $_" }) + '', "Fix those and run the launcher again. Help: $($Links.Install)")
}
Write-Host "Checks passed: Skyrim $((Get-SkyrimRuntime (Get-Item $skyrimExe).VersionInfo.FileVersion).Version), San Andreas $(if (Get-SaExeVersion $md5) { (Get-SaExeVersion $md5).Name } else { '(unknown exe)' })."

if (Get-Process SkyrimSE, skse64_loader -ErrorAction SilentlyContinue) { Fail 'Skyrim is already running. Close it and run the launcher again.' }
$saRunning = Get-Process gta_sa -ErrorAction SilentlyContinue

# --- Install --------------------------------------------------------------------------------

if ((Test-Path $asi) -and -not ((Test-Path $asiDest) -and (Get-FileHash $asiDest).Hash -eq (Get-FileHash $asi).Hash)) {
    if ($saRunning) { Fail 'San Andreas is running with an older AhShitRim.asi. Close it and run the launcher again.' }
    New-Item -ItemType Directory -Force (Split-Path $asiDest) | Out-Null
    try { Copy-Item -Force $asi $asiDest }
    catch { Fail "Can't write $asiDest ($($_.Exception.Message))." }
    Write-Host "Installed $asiDest"
}

# The SKSE plugin reads San Andreas's fonts and HUD sprites from this folder. It goes in the real
# Data folder, which MO2 shows to Skyrim as well, never in a mod manager's mod.
$ini = Join-Path $realPlugins 'AhShitRim.ini'
# The plugin reads it as narrow text, so it's in the ANSI code page (a non-ASCII folder name survives).
# F11 screenshots go to Screenshots next to this script.
$shots = Join-Path $Root 'Screenshots'
$ansi = [Text.Encoding]::Default
# The player's own settings stay: the key layout, and whether misc quests are odd jobs (on unless set to 0).
$oldLines = if (Test-Path $ini) { [IO.File]::ReadAllLines($ini, $ansi) } else { @() }
$layout = $oldLines | Where-Object { $_ -match '^\s*KeyLayout\s*=' } | Select-Object -First 1
$misc = $oldLines | Where-Object { $_ -match '^\s*MiscPassed\s*=' } | Select-Object -First 1
$iniLines = [System.Collections.Generic.List[string]]::new()
$iniLines.AddRange([string[]] @('[SanAndreas]', "Path=$sa", '[Screenshots]', "Dir=$shots"))
if ($layout) { $iniLines.AddRange([string[]] @('[Input]', $layout.Trim())) }
$iniLines.AddRange([string[]] @('[Missions]', $(if ($misc) { $misc.Trim() } else { 'MiscPassed=1' })))
$iniLines = $iniLines.ToArray()
if (-not (Test-Path $ini) -or ([IO.File]::ReadAllLines($ini, $ansi) -join "`n") -ne ($iniLines -join "`n")) {
    New-Item -ItemType Directory -Force $realPlugins | Out-Null
    try { [IO.File]::WriteAllLines($ini, $iniLines, $ansi) }
    catch { Fail "Can't write $ini ($($_.Exception.Message))." }
    Write-Host "Wrote $ini"
}
Write-Host "Screenshots (F11) go to $shots"

if ($NoStart) { Write-Host 'Checked and installed, not starting the games (-NoStart).'; return }

# --- Start San Andreas ----------------------------------------------------------------------

if ($saRunning) {
    Write-Host 'San Andreas is already running, leaving it as it is.'
} else {
    Write-Host 'Starting San Andreas (it stays minimized)'
    # The ASI rewrites its log at every start, so a ready line newer than the launch is this run's.
    $log = Join-Path $sa 'scripts\AhShitRim.log'
    $launched = Get-Date
    $process = Start-Process -PassThru -WorkingDirectory $sa $saExe
    $ready = $null
    while ((Get-Date) -lt $launched.AddSeconds(120)) {
        Start-Sleep -Milliseconds 500
        if ($process.HasExited) {
            Fail @("San Andreas closed (exit code $($process.ExitCode)) before AhShitRim was ready.", "Its log, if there is one: $log")
        }
        if ((Test-Path $log) -and (Get-Item $log).LastWriteTime -gt $launched) {
            $ready = Select-String -Path $log -SimpleMatch -Pattern "runner: CJ waits for Skyrim's ground", 'runner: could not create CJ', 'not starting' | Select-Object -First 1
            if ($ready) { break }
        }
    }
    if (-not $ready) {
        Fail @('San Andreas did not get ready within 2 minutes.', "If $log is missing, the ASI loader isn't loading scripts\AhShitRim.asi.")
    }
    if ($ready.Line -notlike '*CJ waits*') { Fail @("San Andreas started, but AhShitRim isn't running in it: $($ready.Line.Trim())", "Its log: $log") }
    Write-Host "San Andreas is up (pid $($process.Id)), minimized." -ForegroundColor Green
}

# --- Start Skyrim ---------------------------------------------------------------------------

# ModOrganizer.exe: Mo2Exe in launcher.ini, a portable instance's own folder, a running MO2, the
# nxm:// link handler MO2 registers, or else asked for once (and kept as Mo2Exe).
function Find-Mo2Exe($instance) {
    $candidates = @($config['Mo2Exe'])
    if ($instance.ShortcutName -eq '') { $candidates += Join-Path $instance.Dir 'ModOrganizer.exe' }
    $candidates += @(Get-Process ModOrganizer -ErrorAction SilentlyContinue | ForEach-Object Path)
    $nxm = (Get-ItemProperty 'HKCU:\Software\Classes\nxm\shell\open\command' -ErrorAction SilentlyContinue).'(default)'
    if ($nxm) { $candidates += Get-Mo2ExeFromCommand $nxm }
    foreach ($candidate in $candidates) { if ($candidate -and (Test-Path $candidate)) { return $candidate } }
    Write-Host 'Pick ModOrganizer.exe, to start Skyrim through Mod Organizer 2.'
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    $dialog.Title = 'ModOrganizer.exe'
    $dialog.Filter = 'ModOrganizer.exe|ModOrganizer.exe'
    if ($dialog.ShowDialog() -ne 'OK') { return $null }
    $config['Mo2Exe'] = $dialog.FileName
    Save-Config
    return $dialog.FileName
}

if ($source.Mode -eq 'Mo2') {
    # MO2 runs the shortcut's executable itself, or hands it to the MO2 already open (which
    # refuses, with a message, when that one is open for another instance).
    $started = $false
    if (-not $mo2.SkseTitle) {
        Write-Host "MO2 instance '$($mo2.Name)' has no executable for $(Join-Path $skyrim 'skse64_loader.exe')." -ForegroundColor Yellow
    } else {
        $mo2Exe = Find-Mo2Exe $mo2
        if ($mo2Exe) {
            $uri = Get-Mo2ShortcutUri $mo2.ShortcutName $mo2.SkseTitle
            Write-Host "Starting Skyrim through Mod Organizer 2 ('$($mo2.SkseTitle)' in '$($mo2.Name)')"
            try {
                Start-Process -FilePath $mo2Exe -WorkingDirectory (Split-Path $mo2Exe) -ArgumentList "`"$uri`""
                $started = $true
            } catch { Write-Host "Couldn't start $mo2Exe ($($_.Exception.Message))." -ForegroundColor Yellow }
        }
    }
    if (-not $started) {
        Write-Host ''
        Write-Host "AhShitRim's Skyrim part is a Mod Organizer 2 mod, so start Skyrim from MO2 now: pick SKSE in its executables list and press Run." -ForegroundColor Green
        Write-Host 'Then load a save and press F10 to hand the player to San Andreas.' -ForegroundColor Green
        Read-Host 'Press Enter to close this window' | Out-Null
        return
    }
} else {
    Write-Host 'Starting Skyrim (SKSE)'
    Start-Process -WorkingDirectory $skyrim (Join-Path $skyrim 'skse64_loader.exe')
}
Write-Host ''
Write-Host 'Load a save in Skyrim (or start a new game) and press F10 to hand the player to San Andreas.' -ForegroundColor Green
Write-Host 'This window closes in 15 seconds.'
Start-Sleep -Seconds 15
