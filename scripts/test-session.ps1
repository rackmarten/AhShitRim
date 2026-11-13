<#
.SYNOPSIS
    Helpers for test sessions (scripts/test-deploy.ps1): keeping the games' files while a session deploys over them,
    and putting them back. File work only, no games, so scripts/test.ps1 covers them (scripts/tests). Dot-source it.
    Runs on Windows PowerShell 5.1 too.
#>

# The list of kept files in $Store (list.json): one entry per path, with absent (wasn't there) or moved (set aside), and
# the copy's file name.
function Read-SessionList([string] $Store) {
    $file = Join-Path $Store 'list.json'
    if (-not (Test-Path -LiteralPath $file)) { return $null }
    # ForEach-Object unrolls the array: Windows PowerShell 5.1's ConvertFrom-Json hands it on as one object.
    return @(Get-Content -Raw -LiteralPath $file | ConvertFrom-Json | ForEach-Object { $_ })
}

function Write-SessionList([string] $Store, $List) {
    [IO.File]::WriteAllText((Join-Path $Store 'list.json'), (ConvertTo-Json -InputObject @($List) -Depth 3), (New-Object Text.UTF8Encoding $false))
}

function Get-FileStamp([string] $Path) {
    if (Test-Path -LiteralPath $Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash } else { '' }
}

# Before a deploy: copies each of $Keep (files the deploy overwrites) into $Store, notes those that aren't there, and
# moves each of $Move that's there (another mod's files) into $Store; the list is written after each, so it always says
# where everything is. $Dirs are folders the deploy may create: the ones not there yet are noted, so a restore removes
# them again once they're empty. A second deploy of the same session keeps the first copies (those are the originals)
# and only sets aside what came back. It refuses when the games' files changed since the deploy before (a session that
# was never stopped, and a new AhShitRim installed meanwhile): restoring then would put old files over new ones.
# $true when it started a new list.
function Save-SessionFiles([string] $Store, [string[]] $Keep = @(), [string[]] $Move = @(), [string[]] $Dirs = @()) {
    $list = Read-SessionList $Store
    $new = -not $list
    if ($new) {
        $list = @()
        New-Item -ItemType Directory -Force $Store | Out-Null
        # Folders the deploy will create: noted only by the first deploy, which sees the games as they were.
        $absentDirs = @($Dirs | Where-Object { $_ -and -not (Test-Path -LiteralPath $_) })
        [IO.File]::WriteAllText((Join-Path $Store 'dirs.json'), (ConvertTo-Json -InputObject @($absentDirs)), (New-Object Text.UTF8Encoding $false))
    } else {
        $changed = @(Get-ChangedSinceDeploy $Store)
        if ($changed) {
            throw "The games' files changed since an earlier test session deployed (it was never stopped): $($changed -join ', '). Look at them, then run scripts/test-deploy.ps1 restore (puts back what was there before that session) or scripts/test-deploy.ps1 forget (keeps them as they are now)."
        }
    }
    $n = @($list).Count
    foreach ($path in @($Keep) + @($Move)) {
        if (-not $path) { continue }
        $moved = @($Move) -contains $path
        $entry = @($list | Where-Object { $_.path -eq $path }) | Select-Object -First 1
        if ($entry) {
            # Set aside before, and there again: someone put it back meanwhile, so the kept copy may be older.
            if ($moved -and (Test-Path -LiteralPath $path)) { throw "$path was set aside by this test session and is back: look at it, then scripts/test-deploy.ps1 restore or forget." }
            continue
        }
        if ($moved -and -not (Test-Path -LiteralPath $path)) { continue }  # not there: nothing to set aside
        $entry = [pscustomobject]@{ path = $path; absent = -not (Test-Path -LiteralPath $path); moved = $moved; copy = $null }
        if (-not $entry.absent) {
            $entry.copy = "{0:D2}-{1}" -f $n++, (Split-Path -Leaf $path)
            $target = Join-Path $Store $entry.copy
            if ($moved) {
                Copy-Item -LiteralPath $path $target  # the list names it before the original goes
                $list = @($list) + $entry
                Write-SessionList $Store $list
                Remove-Item -LiteralPath $path -Force
                continue
            }
            Copy-Item -LiteralPath $path $target
        }
        $list = @($list) + $entry
        Write-SessionList $Store $list
    }
    if ($new -and -not @($list).Count) { Write-SessionList $Store @() }
    return $new
}

# After a deploy: what it wrote over the kept paths, so a later deploy or restore can tell whether anyone changed them.
# $Skip: kept paths the games write themselves while they run, which aren't watched.
function Save-DeployStamps([string] $Store, [string[]] $Skip = @()) {
    $stamps = [ordered]@{}
    foreach ($entry in @(Read-SessionList $Store)) {
        if (-not $entry.moved -and @($Skip) -notcontains $entry.path) { $stamps[$entry.path] = Get-FileStamp $entry.path }
    }
    [IO.File]::WriteAllText((Join-Path $Store 'deployed.json'), (ConvertTo-Json -InputObject $stamps), (New-Object Text.UTF8Encoding $false))
}

# The kept paths whose file is neither what the session's deploy wrote there nor already what a restore puts back (a
# restore that stopped halfway, a file still locked, may go on).
function Get-ChangedSinceDeploy([string] $Store) {
    $file = Join-Path $Store 'deployed.json'
    if (-not (Test-Path -LiteralPath $file)) { return @() }
    $stamps = Get-Content -Raw -LiteralPath $file | ConvertFrom-Json
    $list = @(Read-SessionList $Store)
    foreach ($property in $stamps.PSObject.Properties) {
        $now = Get-FileStamp $property.Name
        if ($now -eq $property.Value) { continue }
        $entry = @($list | Where-Object { $_.path -eq $property.Name }) | Select-Object -First 1
        $restored = if (-not $entry) { $false } elseif ($entry.absent) { $now -eq '' } else { $now -eq (Get-FileStamp (Join-Path $Store $entry.copy)) }
        if (-not $restored) { $property.Name }
    }
}

# Puts every file in $Store's list back where it was, deletes the ones that weren't there, removes the folders the
# deploy created once they're empty, then $Store. The paths it put back or deleted; none without a list. Refuses,
# keeping everything, when a file changed since the deploy (-Force: puts them back all the same).
function Restore-SessionFiles([string] $Store, [switch] $Force) {
    if (-not (Test-Path -LiteralPath (Join-Path $Store 'list.json'))) { return @() }
    $list = @(Read-SessionList $Store)
    $changed = @(Get-ChangedSinceDeploy $Store)
    if ($changed -and -not $Force) {
        throw "Not put back: $($changed -join ', ') changed since the test session deployed (a new AhShitRim installed?). Look at them, then scripts/test-deploy.ps1 restore-anyway or forget."
    }
    $done = @()
    foreach ($entry in $list) {
        if ($entry.absent) {
            if (Test-Path -LiteralPath $entry.path) { Remove-Item -LiteralPath $entry.path -Force; $done += $entry.path }
            continue
        }
        $copy = Join-Path $Store $entry.copy
        if (-not (Test-Path -LiteralPath $copy)) { throw "The kept copy of $($entry.path) is missing ($copy): left as it is." }
        New-Item -ItemType Directory -Force (Split-Path $entry.path) | Out-Null
        Copy-Item -LiteralPath $copy $entry.path -Force
        $done += $entry.path
    }
    $dirsFile = Join-Path $Store 'dirs.json'
    if (Test-Path -LiteralPath $dirsFile) {
        # Deepest first, so a folder the deploy made inside another it made goes before its parent.
        $dirs = @(Get-Content -Raw -LiteralPath $dirsFile | ConvertFrom-Json | ForEach-Object { $_ } | Sort-Object Length -Descending)
        foreach ($dir in $dirs) {
            if ((Test-Path -LiteralPath $dir) -and -not (Get-ChildItem -LiteralPath $dir -Force | Select-Object -First 1)) { Remove-Item -LiteralPath $dir -Force }
        }
    }
    Remove-Item -LiteralPath $Store -Recurse -Force
    return $done
}
