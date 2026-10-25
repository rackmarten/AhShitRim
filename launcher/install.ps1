<#
.SYNOPSIS
    What the launcher installs, as functions with no dialogs: finding San Andreas installs,
    copying one, the pinned downloads (verified by SHA-256 before anything runs), the ASI
    loader, and the Skyrim part straight into Skyrim's Data folder with a record of what it
    copied so it can be uninstalled again. launcher/tests/install.Tests.ps1 covers them on
    temporary folders. wizard.ps1 asks the player; AhShitRim.ps1 dot-sources both.
#>

# Every download the launcher makes, each from its project's own GitHub release and checked
# against the SHA-256 GitHub lists for that asset. A new version means a new URL and hash here.
$PinnedDownloads = @{
    # gtasa-open-downgrader (MIT, the open-source successor of the RockstarNexus downgrader): turns
    # the current Steam San Andreas into 1.0 US. The online build fetches its patches itself.
    Downgrader = [pscustomobject] @{
        Name    = 'gtasa-open-downgrader 0.2.3'
        Source  = 'github.com/xxanqw/gtasa-open-downgrader'
        Url     = 'https://github.com/xxanqw/gtasa-open-downgrader/releases/download/v0.2.3/gtasa-open-downgrader-windows.exe'
        Sha256  = 'b9868a87fb93ab5d8ec6ea75d16dabe51010623e4291c627d271bc87cbd89637'
        Size    = 57103610
        File    = 'gtasa-open-downgrader-windows.exe'
    }
    # ThirteenAG's Ultimate ASI Loader, 32-bit dinput8.dll only (San Andreas loads dinput8.dll itself).
    AsiLoader  = [pscustomobject] @{
        Name    = 'Ultimate ASI Loader 9.7.4'
        Source  = 'github.com/ThirteenAG/Ultimate-ASI-Loader'
        Url     = 'https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/v9.7.4/Ultimate-ASI-Loader-NoPDB.zip'
        Sha256  = '14b3a1ad018899571ac9aa01482977f3c6d49e6cba99f552d01c5acacd1315e1'
        Size    = 928310
        File    = 'Ultimate-ASI-Loader-NoPDB.zip'
    }
}

# The record of a Skyrim part installed straight into Data, next to the plugin.
$SkyrimPartRecord = 'SKSE\Plugins\AhShitRim.installed.txt'

function Format-Size([long] $Bytes) {
    if ($Bytes -ge 1GB) { return '{0:N1} GB' -f ($Bytes / 1GB) }
    if ($Bytes -ge 1MB) { return '{0:N0} MB' -f ($Bytes / 1MB) }
    return '{0:N0} KB' -f [math]::Max(1.0, $Bytes / 1KB)
}

# How far a copy of $Total bytes is at $Done, 0 to 100. In 64 bits: a game is more than an Int32 of bytes.
function Get-CopyPercent([long] $Done, [long] $Total) {
    if ($Total -le 0) { return 100 }
    return [int] [math]::Min(100L, [math]::Floor(100.0 * $Done / $Total))
}

# --- San Andreas ------------------------------------------------------------------------------

# $Base\$Child as plain text, or $null when it can't be one. Join-Path and Split-Path throw for a
# drive that doesn't exist (a Steam library on a drive since removed), so guesses use these instead.
function Join-Guess([string] $Base, [string] $Child) {
    if (-not $Base) { return $null }
    try { return [IO.Path]::Combine($Base, $Child) } catch { return $null }
}

# Steam's library folders: Steam's own folder and every "path" in its libraryfolders.vdf, which
# may name a drive that isn't there any more. $SteamPath defaults to the one in the registry.
function Get-SteamLibraries([string] $SteamPath = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath) {
    if (-not $SteamPath) { return @() }
    $libraries = @($SteamPath)
    $vdf = Join-Guess $SteamPath 'steamapps\libraryfolders.vdf'
    if ($vdf -and (Test-Path -LiteralPath $vdf)) {
        foreach ($match in [regex]::Matches((Get-Content -Raw -LiteralPath $vdf), '"path"\s+"([^"]+)"')) {
            $libraries += $match.Groups[1].Value -replace '\\\\', '\'
        }
    }
    return $libraries | Select-Object -Unique
}

# A game folder from a registry value: a folder, or an exe path (quoted or not).
function Get-FolderFromRegistryValue([string] $Value) {
    if (-not $Value) { return $null }
    $path = $Value.Trim().Trim('"')
    try { if ($path -match '\.exe$') { $path = [IO.Path]::GetDirectoryName($path) } } catch { return $null }
    if (-not $path) { return $null }
    return $path.TrimEnd('\')
}

# The folders where San Andreas may be: Rockstar's own registry keys (retail, Rockstar Games
# Launcher), any uninstall entry named San Andreas (Steam's "Steam App 12120" among them, never the
# Definitive Edition), the Steam libraries and the usual install folders. Not checked on disk, and
# never throws: a folder on a drive that's gone is just a guess Select-SaInstalls drops.
function Get-SaFolderGuesses($SteamLibraries) {
    $guesses = @()
    $rockstar = 'Software\WOW6432Node\Rockstar Games', 'Software\Rockstar Games'
    foreach ($key in $rockstar) {
        foreach ($sub in 'Grand Theft Auto San Andreas\Installation', 'Grand Theft Auto San Andreas', 'GTA San Andreas') {
            $item = Get-ItemProperty "HKLM:\$key\$sub" -ErrorAction SilentlyContinue
            if ($item) { $guesses += Get-FolderFromRegistryValue $item.ExePath; $guesses += Get-FolderFromRegistryValue $item.InstallFolder }
        }
    }
    $uninstall = 'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*', 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*', 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*'
    foreach ($entry in Get-ItemProperty $uninstall -ErrorAction SilentlyContinue) {
        if ($entry.DisplayName -match 'San Andreas' -and $entry.DisplayName -notmatch 'Definitive') { $guesses += Get-FolderFromRegistryValue $entry.InstallLocation }
    }
    foreach ($library in @($SteamLibraries)) { $guesses += Join-Guess $library 'steamapps\common\Grand Theft Auto San Andreas' }
    foreach ($programs in ${env:ProgramFiles(x86)}, $env:ProgramFiles) {
        if (-not $programs) { continue }
        foreach ($name in 'Rockstar Games\GTA San Andreas', 'Rockstar Games\Grand Theft Auto San Andreas', 'Rockstar Games\San Andreas') { $guesses += Join-Guess $programs $name }
    }
    return @($guesses | Where-Object { $_ })
}

# The guesses that hold a San Andreas exe (gta_sa.exe, or gta-sa.exe for the current Steam build),
# once each, as @{ Path; Exe }. A folder that can't be read (a drive that's gone, a bad name) is skipped.
function Select-SaInstalls([string[]] $Folders) {
    $seen = @{}
    foreach ($folder in $Folders) {
        try {
            if (-not $folder -or -not (Test-Path -LiteralPath $folder -PathType Container)) { continue }
            $full = (Resolve-Path -LiteralPath $folder).Path.TrimEnd('\')
            if ($seen[$full.ToLowerInvariant()]) { continue }
            $seen[$full.ToLowerInvariant()] = $true
            foreach ($exe in 'gta_sa.exe', 'gta-sa.exe') {
                if (Test-Path -LiteralPath ([IO.Path]::Combine($full, $exe))) { [pscustomobject] @{ Path = $full; Exe = $exe }; break }
            }
        } catch { continue }
    }
}

# Where a copy of San Andreas goes when the player picks $Picked: the picked folder itself when
# it's empty (or new), else a "San Andreas (AhShitRim)" folder inside it.
function Get-SaCopyDestination([string] $Picked) {
    if ($Picked -match '^[A-Za-z]:\\?$') { return $Picked.Substring(0, 2) + '\San Andreas (AhShitRim)' }
    if (-not (Test-Path -LiteralPath $Picked) -or -not (Get-ChildItem -LiteralPath $Picked -Force | Select-Object -First 1)) { return $Picked.TrimEnd('\') }
    return Join-Path $Picked 'San Andreas (AhShitRim)'
}

# Why San Andreas can't be copied from $Source to $Dest ($null when it can): the same folder or one
# inside the other, a Program Files destination (Windows makes those read-only for games), a
# destination that already has files, or too little room on its drive.
function Get-SaCopyProblem([string] $Source, [string] $Dest, [long] $SourceBytes, [long] $FreeBytes) {
    $src = ($Source -replace '/', '\').TrimEnd('\') + '\'
    $dst = ($Dest -replace '/', '\').TrimEnd('\') + '\'
    if ($dst.StartsWith($src, 'OrdinalIgnoreCase') -or $src.StartsWith($dst, 'OrdinalIgnoreCase')) { return "The copy can't go inside the original San Andreas folder (or the other way round). Pick another folder." }
    foreach ($programs in ${env:ProgramFiles(x86)}, $env:ProgramFiles) {
        if ($programs -and $dst.StartsWith($programs.TrimEnd('\') + '\', 'OrdinalIgnoreCase')) { return "Windows keeps Program Files read-only for games, so San Andreas can't be downgraded there. Pick a folder such as C:\Games." }
    }
    if ((Test-Path -LiteralPath $Dest) -and (Get-ChildItem -LiteralPath $Dest -Force | Select-Object -First 1)) { return "$Dest already has files in it. Pick an empty folder." }
    if ($FreeBytes -ge 0 -and $FreeBytes -lt $SourceBytes + 200MB) { return "The copy needs $(Format-Size $SourceBytes) and that drive has $(Format-Size $FreeBytes) free. Pick a folder on another drive." }
    return $null
}

function Get-FolderSize([string] $Path) {
    $sum = (Get-ChildItem -LiteralPath $Path -Recurse -File -Force -ErrorAction SilentlyContinue | Measure-Object -Sum Length).Sum
    if ($sum) { return [long] $sum } else { return 0L }
}

# Free bytes on the drive $Path is on (or will be on), -1 when it can't tell.
function Get-FreeBytes([string] $Path) {
    try { return (New-Object IO.DriveInfo ([IO.Path]::GetPathRoot([IO.Path]::GetFullPath($Path)))).AvailableFreeSpace } catch { return -1 }
}

# Copies a whole game folder, file by file with a progress bar. $Dest must not exist yet or be empty.
# A copy that fails halfway is taken out again, so the same folder can be picked for another go.
function Copy-GameFolder([string] $Source, [string] $Dest) {
    $source = (Resolve-Path -LiteralPath $Source).Path.TrimEnd('\')
    $files = @(Get-ChildItem -LiteralPath $source -Recurse -File -Force)
    $total = [long] ($files | Measure-Object -Sum Length).Sum
    $done = 0L
    $existed = Test-Path -LiteralPath $Dest
    try {
        New-Item -ItemType Directory -Force $Dest | Out-Null
        foreach ($dir in Get-ChildItem -LiteralPath $source -Recurse -Directory -Force) {
            New-Item -ItemType Directory -Force (Join-Path $Dest $dir.FullName.Substring($source.Length + 1)) | Out-Null
        }
        foreach ($file in $files) {
            $relative = $file.FullName.Substring($source.Length + 1)
            Write-Progress -Activity "Copying San Andreas to $Dest" -Status "$(Format-Size $done) of $(Format-Size $total)" -CurrentOperation $relative -PercentComplete (Get-CopyPercent $done $total)
            [IO.File]::Copy($file.FullName, (Join-Path $Dest $relative), $false)
            $done += $file.Length
        }
    } catch {
        Write-Progress -Activity "Copying San Andreas to $Dest" -Completed
        if ($existed) { Get-ChildItem -LiteralPath $Dest -Force | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue }
        else { Remove-Item -LiteralPath $Dest -Recurse -Force -ErrorAction SilentlyContinue }
        throw
    }
    Write-Progress -Activity "Copying San Andreas to $Dest" -Completed
}

# The 4GB patch the downgrader's tools offer, which AhShitRim is tested with: the Large Address Aware
# bit in gta_sa.exe's PE header, as the downgrader sets it. Only on the plain 1.0 US exe ($FromMd5),
# and only kept when the result is the known 1.0 US exe with the flag ($ToMd5); any other exe stays
# as it is. Returns $true when it patched.
function Set-LargeAddressAware([string] $Exe, [string] $FromMd5 = '170b3a9108687b26da2d8901c6948a18', [string] $ToMd5 = '2b5066bd4097ac2944ce6a9cf8fe5677') {
    if ((Get-FileHash -LiteralPath $Exe -Algorithm MD5).Hash -ne $FromMd5.ToUpperInvariant()) { return $false }
    $bytes = [IO.File]::ReadAllBytes($Exe)
    $pe = [BitConverter]::ToInt32($bytes, 60)
    if ($pe -lt 0 -or $pe + 24 -gt $bytes.Length -or [Text.Encoding]::ASCII.GetString($bytes, $pe, 4) -ne "PE`0`0") { return $false }
    # IMAGE_FILE_HEADER.Characteristics, 18 bytes into the file header after the "PE\0\0" signature.
    $bytes[$pe + 4 + 18] = $bytes[$pe + 4 + 18] -bor 0x20
    $md5 = [Security.Cryptography.MD5]::Create()
    try { $patched = -join ($md5.ComputeHash($bytes) | ForEach-Object { $_.ToString('X2') }) } finally { $md5.Dispose() }
    if ($patched -ne $ToMd5.ToUpperInvariant()) { return $false }
    [IO.File]::WriteAllBytes("$Exe.laa", $bytes)
    Move-Item -LiteralPath "$Exe.laa" $Exe -Force
    return $true
}

# --- Downloads ----------------------------------------------------------------------------------

function Test-Sha256([string] $Path, [string] $Expected) {
    return (Test-Path -LiteralPath $Path) -and (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -eq $Expected.ToUpperInvariant()
}

# Downloads $Download (a $PinnedDownloads entry) into $Dir, unless a verified copy is there already,
# and returns its path. A file whose SHA-256 isn't the pinned one is deleted and throws: nothing
# unverified is ever left behind to run.
function Save-VerifiedDownload($Download, [string] $Dir) {
    $dest = Join-Path $Dir $Download.File
    if (Test-Sha256 $dest $Download.Sha256) { return $dest }
    New-Item -ItemType Directory -Force $Dir | Out-Null
    $part = "$dest.part"
    Remove-Item -LiteralPath $part, $dest -Force -ErrorAction SilentlyContinue
    # Windows PowerShell 5.1 offers TLS 1.0 by default, which GitHub refuses.
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    $client = New-Object Net.WebClient
    try { $client.DownloadFile($Download.Url, $part) }
    catch { Remove-Item -LiteralPath $part -Force -ErrorAction SilentlyContinue; throw "Couldn't download $($Download.Url): $($_.Exception.InnerException.Message)$($_.Exception.Message)" }
    finally { $client.Dispose() }
    if (-not (Test-Sha256 $part $Download.Sha256)) {
        $got = (Get-FileHash -LiteralPath $part -Algorithm SHA256).Hash.ToLowerInvariant()
        Remove-Item -LiteralPath $part -Force
        throw "$($Download.File) from $($Download.Url) isn't the file AhShitRim expects (SHA-256 $got, expected $($Download.Sha256)), so it was deleted and not run."
    }
    Move-Item -LiteralPath $part $dest
    return $dest
}

# --- ASI loader ---------------------------------------------------------------------------------

# Puts the Ultimate ASI Loader's dinput8.dll (the 32-bit one at its release zip's root) into San
# Andreas's folder, and makes the scripts folder it loads AhShitRim.asi from. A dinput8.dll that's
# there already (another mod's) is kept as dinput8.dll.bak.
function Install-AsiLoader([string] $Zip, [string] $SaPath) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($Zip)
    try {
        $entry = $archive.Entries | Where-Object { $_.FullName -eq 'dinput8.dll' } | Select-Object -First 1
        if (-not $entry) { throw "$Zip has no dinput8.dll." }
        $dest = Join-Path $SaPath 'dinput8.dll'
        if (Test-Path -LiteralPath $dest) { Move-Item -LiteralPath $dest "$dest.bak" -Force }
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $dest, $true)
    } finally { $archive.Dispose() }
    New-Item -ItemType Directory -Force (Join-Path $SaPath 'scripts') | Out-Null
}

# --- The Skyrim part without a mod manager ------------------------------------------------------

# The release's AhShitRim-Skyrim-<version>.zip next to the launcher (the newest when there are several).
function Find-SkyrimPartZip([string] $LauncherDir) {
    Get-ChildItem -LiteralPath $LauncherDir -Filter 'AhShitRim-Skyrim-*.zip' -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime | Select-Object -Last 1 -ExpandProperty FullName
}

# Whether a path from a zip or the record stays inside Data: relative, with no '..' in it.
function Test-DataRelativePath([string] $Relative) {
    $path = $Relative -replace '/', '\'
    return $path -and -not [IO.Path]::IsPathRooted($path) -and $path -notmatch '(^|\\)\.\.(\\|$)' -and $path -notmatch ':'
}

# What a previous install recorded: @{ Zip = the zip's SHA-256; Files = @{ relative path = SHA-256 }; Dirs = folders it made }.
function Read-SkyrimPartRecord([string] $DataDir) {
    $path = Join-Path $DataDir $SkyrimPartRecord
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    $record = [pscustomobject] @{ Zip = ''; Files = [ordered] @{}; Dirs = @() }
    foreach ($line in Get-Content -LiteralPath $path -Encoding UTF8) {
        if ($line -match '^zip=([0-9A-Fa-f]{64})') { $record.Zip = $Matches[1].ToUpperInvariant() }
        elseif ($line -match '^file=([0-9A-Fa-f]{64}) (.+)$' -and (Test-DataRelativePath $Matches[2])) { $record.Files[$Matches[2]] = $Matches[1].ToUpperInvariant() }
        elseif ($line -match '^dir=(.+)$' -and (Test-DataRelativePath $Matches[1])) { $record.Dirs += $Matches[1] }
    }
    return $record
}

# Whether the Skyrim part in Data is the one in $Zip, every file still there and unchanged.
function Test-SkyrimPartCurrent([string] $Zip, [string] $DataDir) {
    $record = Read-SkyrimPartRecord $DataDir
    if (-not $record -or $record.Zip -ne (Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash) { return $false }
    foreach ($file in $record.Files.Keys) {
        if (-not (Test-Sha256 (Join-Path $DataDir $file) $record.Files[$file])) { return $false }
    }
    return $true
}

# Copies the Skyrim part's zip (its root is Skyrim's Data folder) into $DataDir and records every
# file and every folder it had to make in Data\SKSE\Plugins\AhShitRim.installed.txt. Files of an
# older install that the new zip no longer has are removed. Every name is checked before anything
# is copied, and a copy that fails halfway still records what it wrote (with no zip hash, so the
# next launch tries again), so an uninstall always finds every file.
function Install-SkyrimPart([string] $Zip, [string] $DataDir) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $old = Read-SkyrimPartRecord $DataDir
    $files = [ordered] @{}
    $dirs = [System.Collections.Generic.List[string]]::new()
    if ($old) { $dirs.AddRange([string[]] @($old.Dirs)) }
    $zipHash = (Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash
    $done = $false
    $archive = [IO.Compression.ZipFile]::OpenRead($Zip)
    try {
        $entries = @($archive.Entries | Where-Object { $_.Name })
        foreach ($entry in $entries) {
            if (-not (Test-DataRelativePath $entry.FullName)) { throw "$Zip has a file outside Skyrim's Data folder ($($entry.FullName)), so nothing was installed." }
        }
        foreach ($entry in $entries) {
            $relative = $entry.FullName -replace '/', '\'
            $dest = Join-Path $DataDir $relative
            # Each folder this makes, deepest last, so an uninstall can take the empty ones away again.
            $parts = (Split-Path $relative) -split '\\' | Where-Object { $_ }
            $sofar = ''
            foreach ($part in $parts) {
                $sofar = if ($sofar) { "$sofar\$part" } else { $part }
                if (-not (Test-Path -LiteralPath (Join-Path $DataDir $sofar))) {
                    New-Item -ItemType Directory (Join-Path $DataDir $sofar) | Out-Null
                    if (-not $dirs.Contains($sofar)) { $dirs.Add($sofar) }
                }
            }
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $dest, $true)
            $files[$relative] = (Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash
        }
        if ($old) {
            foreach ($file in $old.Files.Keys) {
                if (-not $files.Contains($file)) { Remove-Item -LiteralPath (Join-Path $DataDir $file) -Force -ErrorAction SilentlyContinue }
            }
        }
        $done = $true
    } finally {
        $archive.Dispose()
        $recorded = [ordered] @{}
        # Halfway: the older install's files are still there too, so they stay recorded.
        if (-not $done -and $old) { foreach ($file in $old.Files.Keys) { $recorded[$file] = $old.Files[$file] } }
        foreach ($file in $files.Keys) { $recorded[$file] = $files[$file] }
        if ($done -or $recorded.Count) {
            $lines = @('# AhShitRim''s Skyrim part, installed by its launcher without a mod manager. The launcher''s uninstall removes these again.')
            if ($done) { $lines += "zip=$zipHash $(Split-Path -Leaf $Zip)" }
            $lines += foreach ($file in $recorded.Keys) { "file=$($recorded[$file]) $file" }
            $lines += foreach ($dir in $dirs) { "dir=$dir" }
            $recordPath = Join-Path $DataDir $SkyrimPartRecord
            New-Item -ItemType Directory -Force (Split-Path $recordPath) | Out-Null
            [IO.File]::WriteAllLines($recordPath, [string[]] $lines, (New-Object Text.UTF8Encoding $false))
        }
    }
    return @($files.Keys)
}

# Removes what Install-SkyrimPart recorded, plus $Extra (relative to Data, such as the launcher's
# AhShitRim.ini), then the folders it made once they're empty, deepest first. Returns what it removed.
function Uninstall-SkyrimPart([string] $DataDir, [string[]] $Extra = @()) {
    $record = Read-SkyrimPartRecord $DataDir
    $removed = @()
    $targets = @(if ($record) { $record.Files.Keys }) + @($Extra | Where-Object { $_ })
    foreach ($file in $targets) {
        $path = Join-Path $DataDir $file
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force; $removed += $file }
    }
    Remove-Item -LiteralPath (Join-Path $DataDir $SkyrimPartRecord) -Force -ErrorAction SilentlyContinue
    if ($record) {
        foreach ($dir in @($record.Dirs | Sort-Object { $_.Length } -Descending)) {
            $path = Join-Path $DataDir $dir
            if ((Test-Path -LiteralPath $path) -and -not (Get-ChildItem -LiteralPath $path -Force | Select-Object -First 1)) { Remove-Item -LiteralPath $path -Force }
        }
    }
    return $removed
}

# --- Mod managers -------------------------------------------------------------------------------

# How the Skyrim part gets into Skyrim: 'None' (the launcher copies it into Data), 'MO2' or
# 'Vortex' (the player installs the zip there). Configured: ModManager in launcher.ini, which wins.
# Otherwise a Vortex deployment in Data or an MO2 instance for this Skyrim means that manager.
function Get-ModManager([string] $Configured, [bool] $HasMo2Instance, [bool] $HasVortexDeployment) {
    switch -Regex ($Configured) {
        '^\s*none\s*$' { return 'None' }
        '^\s*mo2\s*$' { return 'MO2' }
        '^\s*vortex\s*$' { return 'Vortex' }
    }
    if ($HasMo2Instance) { return 'MO2' }
    if ($HasVortexDeployment) { return 'Vortex' }
    return 'None'
}
