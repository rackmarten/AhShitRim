<#
.SYNOPSIS
    The load test: hundreds of loads, unattended and hidden, counting the crashes. Starts a test session into the first
    save (skse-test-kit, scripts/skytest.ps1: Skyrim hidden and never focused, San Andreas beside it), then runs cycle
    after cycle through the SKSE plugin's asr.load (skse/src/debug/LoadTest.cpp): the next save loaded through the
    console, the game left to settle, the player handed to San Andreas, then one scenario, in turn: idle, people scared
    (flee), a 6-star chase loaded through, ASNAEB, a coc (travel), AEZAKMI, an F10 hand-back. The saves alternate
    between StressA and StressB in Skyrim's Saves folder (copies of any two saves).
    A crash (a new crash log), Skyrim gone or a hang (no frame for -HangSeconds) is recorded with the cycle it was in and
    the plugin's last step, the session is started again, and the test goes on with the next cycle. Each run's events
    go to Documents/My Games/AhShitRim/loadtest-results.csv, every cycle's steps to loadtest.csv beside it (the one
    before is kept with its time in its name). Exit code: 0 when no cycle crashed, hung or got stuck, 1 otherwise.
    Never saves, unless -SaveAs. With -Dll this checkout's build is deployed again at the end.
.EXAMPLE
    scripts/load-test.ps1 -Cycles 70 -Label after
    scripts/load-test.ps1 -Cycles 40 -Scenarios chase -Label chase-only
    scripts/load-test.ps1 -Cycles 70 -Dll C:\builds\before\AhShitRim.dll -Label before   # a build with the kit linked
#>
param(
    [int] $Cycles = 70,
    [string] $Label = 'run',
    [string] $Saves = 'StressA,StressB',
    [string] $Scenarios = '',         # only these, e.g. "chase" or "chase,asnaeb"
    [string] $Dll = '',               # deploy this SKSE plugin instead of this checkout's last build (it must link the kit)
    [string] $Results = '',
    [int] $HangSeconds = 180,         # a load's own pauses are long: no frame for this long is a hang
    [string] $SaveAs = '',            # the last cycle saves its loaded game under this name once settled (a save cleaned of the police)
    [switch] $Keep                    # leave the session running at the end (scripts/skytest.ps1 stop ends it); not with -Dll
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$module = Join-Path $root 'skse/extern/skse-test-kit/powershell/SkyrimTest.psm1'
if (-not (Test-Path $module)) {
    throw 'skse-test-kit is not checked out: git submodule update --init --checkout skse/extern/skse-test-kit (then scripts/build.ps1).'
}
Import-Module $module -Force
$config = Get-SkytestConfig $root
if ($Dll) {
    if ($Keep) { throw '-Keep leaves the session running, and with -Dll it would run the other build as if it were this checkout''s: drop one.' }
    $Dll = (Resolve-Path $Dll).Path
    $config.Deploy = @{ Script = 'scripts/deploy-local.ps1'; Arguments = @('-SkseDll', $Dll) }
}
$folder = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games\AhShitRim'
$csv = Join-Path $folder 'loadtest.csv'
if (-not $Results) { $Results = Join-Path $folder 'loadtest-results.csv' }
$all = @('idle', 'flee', 'chase', 'asnaeb', 'travel', 'aezakmi', 'handback')
$picked = if ($Scenarios) { @($all | Where-Object { $Scenarios.Contains($_) }) } else { $all }
if (-not $picked) { throw "-Scenarios names none of $($all -join ', ')." }
$saveList = @($Saves -split ',' | ForEach-Object Trim | Where-Object { $_ })
if (-not $saveList) { throw '-Saves names no save.' }

function Record([string] $What, [int] $Cycle, [string] $Detail) {
    Add-Content -Path $Results -Value ('{0},{1},{2},{3},"{4}"' -f (Get-Date -Format 's'), $Label, $What, $Cycle, ($Detail -replace '"', "'"))
    Write-Host "$(Get-Date -Format T) cycle ${Cycle}: $What $Detail" -ForegroundColor $(if ($What -in 'crash', 'exit', 'hang', 'stuck') { 'Red' } else { 'Gray' })
}

# The plugin's last step before a crash: its log is rewritten when Skyrim starts again.
function Get-LastStep {
    $line = Select-String -Path $config.Log -Pattern 'load test: ' -ErrorAction SilentlyContinue | Select-Object -Last 1
    if ($line) { return ($line.Line -replace '^.*load test: ', '') }
    return '(no load test line in the log)'
}

$script:connection = $null
$script:watch = $null
# A session into the first save, tried again (a start can crash too: it loads a save) up to 5 times; each failure is
# recorded against the cycle that comes next.
function Open-Session([int] $Cycle) {
    Disconnect-Skytest $script:connection
    $script:connection = $null
    for ($try = 1; ; $try++) {
        try {
            $started = Start-SkyrimTest -Config $config -Start "save:$($saveList[0])" -HangSeconds $HangSeconds
            Write-Host "Session up: Skyrim ready $($started.Timings.launchToReady) s after its launch."
            $script:connection = Connect-Skytest $config
            $script:watch = New-SessionWatch $config -HangSeconds $HangSeconds
            return
        } catch {
            $counts.start++
            Record 'start' $Cycle "try $try`: $($_.Exception.Message)"
            if ($try -ge 5) { throw }
            Start-Sleep -Seconds (5 * $try)
        }
    }
}

New-Item -ItemType Directory -Force $folder | Out-Null
if (-not (Test-Path $Results)) { Add-Content -Path $Results -Value 'time,label,event,cycle,detail' }
if (Test-Path $csv) { Move-Item -Force $csv (Join-Path $folder "loadtest-$(Get-Date -Format 'yyyyMMdd-HHmmss').csv") }

$counts = [ordered]@{ ok = 0; crash = 0; exit = 0; hang = 0; stuck = 0; start = 0 }
$cycle = 0
$clock = [Diagnostics.Stopwatch]::StartNew()
Record 'start' 0 "$(if ($Dll) { $Dll } else { 'this checkout''s build' }), $Cycles cycles$(if ($Scenarios) { ", only $($picked -join ', ')" })"
try {
    Open-Session 1
    for ($cycle = 1; $cycle -le $Cycles; $cycle++) {
        $save = $saveList[$cycle % $saveList.Count]
        $scenario = $picked[($cycle - 1) % $picked.Count]
        Update-GamesLock $config  # an hours-long run keeps the lock fresh
        $answer = $null
        $problem = $null
        $request = "asr.load $save $scenario$(if ($SaveAs -and $cycle -eq $Cycles) { " saveAs=$SaveAs" })"
        try { $answer = Invoke-SkytestRequest $script:connection $request 300 $script:watch }
        catch {
            $kind = if ($_.Exception.Data.Contains('Fault')) { $_.Exception.Data['Fault'] } else { 'stuck' }
            $problem = @{ Kind = $kind; Detail = "$($_.Exception.Message); last step: $(Get-LastStep)" }
        }
        if ($answer) {
            foreach ($row in @($(if ($answer.Response.PSObject.Properties['rows']) { $answer.Response.rows }))) {
                Add-Content -Path $csv -Value ('{0},{1},{2},{3},{4}' -f $row.time, $cycle, $save, $scenario, $row.what)
            }
            if ($answer.Response.ok) { $counts.ok++ }
            else { $problem = @{ Kind = 'stuck'; Detail = "$($answer.Response.error); last step: $(Get-LastStep)" } }  # the kit's timeout
        }
        if ($problem) {
            $counts[$problem.Kind]++
            Record $problem.Kind $cycle "$save $scenario`: $($problem.Detail)"
            if ($cycle -lt $Cycles) { Open-Session ($cycle + 1) }  # closes this session's own Skyrim first
        }
    }
} finally {
    Disconnect-Skytest $script:connection
    $done = [math]::Min($cycle, $Cycles)
    Record 'end' $done ("{0} cycles in {1:n0} min: {2} ok, {3} crash(es), {4} exit(s), {5} hang(s), {6} stuck, {7} failed start(s)" -f $done,
        $clock.Elapsed.TotalMinutes, $counts.ok, $counts.crash, $counts.exit, $counts.hang, $counts.stuck, $counts.start)
    if (-not $Keep) { Stop-SkyrimTest -Config $config }
    if ($Dll) { & (Join-Path $PSScriptRoot 'deploy-local.ps1') | Out-Null; Write-Host 'Deployed this checkout''s build again.' }
}
if ($counts.crash + $counts.exit + $counts.hang + $counts.stuck + $counts.start) { exit 1 }
