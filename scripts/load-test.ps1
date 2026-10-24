<#
.SYNOPSIS
    The load test: hundreds of loads, unattended, counting the crashes. Deploys this checkout's last build (or -Dll),
    adds [Debug] LoadTest to AhShitRim.ini, starts both games and watches them. The SKSE plugin's load test
    (skse/src/debug/LoadTest.cpp) alternates loads between two saves (StressA and StressB in Skyrim's Saves folder,
    copies of any two saves) and runs one scenario per cycle: idle, people scared, a 6-star chase loaded through,
    ASNAEB, a coc, AEZAKMI, an F10 hand-back. A new crash-*.log, a hang (the plugin log still for 180 s) or Skyrim gone
    is recorded with the cycle it was in, the games are started again, and the plugin goes on with the next cycle.
    Each run's events go to Documents/My Games/AhShitRim/loadtest-results.csv, each cycle's steps to loadtest.csv.
    At the end the ini is deployed afresh, without the [Debug] lines.
.EXAMPLE
    scripts/load-test.ps1 -Cycles 70 -Label after
    scripts/load-test.ps1 -Cycles 40 -Scenarios chase -Label chase-only
#>
param(
    [int] $Cycles = 70,
    [string] $Label = 'run',
    [string] $Saves = 'StressA,StressB',
    [string] $Scenarios = '',         # LoadTestScenarios: only these, e.g. "chase"
    [string] $Dll = '',               # deploy this SKSE plugin instead of this checkout's last build
    [string[]] $IniExtra = @(),       # more lines under [Debug]
    [string] $Results = '',
    [int] $HangSeconds = 180
)

. "$PSScriptRoot/common.ps1"
$ErrorActionPreference = 'Stop'
$config = Get-LocalConfig
$skyrim = Get-RequiredSetting $config 'SkyrimPath'
$docs = [Environment]::GetFolderPath('MyDocuments')
$skse = Join-Path $docs 'My Games\Skyrim Special Edition\SKSE'
$log = Join-Path $skse 'AhShitRim.log'
$folder = Join-Path $docs 'My Games\AhShitRim'
$progress = Join-Path $folder 'loadtest-progress.txt'
$csv = Join-Path $folder 'loadtest.csv'
$ini = Join-Path $skyrim 'Data/SKSE/Plugins/AhShitRim.ini'
if (-not $Results) { $Results = Join-Path $folder 'loadtest-results.csv' }

function Record([string] $what, [string] $detail) {
    $cycle = if (Test-Path $progress) { (Get-Content $progress -First 1) } else { '0' }
    Add-Content -Path $Results -Value ('{0},{1},{2},{3},"{4}"' -f (Get-Date -Format 's'), $Label, $what, $cycle, $detail)
    Write-Host "$(Get-Date -Format T) cycle ${cycle}: $what $detail"
}
function Stop-Games { Get-Process SkyrimSE, skse64_loader, gta_sa -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep -Seconds 5 }
function Start-Games {
    & "$PSScriptRoot/launch-sa.ps1" | Out-Null
    Start-Process -WorkingDirectory $skyrim (Join-Path $skyrim 'skse64_loader.exe')
}
function Done {
    (Test-Path $progress) -and [int](Get-Content $progress -First 1) -ge $Cycles -and
        (Select-String -Path $log -Pattern 'load test: (all \d+ cycles done|on, but all \d+ cycles are done)' -Quiet -ErrorAction SilentlyContinue)
}

New-Item -ItemType Directory -Force $folder | Out-Null
if (-not (Test-Path $Results)) { Add-Content -Path $Results -Value 'time,label,event,cycle,detail' }
Set-Content -Path $progress -Value '0'
if (Test-Path $csv) { Move-Item -Force $csv (Join-Path $folder "loadtest-$(Get-Date -Format 'yyyyMMdd-HHmmss').csv") }

Stop-Games
& "$PSScriptRoot/deploy-local.ps1" | Out-Null
if ($Dll) {
    Copy-Item -Force $Dll (Join-Path $skyrim 'Data/SKSE/Plugins/AhShitRim.dll')
    Remove-Item -ErrorAction SilentlyContinue (Join-Path $skyrim 'Data/SKSE/Plugins/AhShitRim.pdb')
}
Add-Content -Path $ini -Value @('', '[Debug]', "LoadTest=$Cycles", "LoadTestSaves=$Saves")
if ($Scenarios) { Add-Content -Path $ini -Value "LoadTestScenarios=$Scenarios" }
if ($IniExtra) { Add-Content -Path $ini -Value $IniExtra }
Record 'start' "$(if ($Dll) { $Dll } else { 'this checkout''s build' }), $Cycles cycles$(if ($Scenarios) { ", only $Scenarios" })"

$known = @(Get-ChildItem $skse -Filter 'crash-*.log' | ForEach-Object Name)
$crashes = 0
$hangs = 0
$deadline = (Get-Date).AddHours(10)
while (-not (Done) -and (Get-Date) -lt $deadline) {
    Start-Games
    $since = Get-Date
    while ($true) {
        Start-Sleep -Seconds 5
        $new = Get-ChildItem $skse -Filter 'crash-*.log' | Where-Object { $known -notcontains $_.Name }
        if ($new) {
            $crashes++
            foreach ($c in $new) {
                $known += $c.Name
                $where = (Select-String -Path $c.FullName -Pattern 'Unhandled exception' | Select-Object -First 1).Line
                Record 'crash' "$($c.Name): $where"
            }
            break
        }
        if (-not (Get-Process SkyrimSE -ErrorAction SilentlyContinue)) {
            if (Done) { break }
            $crashes++
            Record 'gone' 'Skyrim exited without a crash log'
            break
        }
        $age = if (Test-Path $log) { ((Get-Date) - (Get-Item $log).LastWriteTime).TotalSeconds } else { 0 }
        if (((Get-Date) - $since).TotalSeconds -gt 120 -and $age -gt $HangSeconds) {
            $hangs++
            Record 'hang' "the plugin log still for $([int]$age) s"
            break
        }
        if (Done) { break }
    }
    Stop-Games
}

Record 'end' "$([int](Get-Content $progress -First 1)) cycles, $crashes crash(es), $hangs hang(s)"
& "$PSScriptRoot/deploy-local.ps1" | Out-Null   # the ini without the [Debug] lines
