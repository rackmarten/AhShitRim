<#
.SYNOPSIS
    Checks Skyrim's attacks on CJ's vehicle with nobody at the keys (v31): closes both games, deploys what was built
    last, turns on the SKSE plugin's hit test ([Debug] HitTest, skse/src/debug/HitTest.h), starts San Andreas and
    Skyrim, and waits while it loads the most recent save, goes to Riverwood, hands CJ over in a Sandking and either sets
    a giant and two guards on him (hits) or gives him two stars (bust); stealth hands CJ over on foot with the knife, puts a
    guard with his back to him and clicks the mouse buttons for a stealth kill.
    Or (respawns) it hands CJ over on foot, has
    him kill a few of the people around, presses / for Skyrim's wait menu (before and after the kills), puts the clock an hour on, steps into an interior and back out, and logs who
    came back. Or (pad) it injects gamepad events as Skyrim sends them and checks each action from San Andreas's pose
    ("hit test: pad: ... ok"). Or (riders) it puts a guard beside the player as a follower, hands CJ over in the Sandking, presses F and
    traces both getting out. Or (ridecrew) CJ, on foot at the Whiterun stables, recruits two homies of the crew, gets into a Huntley
    spawned in front of him with F, and gets out again: each homie's step out is traced (no frame seated while CJ gets out, on the
    ground once let go, then in his place behind CJ). Or (ridebike) the same on a Sanchez: one homie rides its pillion and gets off with CJ. Or (oil) it goes to Bleak Falls Barrow instead and places three fresh oil pools there and lights them, by a
    bullet, a Molotov and a rocket, and logs whether each burns. Or (soak) a long run of play: the crew on foot, wolves, a drive, F11 and the clock running for -Seconds (the working set of both games every 30 s to soak-memory.csv in the temp folder). Or (drive) CJ drives the Sandking for -Seconds, his body,
    its model and the camera traced in the vehicle every frame as Skyrim draws them. Then it prints what the logs say, closes both
    games and takes the hit test out of the ini again. Loads (never saves) the most recent save, or with -NewGame starts
    a new game at the start instead (for Skyrim SE 1.5.97, which can't load the AE saves).
.EXAMPLE
    scripts/hit-test.ps1
    scripts/hit-test.ps1 -Scenario bust -Seconds 60
    scripts/hit-test.ps1 -Scenario respawns
    scripts/hit-test.ps1 -Scenario crew      # v39: the Grove Street crew at the Whiterun stables: spots, recruits, a fight, friendly fire, F10, CJ walking and running off with the crew, a move, the walk again up against Whiterun's walls
    scripts/hit-test.ps1 -Scenario oil
    scripts/hit-test.ps1 -NewGame
    scripts/hit-test.ps1 -Scenario stealth -Seconds 12   # v38: CJ's knife stealth kill on a guard, on foot
    scripts/hit-test.ps1 -Scenario stealth -StealthMeters 0.7   # the guard closer in
    scripts/hit-test.ps1 -Scenario stealth -Near 000DC9E4   # moved to that ref first, its stealth verdict logged
    scripts/hit-test.ps1 -Scenario pad   # the gamepad: Back's hold, the sticks, weapons, aim and fire, driving, the radio
    scripts/hit-test.ps1 -Scenario armour -Seconds 120   # CJ stands still with full armour: what takes any, both games log it
    scripts/hit-test.ps1 -Scenario riders   # a follower rides in the Sandking, both get out, traced every frame
    scripts/hit-test.ps1 -Scenario ridecrew # two homies of the Grove Street crew board a Huntley with CJ and step out with him, at the Whiterun stables
    scripts/hit-test.ps1 -Scenario ridebike # one homie rides a Sanchez's pillion with CJ and gets off with him, at the Whiterun stables
    scripts/hit-test.ps1 -Scenario soak -Seconds 900 -Hour 9   # a long run of play: crew on foot with wolves, a drive, F11 every 30 s, the clock running; memory of both games sampled, crashes and errors counted
    scripts/hit-test.ps1 -Scenario drive -Seconds 60   # CJ drives; he must stay in his seat as Skyrim draws him
#>
param(
    [ValidateSet('hits', 'bust', 'stealth', 'respawns', 'crew', 'pad', 'riders', 'ridecrew', 'ridebike', 'armour', 'oil', 'drive', 'soak')] [string] $Scenario = 'hits',
    [int] $Seconds = 90,
    [string] $Start = '',
    [int] $TimeoutSeconds = 300,
    [switch] $NewGame,
    [double] $StealthMeters = 1.6,
    [string] $Near = '',
    [string] $Hour = '',       # soak: the game hour it starts at
    [string] $Timescale = ''   # soak: Skyrim's timescale (60 by default: an hour of game time a minute)
)

. "$PSScriptRoot/common.ps1"

if ($Scenario -in 'crew', 'ridecrew', 'ridebike' -and -not $PSBoundParameters.ContainsKey('Start')) { $Start = 'cow Tamriel 4 -3' }  # the Whiterun stables' cell, by the gate

$config = Get-LocalConfig
$skyrim = Get-RequiredSetting $config 'SkyrimPath'
$sa = Get-RequiredSetting $config 'SaPath'
$ini = Join-Path $skyrim 'Data/SKSE/Plugins/AhShitRim.ini'
$skseLog = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games\Skyrim Special Edition\SKSE\AhShitRim.log'
$saLog = Join-Path $sa 'scripts\AhShitRim.log'

function Stop-Games {
    $running = Get-Process gta_sa, SkyrimSE, skse64_loader -ErrorAction SilentlyContinue
    if ($running) {
        $running | Stop-Process -Force
        $running | Wait-Process -Timeout 30 -ErrorAction SilentlyContinue
    }
}

if ($Scenario -eq 'soak') {
    if (-not $PSBoundParameters.ContainsKey('Start')) { $Start = 'cow Tamriel 4 -3' }
    if (-not $PSBoundParameters.ContainsKey('TimeoutSeconds')) { $TimeoutSeconds = $Seconds + 240 }
}

if (-not $Start) { $Start = if ($Scenario -eq 'oil') { 'BleakFallsBarrow01' } else { 'Riverwood' } }

Stop-Games
for ($attempt = 1; ; $attempt++) {
    try { & "$PSScriptRoot/deploy-local.ps1"; break }
    catch {
        if ($attempt -ge 10) { throw }
        Start-Sleep -Seconds 2
    }
}
Add-Content -Path $ini -Encoding ascii -Value @('', '[Debug]', "HitTest=$Scenario", "HitTestSeconds=$Seconds", "HitTestStart=$Start", "HitTestNewGame=$([int] $NewGame.IsPresent)",
    "HitTestHour=$Hour", "HitTestTimescale=$Timescale", "HitTestStealthMeters=$($StealthMeters.ToString([Globalization.CultureInfo]::InvariantCulture))", "HitTestNear=$Near")

try {
    $started = Get-Date
    & "$PSScriptRoot/launch-sa.ps1"
    Write-Host "Starting Skyrim (SKSE), hit test '$Scenario'"
    Start-Process -WorkingDirectory $skyrim (Join-Path $skyrim 'skse64_loader.exe')
    $deadline = $started.AddSeconds($TimeoutSeconds)
    $finished = $false
    $memory = Join-Path ([IO.Path]::GetTempPath()) 'soak-memory.csv'
    $nextSample = Get-Date
    if ($Scenario -eq 'soak') { Set-Content -Path $memory -Value 'time,process,workingSetMB,privateMB,handles' }
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 5
        if ($Scenario -eq 'soak' -and (Get-Date) -ge $nextSample) {
            $nextSample = (Get-Date).AddSeconds(30)
            foreach ($p in Get-Process SkyrimSE, gta_sa -ErrorAction SilentlyContinue) {
                Add-Content -Path $memory -Value ('{0},{1},{2:N0},{3:N0},{4}' -f (Get-Date -Format 's'), $p.ProcessName, ($p.WorkingSet64 / 1MB), ($p.PrivateMemorySize64 / 1MB), $p.HandleCount)
            }
        }
        if ((Test-Path $skseLog) -and (Get-Item $skseLog).LastWriteTime -gt $started -and
            (Select-String -Path $skseLog -Pattern 'hit test: (done|could not|CJ never)' -Quiet)) {
            $finished = $true
            break
        }
        if (-not (Get-Process SkyrimSE -ErrorAction SilentlyContinue) -and (Get-Date) -gt $started.AddSeconds(60)) {
            Write-Warning 'Skyrim is gone (a crash? Documents/My Games/Skyrim Special Edition/SKSE/crash-*.log)'
            break
        }
    }
    Write-Host "--- Skyrim ($skseLog)"
    if ($Scenario -eq 'soak' -and (Test-Path $memory)) {
        Write-Host "--- memory ($memory), first and last sample of each game"
        Import-Csv $memory | Group-Object process | ForEach-Object { $_.Group[0]; $_.Group[-1] } | Format-Table -AutoSize | Out-String | Write-Host
        $crashDir = Split-Path $skseLog
        Write-Host "--- crash logs since the start: $(@(Get-ChildItem $crashDir -Filter 'crash-*.log' -ErrorAction SilentlyContinue | Where-Object LastWriteTime -gt $started).Count)"
    }
    Select-String -Path $skseLog -Pattern 'hit test:|respawns:|player sync: / pressed|skyrim messages: .*help box|homies:|hitboxes:|vitals: CJ''s vehicle|police: .*(door|runs for|BUSTED|star)|car hits:|stealth kill|lock-on:|shots: knife|chatter: a stealth|riders:|vitals: San Andreas hit|object hits: .*oil|CJ re-anchored' | ForEach-Object { $_.Line }
    Write-Host "--- San Andreas ($saLog)"
    Select-String -Path $saLog -Pattern 'car hits: Skyrim|scenes: CJ (hit|has)|busted|melee: |weapons: loadout|crew: |health: ' | ForEach-Object { $_.Line }
    if ($Scenario -eq 'soak') {
        foreach ($log in $skseLog, $saLog) {
            $bad = @(Select-String -Path $log -Pattern 'error|failed|could not|link down|ring full|exception' -ErrorAction SilentlyContinue)
            Write-Host "--- $(Split-Path $log -Leaf) in $(Split-Path (Split-Path $log) -Leaf): $($bad.Count) lines with error/failed/could not/link down/ring full/exception; the first 15:"
            $bad | Select-Object -First 15 | ForEach-Object { $_.Line }
        }
    }
    if ($Scenario -eq 'crew') {
        # San Andreas's 5 s lines: how far off the way they went the moving followers faced (Crew::Facing).
        # Only windows with 30 frames or more of it: a follower stuck behind twitches on the spot for a frame or two.
        $faced = Select-String -Path $saLog -Pattern 'moving followers faced (\d+) degrees off their way on average, \d+ at most \((\d+) frames\)' |
            Where-Object { [int] $_.Matches[0].Groups[2].Value -ge 30 } | ForEach-Object { [int] $_.Matches[0].Groups[1].Value }
        if ($faced) {
            $worst = ($faced | Measure-Object -Maximum).Maximum
            Write-Host ("hit test: crew: the moving followers faced {0} degrees off their way on average in their worst 5 s of 30 frames or more (within 20): {1}" -f $worst, $(if ($worst -le 20) { 'ok' } else { 'FAILED' }))
        } else {
            Write-Host 'hit test: crew: San Andreas saw no follower move: FAILED'
        }
    }
    if (-not $finished) { throw "The hit test didn't finish within $TimeoutSeconds s." }
} finally {
    Stop-Games
    $kept = Get-Content $ini | Where-Object { $_ -notmatch '^(\[Debug\]|HitTest)' }
    Set-Content -Path $ini -Encoding ascii -Value ($kept -join "`r`n").TrimEnd()
}
