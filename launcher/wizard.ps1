<#
.SYNOPSIS
    The launcher's first-run setup, as dialogs: find San Andreas, copy it so the original stays
    untouched, download and run the downgrader on the copy, add an ASI loader, and later ask how
    the Skyrim part goes in. The work itself is in install.ps1 and checks.ps1; dot-source those
    first. AhShitRim.ps1 runs Invoke-SaSetup whenever launcher.ini has no usable SaPath.
#>

Add-Type -AssemblyName System.Windows.Forms

# A message box in front of the console window. Returns the button pressed ('OK', 'Yes', 'No', 'Cancel', 'Retry').
function Show-Message([string] $Text, [string] $Buttons = 'OK', [string] $Icon = 'Information') {
    $owner = New-Object System.Windows.Forms.Form -Property @{ TopMost = $true; ShowInTaskbar = $false }
    try { return [string] [System.Windows.Forms.MessageBox]::Show($owner, $Text, 'AhShitRim setup', $Buttons, $Icon) }
    finally { $owner.Dispose() }
}

function Select-Folder([string] $Description, [string] $Start) {
    $owner = New-Object System.Windows.Forms.Form -Property @{ TopMost = $true; ShowInTaskbar = $false }
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog -Property @{ Description = $Description; ShowNewFolderButton = $true }
    if ($Start -and (Test-Path -LiteralPath $Start)) { $dialog.SelectedPath = $Start }
    try { if ($dialog.ShowDialog($owner) -eq 'OK') { return $dialog.SelectedPath } else { return $null } }
    finally { $dialog.Dispose(); $owner.Dispose() }
}

# gta_sa.exe's version in $SaPath ($null when there's no gta_sa.exe or it's one we don't know).
function Get-SaFolderVersion([string] $SaPath) {
    $exe = Join-Path $SaPath 'gta_sa.exe'
    if (-not (Test-Path -LiteralPath $exe)) { return $null }
    return Get-SaExeVersion (Get-FileHash -LiteralPath $exe -Algorithm MD5).Hash
}

function Get-SaFolderAsiLoader([string] $SaPath) {
    Get-AsiLoader @(Get-ChildItem -LiteralPath $SaPath -File -Filter *.dll | ForEach-Object { @{ Name = $_.Name; Description = $_.VersionInfo.FileDescription } })
}

function Confirm-Download($Download) {
    return (Show-Message "Download $($Download.Name) ($(Format-Size $Download.Size)) from $($Download.Source)?`n`nThe launcher checks the file's SHA-256 before it runs anything, and keeps it in the Tools folder next to the launcher.`n`nVirusTotal's report on this exact file:`n$(Get-VirusTotalUrl $Download.Sha256)" 'YesNo' 'Question') -eq 'Yes'
}

# Picks the San Andreas folder to start from: a found one the player confirms, or one they pick.
function Select-SaSource($Installs) {
    foreach ($install in @($Installs)) {
        $what = if ($install.Exe -eq 'gta-sa.exe') { 'the current Steam version' } else { $v = Get-SaFolderVersion $install.Path; if ($v) { $v.Name } else { 'an unknown version' } }
        $answer = Show-Message "Found San Andreas ($what) in:`n`n$($install.Path)`n`nSet AhShitRim up from this one?" 'YesNoCancel' 'Question'
        if ($answer -eq 'Yes') { return $install.Path }
        if ($answer -eq 'Cancel') { return $null }
    }
    while ($true) {
        Show-Message "Pick your San Andreas folder: the one with gta_sa.exe (or gta-sa.exe for Steam's).`n`nThe classic game only. The Definitive Edition won't work." | Out-Null
        $picked = Select-Folder 'San Andreas folder (with gta_sa.exe or gta-sa.exe)' ${env:ProgramFiles(x86)}
        if (-not $picked) { return $null }
        if (Select-SaInstalls @($picked)) { return $picked }
        if ((Show-Message "$picked has neither gta_sa.exe nor gta-sa.exe. Pick another folder?" 'RetryCancel' 'Warning') -ne 'Retry') { return $null }
    }
}

# Offers a copy of $Source somewhere the player picks. Returns the folder to set up: the copy, or
# $Source when the player prefers to downgrade it in place; $null when they cancel.
function Invoke-SaCopy([string] $Source) {
    $bytes = Get-FolderSize $Source
    $answer = Show-Message "Make a separate copy of San Andreas for AhShitRim? (Recommended)`n`nYour original install stays as it is, and Steam can't undo the downgrade on the copy. It needs $(Format-Size $bytes) of free space.`n`nYes: pick where the copy goes.`nNo: downgrade $Source itself." 'YesNoCancel' 'Question'
    if ($answer -eq 'Cancel') { return $null }
    if ($answer -eq 'No') { return $Source }
    while ($true) {
        $picked = Select-Folder 'Where should the copy of San Andreas go? An empty folder, or one to make "San Andreas (AhShitRim)" in.' "$([IO.Path]::GetPathRoot($Source))"
        if (-not $picked) { return $null }
        $dest = Get-SaCopyDestination $picked
        $problem = Get-SaCopyProblem $Source $dest $bytes (Get-FreeBytes $dest)
        if (-not $problem) { break }
        if ((Show-Message $problem 'RetryCancel' 'Warning') -ne 'Retry') { return $null }
    }
    Write-Host "Copying San Andreas to $dest ($(Format-Size $bytes), a few minutes)"
    try { Copy-GameFolder $Source $dest }
    catch { Show-Message "Copying San Andreas failed: $($_.Exception.Message)" 'OK' 'Error' | Out-Null; return $null }
    Write-Host "Copied San Andreas to $dest"
    return $dest
}

# Downloads and runs the downgrader until $SaPath has a 1.0 US gta_sa.exe. $true when it has one.
function Invoke-SaDowngrade([string] $SaPath, [string] $ToolsDir) {
    while ($true) {
        $version = Get-SaFolderVersion $SaPath
        if ($version -and $version.Supported) { Write-Host "San Andreas in $SaPath is $($version.Name)."; return $true }
        $what = if ($version) { $version.Name } elseif (Test-Path -LiteralPath (Join-Path $SaPath 'gta_sa.exe')) { 'an exe AhShitRim doesn''t know' } else { 'the current Steam version' }
        Show-Message "San Andreas in $SaPath is $what. AhShitRim runs on 1.0 US only, so it gets downgraded once with the open-source downgrader." | Out-Null
        $download = $PinnedDownloads.Downgrader
        if (-not (Confirm-Download $download)) { return $false }
        try { Write-Host "Downloading $($download.Name) ($(Format-Size $download.Size))"; $exe = Save-VerifiedDownload $download (Join-Path $ToolsDir 'downgrader') }
        catch { Show-Message $_.Exception.Message 'OK' 'Error' | Out-Null; return $false }
        Set-Clipboard -Value $SaPath
        Show-Message ("The downgrader opens next. In it:`n`n" +
            "1. Choose this folder as the game folder (it's on the clipboard, paste it with Ctrl+V):`n   $SaPath`n" +
            "2. Downgrade it to 1.0 US. If it asks to download its patches, say yes.`n" +
            "3. Optional: install SilentPatch from its mod list. Leave the other mods out, AhShitRim isn't tested with them.`n" +
            "4. Close the downgrader. Setup carries on from there.") | Out-Null
        Write-Host 'Waiting for the downgrader to close'
        try { Start-Process -Wait -FilePath $exe -WorkingDirectory (Split-Path $exe) }
        catch { Show-Message "Couldn't start the downgrader: $($_.Exception.Message)" 'OK' 'Error' | Out-Null; return $false }
        $version = Get-SaFolderVersion $SaPath
        if ($version -and $version.Supported) { continue }
        if ((Show-Message "gta_sa.exe in $SaPath still isn't 1.0 US. Run the downgrader again?" 'RetryCancel' 'Warning') -ne 'Retry') { return $false }
    }
}

# Installs the Ultimate ASI Loader into $SaPath unless it has an ASI loader already. $true when it has one.
function Invoke-AsiLoaderSetup([string] $SaPath, [string] $ToolsDir) {
    $loader = Get-SaFolderAsiLoader $SaPath
    if ($loader) { Write-Host "San Andreas has $loader."; return $true }
    Show-Message 'San Andreas needs an ASI loader to load AhShitRim.' | Out-Null
    $download = $PinnedDownloads.AsiLoader
    if (-not (Confirm-Download $download)) { return $false }
    try {
        Write-Host "Downloading $($download.Name)"
        Install-AsiLoader (Save-VerifiedDownload $download (Join-Path $ToolsDir 'asi-loader')) $SaPath
    } catch { Show-Message $_.Exception.Message 'OK' 'Error' | Out-Null; return $false }
    $loader = Get-SaFolderAsiLoader $SaPath
    if (-not $loader) { Show-Message "The ASI loader didn't install into $SaPath." 'OK' 'Error' | Out-Null; return $false }
    Write-Host "Installed $loader into $SaPath"
    return $true
}

# The whole San Andreas setup. Returns the ready folder, or $null when the player stops.
# $OnFolder gets the folder as soon as it's chosen (or copied), for launcher.ini.
function Invoke-SaSetup($SteamLibraries, [string] $ToolsDir, [scriptblock] $OnFolder) {
    $start = Show-Message ("AhShitRim needs San Andreas 1.0 US with an ASI loader. Setup does it in a few clicks:`n`n" +
        "1. Finds your San Andreas (or you pick it).`n" +
        "2. Copies it to a folder of your choice, so your original stays untouched.`n" +
        "3. Downloads the open-source downgrader and you run it on the copy.`n" +
        "4. Adds an ASI loader.`n`nStart?") 'OKCancel' 'Information'
    if ($start -ne 'OK') { return $null }
    $installs = @(Select-SaInstalls (Get-SaFolderGuesses $SteamLibraries))
    # One already set up (a 1.0 US gta_sa.exe) comes first.
    $installs = @($installs | Sort-Object { -not ($_.Exe -eq 'gta_sa.exe' -and (Get-SaFolderVersion $_.Path).Supported) })
    $source = Select-SaSource $installs
    if (-not $source) { return $null }
    $sa = $source
    $version = Get-SaFolderVersion $source
    # A gta_sa.exe AhShitRim doesn't know may still be a 1.0 US one (the launcher only warns about it).
    $unknown = -not $version -and (Test-Path -LiteralPath (Join-Path $source 'gta_sa.exe'))
    if ($unknown -and (Show-Message "gta_sa.exe in $source isn't one AhShitRim knows. Use it as it is?`n`nYes: use it (if San Andreas's AhShitRim.log later says its version isn't supported, run setup again).`nNo: copy and downgrade it." 'YesNo' 'Question') -eq 'Yes') {
        $version = [pscustomobject] @{ Supported = $true }
    }
    if (-not ($version -and $version.Supported)) {
        $sa = Invoke-SaCopy $source
        if (-not $sa) { return $null }
        if ($OnFolder) { & $OnFolder $sa }
        if (-not (Invoke-SaDowngrade $sa $ToolsDir)) { return $null }
    } elseif ($OnFolder) { & $OnFolder $sa }
    if (-not (Invoke-AsiLoaderSetup $sa $ToolsDir)) { return $null }
    return $sa
}

# How the Skyrim part goes in, asked once when a mod manager is about and AhShitRim isn't in it
# yet. Returns 'None' (the launcher copies it into Data) or $Detected ('MO2' or 'Vortex').
function Select-ModManager([string] $Detected) {
    $name = if ($Detected -eq 'MO2') { 'Mod Organizer 2' } else { 'Vortex' }
    $answer = Show-Message ("You have $name for this Skyrim. How should AhShitRim's Skyrim part go in?`n`n" +
        "Yes: you install AhShitRim-Skyrim-*.zip with $name yourself (docs/INSTALL.md, step 3), then run the launcher again.`n" +
        "No: the launcher copies it straight into Skyrim's Data folder, and can uninstall it again (Uninstall AhShitRim.bat).") 'YesNo' 'Question'
    if ($answer -eq 'Yes') { return $Detected } else { return 'None' }
}
