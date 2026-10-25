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

    The first time (no usable SaPath in launcher.ini), setup sets San Andreas up: finds it, copies
    it, downgrades the copy and adds an ASI loader (wizard.ps1, install.ps1). Without a mod manager
    (ModManager=None in launcher.ini) the launcher copies the Skyrim part into Skyrim's Data folder
    itself, and keeps a record of it there so -Uninstall can take it out again.

    -NoStart checks and installs, and starts nothing.
    -Uninstall removes the Skyrim part the launcher installed, AhShitRim.ini and San Andreas's
    AhShitRim.asi ("Uninstall AhShitRim.bat"). The ASI loader and the San Andreas copy stay.
#>
param([switch] $NoStart, [switch] $Uninstall)

$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
$ConfigPath = Join-Path $Root 'launcher.ini'
. (Join-Path $Root 'checks.ps1')
. (Join-Path $Root 'install.ps1')
. (Join-Path $Root 'wizard.ps1')

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

function Find-Game($name, $folder, $exe, $configured, $hint) {
    # A folder on a drive that's gone (a removed Steam library) makes Join-Path throw; Join-Guess (install.ps1) doesn't.
    $configuredExe = Join-Guess $configured $exe
    if ($configuredExe -and (Test-Path -LiteralPath $configuredExe)) { return $configured }
    if ($folder) {
        foreach ($library in Get-SteamLibraries) {
            $path = Join-Guess $library "steamapps\common\$folder"
            $pathExe = Join-Guess $path $exe
            if ($pathExe -and (Test-Path -LiteralPath $pathExe)) { return (Resolve-Path -LiteralPath $path).Path }
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
function Save-Config {
    $lines = @('; Settings for the AhShitRim launcher. Delete this file to run setup again.')
    foreach ($key in 'SkyrimPath', 'SaPath', 'ModManager', 'Mo2Path', 'Mo2Exe') { if ($config[$key]) { $lines += "$key=$($config[$key])" } }
    Set-Content -Path $ConfigPath -Encoding utf8 -Value $lines
}

# Takes out what the launcher put in place: the Skyrim part it copied into Data (from its record),
# AhShitRim.ini, and AhShitRim.asi in San Andreas. A mod manager's mod stays for the player to remove.
function Uninstall-AhShitRim {
    if (Get-Process SkyrimSE, skse64_loader, gta_sa -ErrorAction SilentlyContinue) { Fail 'Close Skyrim and San Andreas first, then run the uninstall again.' }
    $data = Join-Path $skyrim 'Data'
    $record = Read-SkyrimPartRecord $data
    $saAsi = Join-Guess $config['SaPath'] 'scripts\AhShitRim.asi'
    $what = if ($record) { "AhShitRim's Skyrim part ($($record.Files.Count) files) and AhShitRim.ini from $data" } else { "AhShitRim.ini from $data" }
    if ($saAsi) { $what += ", and AhShitRim.asi from $(Split-Path $saAsi)" }
    if ((Show-Message "Remove ${what}?`n`nYour saves, San Andreas itself and the games stay as they are." 'YesNo' 'Question') -ne 'Yes') { return }
    $removed = @(Uninstall-SkyrimPart $data @('SKSE\Plugins\AhShitRim.ini') | ForEach-Object { Join-Path $data $_ })
    if ($saAsi -and (Test-Path $saAsi)) { Remove-Item -Force $saAsi; $removed += $saAsi }
    foreach ($file in $removed) { Write-Host "Removed $file" }
    # The next launch decides again where the Skyrim part goes.
    $config.Remove('ModManager')
    Save-Config
    $left = if (-not $record) { "`n`nIf you installed the Skyrim part with Mod Organizer 2 or Vortex, remove the AhShitRim mod there." } else { '' }
    Show-Message "AhShitRim is uninstalled. Start AhShitRim.bat sets it up again.$left" | Out-Null
}
if ($Uninstall) { Uninstall-AhShitRim; return }

# San Andreas: the folder in launcher.ini, else the setup wizard finds, copies, downgrades and
# prepares one (wizard.ps1; downloads go in Tools next to this script). The folder goes into
# launcher.ini as soon as it's chosen, so a setup stopped after the copy carries on from there
# next time (the downgrade and ASI loader steps further down).
$sa = $config['SaPath']
if (-not $sa -or -not (Select-SaInstalls @($sa))) {
    $sa = Invoke-SaSetup (Get-SteamLibraries) (Join-Path $Root 'Tools') { param($folder) $config['SkyrimPath'] = $skyrim; $config['SaPath'] = $folder; Save-Config }
    if (-not $sa) { Fail "Setup stopped before San Andreas was ready. Run the launcher again to carry on, or follow the manual steps: $($Links.Install)" }
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
            Dll          = @(@($withDll | Where-Object { $enabled -contains $_ }) + $withDll | ForEach-Object { Join-Path $mo2.ModsDir "$_\SKSE\Plugins\AhShitRim.dll" })[0]
            SkseTitle    = Find-Mo2SkseExecutable $mo2.Executables $skyrim
        }
    }
}

$realPlugins = Join-Path $skyrim 'Data\SKSE\Plugins'
$instances = @(Get-Mo2Instances)

# How the Skyrim part goes in, decided once while no mod manager has it: with no mod manager the
# launcher copies it, with one the player picks. ModManager in launcher.ini keeps it. A loose
# AhShitRim.dll in Data that no record names (a hand install, a developer's deploy, an older
# release) is no reason to stop: without a mod manager the launcher installs over it, so it can't
# stay a version that doesn't match San Andreas's ASI.
$skyrimData = Join-Path $skyrim 'Data'
$inModManager = @($instances | Where-Object { $_.HasDll }).Count -gt 0
# A part the launcher installed before (launcher.ini deleted since) stays the launcher's to update.
if (-not $config['ModManager'] -and (Read-SkyrimPartRecord $skyrimData)) { $config['ModManager'] = 'None'; Save-Config }
if (-not $config['ModManager'] -and -not $inModManager) {
    # Vortex keeps a deployment file in Data once it has deployed, and its settings in %APPDATA%\Vortex.
    $vortex = (Test-Path (Join-Path $skyrimData 'vortex.deployment.json')) -or (Test-Path (Join-Path $skyrimData 'vortex.deployment.msgpack')) -or (Test-Path (Join-Path $env:APPDATA 'Vortex'))
    $detected = Get-ModManager '' ($instances.Count -gt 0) $vortex
    $config['ModManager'] = if ($detected -eq 'None') { 'None' } else { Select-ModManager $detected }
    Save-Config
}
if ($config['ModManager'] -and (Get-ModManager $config['ModManager'] $false $false) -eq 'None') {
    # Kept up to date with the release zip next to this script, so a new release updates it.
    $partZip = Find-SkyrimPartZip $Root
    if (-not $partZip) {
        Write-Host 'No AhShitRim-Skyrim-*.zip next to the launcher, so the Skyrim part in Data stays as it is.' -ForegroundColor Yellow
    } elseif (-not (Test-SkyrimPartCurrent $partZip $skyrimData)) {
        if (Get-Process SkyrimSE, skse64_loader -ErrorAction SilentlyContinue) { Fail 'Skyrim is running. Close it so the launcher can install the Skyrim part, then run it again.' }
        try { $partFiles = @(Install-SkyrimPart $partZip $skyrimData) }
        catch { Fail "Can't install the Skyrim part into $skyrimData ($($_.Exception.Message))." }
        Write-Host "Installed AhShitRim's Skyrim part into $skyrimData ($($partFiles.Count) files). Uninstall AhShitRim.bat takes it out again."
    }
}

$pluginFiles = Get-FileNames $realPlugins
$source = Get-SkyrimPluginSource ($pluginFiles -contains 'AhShitRim.dll') @($instances | Where-Object { $_.HasDll })
$mo2 = if ($source.Mode -eq 'Mo2') { $instances | Where-Object { $_.Name -eq $source.Instance } | Select-Object -First 1 }
$mo2Plugins = @($instances | ForEach-Object { $_.Plugins })

$skyrimExe = Join-Path $skyrim 'SkyrimSE.exe'
$problems = @(Get-SkyrimProblems (Get-Item $skyrimExe).VersionInfo.FileVersion (Get-FileNames $skyrim) ($pluginFiles + $mo2Plugins))
# Mod Organizer 2 picked for the Skyrim part, but none of its mods has it yet: say how to add it,
# instead of checking a loose copy in Data that MO2 doesn't stand for (install.ps1).
$mo2Missing = $config['ModManager'] -and (Get-ModManager $config['ModManager'] $false $false) -eq 'MO2' -and -not $inModManager
if ($mo2Missing) {
    $problems = @($problems | Where-Object { $_ -notlike "AhShitRim's Skyrim part is not installed*" })
    $problems += @(Get-Mo2MissingProblems (Find-SkyrimPartZip $Root) ($pluginFiles -contains 'AhShitRim.dll') ([bool] (Read-SkyrimPartRecord $skyrimData)))
}

# --- San Andreas ----------------------------------------------------------------------------

# Steam undid the downgrade, or the ASI loader went missing: setup's steps offer to fix it (wizard.ps1).
if (Repair-SaExeName $sa) { Write-Host "Copied the 1.0 US gta-sa.exe to gta_sa.exe in $sa" }
$saVersion = Get-SaFolderVersion $sa
if (-not (Test-Path (Join-Path $sa 'gta_sa.exe')) -or ($saVersion -and -not $saVersion.Supported)) { [void] (Invoke-SaDowngrade $sa (Join-Path $Root 'Tools')) }
if (-not (Get-SaFolderAsiLoader $sa)) { [void] (Invoke-AsiLoaderSetup $sa (Join-Path $Root 'Tools')) }
# AhShitRim is tested with the downgrader's 4GB patch, so the plain 1.0 US exe gets it (install.ps1).
if (Test-Path (Join-Path $sa 'gta_sa.exe')) {
    try { if (Set-LargeAddressAware (Join-Path $sa 'gta_sa.exe')) { Write-Host "Applied the 4GB patch (Large Address Aware) to $(Join-Path $sa 'gta_sa.exe')" } }
    catch { Write-Host "Couldn't apply the 4GB patch to gta_sa.exe ($($_.Exception.Message)). San Andreas runs without it; close it and run the launcher again." -ForegroundColor Yellow }
}

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

# Both halves must speak the same link version: the Skyrim plugin that will load, and the ASI San Andreas will have.
$dll = if ($source.Mode -eq 'Mo2') { $mo2.Dll } else { Join-Path $realPlugins 'AhShitRim.dll' }
$asiUsed = if (Test-Path $asi) { $asi } else { $asiDest }
if (-not $mo2Missing -and $dll -and (Test-Path $dll) -and (Test-Path $asiUsed)) {
    $ascii = { param($path) [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($path)) }
    $problems += @(Get-LinkVersionProblem (& $ascii $dll) (& $ascii $asiUsed) $dll $asiUsed)
}

foreach ($warning in @($source.Warnings) + @($saCheck.Warnings)) { Write-Host $warning -ForegroundColor Yellow }
if ($problems) {
    # SKSE and the Address Library come from their own sites: offer to open the pages.
    $pages = @($Links.Skse, $Links.AddressLibrary | Where-Object { $page = $_; @($problems | Where-Object { $_ -like "*$page*" }).Count -gt 0 })
    if ($pages -and (Show-Message ((@('Not ready yet:') + ($problems | ForEach-Object { " - $_" }) + '', 'Open the download pages in your browser?') -join "`n") 'YesNo' 'Warning') -eq 'Yes') {
        foreach ($page in $pages) { Start-Process $page }
    }
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
# The player's own settings stay: the key layout, whether misc quests are odd jobs (on unless set to 0), and [Hud] Compass.
$oldLines = if (Test-Path $ini) { [IO.File]::ReadAllLines($ini, $ansi) } else { @() }
$layout = $oldLines | Where-Object { $_ -match '^\s*KeyLayout\s*=' } | Select-Object -First 1
$misc = $oldLines | Where-Object { $_ -match '^\s*MiscPassed\s*=' } | Select-Object -First 1
$compass = $oldLines | Where-Object { $_ -match '^\s*Compass\s*=' } | Select-Object -First 1
$iniLines = [System.Collections.Generic.List[string]]::new()
$iniLines.AddRange([string[]] @('[SanAndreas]', "Path=$sa", '[Screenshots]', "Dir=$shots"))
if ($layout) { $iniLines.AddRange([string[]] @('[Input]', $layout.Trim())) }
$iniLines.AddRange([string[]] @('[Missions]', $(if ($misc) { $misc.Trim() } else { 'MiscPassed=1' })))
if ($compass) { $iniLines.AddRange([string[]] @('[Hud]', $compass.Trim())) }
$iniLines = $iniLines.ToArray()
if (-not (Test-Path $ini) -or ([IO.File]::ReadAllLines($ini, $ansi) -join "`n") -ne ($iniLines -join "`n")) {
    New-Item -ItemType Directory -Force $realPlugins | Out-Null
    try { [IO.File]::WriteAllLines($ini, $iniLines, $ansi) }
    catch { Fail "Can't write $ini ($($_.Exception.Message))." }
    Write-Host "Wrote $ini"
}
Write-Host "Screenshots (F11) go to $shots"

# The radar's map, painted from this Skyrim's plugins into Documents\My Games\AhShitRim (a few seconds, and
# nothing while it's up to date). The release zip carries the painter; a repo checkout takes the tools build.
$painter = Join-Path $Root 'radar_map.exe'
if (-not (Test-Path $painter)) { $painter = Join-Path (Split-Path $Root) 'protocol\build\tools\radar_map.exe' }
if (Test-Path $painter) {
    & $painter --skyrim $skyrim
    if ($LASTEXITCODE -ne 0) { Write-Host "The radar's map couldn't be painted (exit code $LASTEXITCODE): the radar shows plain ground." -ForegroundColor Yellow }
} else {
    Write-Host "radar_map.exe is missing next to this launcher: the radar shows plain ground until there's a map." -ForegroundColor Yellow
}

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
