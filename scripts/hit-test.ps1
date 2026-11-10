<#
.SYNOPSIS
    Checks Skyrim's attacks on CJ's vehicle with nobody at the keys (v31): closes both games, deploys what was built
    last, turns on the SKSE plugin's hit test ([Debug] HitTest, skse/src/debug/HitTest.h), starts San Andreas and
    Skyrim, and waits while it loads the most recent save, goes to Riverwood, hands CJ over in a Sandking and either sets
    a giant and two guards on him (hits) or gives him two stars (bust); stealth hands CJ over on foot with the knife, puts a
    guard with his back to him and clicks the mouse buttons for a stealth kill.
    Or (respawns) it hands CJ over on foot, has
    him kill a few of the people around, puts the clock an hour on, steps into an interior and back out, and logs who
    came back. Then it prints what the logs say, closes both
    games and takes the hit test out of the ini again. Loads (never saves) the most recent save, or with -NewGame starts
    a new game at the start instead (for Skyrim SE 1.5.97, which can't load the AE saves).
.EXAMPLE
    scripts/hit-test.ps1
    scripts/hit-test.ps1 -Scenario bust -Seconds 60
    scripts/hit-test.ps1 -Scenario respawns
    scripts/hit-test.ps1 -Scenario crew      # v39: the Grove Street crew at the Whiterun stables: spots, recruits, a fight the elk CJ shot stays out of, friendly fire, HESOYAM and H, F10, CJ walking and running off with the crew, a move, the walk again up against Whiterun's walls
    scripts/hit-test.ps1 -NewGame
    scripts/hit-test.ps1 -Scenario stealth -Seconds 12   # v38: CJ's knife stealth kill on a guard, on foot
#>
param(
    [ValidateSet('hits', 'bust', 'stealth', 'respawns', 'crew')] [string] $Scenario = 'hits',
    [int] $Seconds = 90,
    [string] $Start = 'Riverwood',
    [int] $TimeoutSeconds = 300,
    [switch] $NewGame
)

. "$PSScriptRoot/common.ps1"

if ($Scenario -eq 'crew' -and -not $PSBoundParameters.ContainsKey('Start')) { $Start = 'cow Tamriel 4 -3' }  # the Whiterun stables' cell, by the gate

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

Stop-Games
for ($attempt = 1; ; $attempt++) {
    try { & "$PSScriptRoot/deploy-local.ps1"; break }
    catch {
        if ($attempt -ge 10) { throw }
        Start-Sleep -Seconds 2
    }
}
Add-Content -Path $ini -Encoding ascii -Value @('', '[Debug]', "HitTest=$Scenario", "HitTestSeconds=$Seconds", "HitTestStart=$Start", "HitTestNewGame=$([int] $NewGame.IsPresent)")

try {
    $started = Get-Date
    & "$PSScriptRoot/launch-sa.ps1"
    Write-Host "Starting Skyrim (SKSE), hit test '$Scenario'"
    Start-Process -WorkingDirectory $skyrim (Join-Path $skyrim 'skse64_loader.exe')
    $deadline = $started.AddSeconds($TimeoutSeconds)
    $finished = $false
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 5
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
    Select-String -Path $skseLog -Pattern 'hit test:|respawns:|homies:|hitboxes:|vitals: CJ''s vehicle|police: .*(door|runs for|BUSTED|star)|car hits:|stealth kill|lock-on:|shots: knife|chatter: a stealth' | ForEach-Object { $_.Line }
    Write-Host "--- San Andreas ($saLog)"
    Select-String -Path $saLog -Pattern 'car hits: Skyrim|scenes: CJ (hit|has)|busted|melee: |weapons: loadout|crew: ' | ForEach-Object { $_.Line }
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
        # And whether their feet kept to the ground: the metres they went against the metres their walk or run covered
        # as San Andreas played it (its time through its cycle times its pace), in windows of 30 frames and 3 m or more;
        # and how often they changed gait (a gait flipping back and forth looks as bad as a slide).
        $paced = Select-String -Path $saLog -Pattern 'moving followers went ([\d.]+) m on the ground and their steps covered ([\d.]+) m \((\d+) frames, (\d+) gait changes\)' |
            ForEach-Object { $_.Matches[0] } | Where-Object { [int] $_.Groups[3].Value -ge 30 -and [double] $_.Groups[1].Value -ge 3 }
        if ($paced) {
            $off = ($paced | ForEach-Object { [math]::Abs([double] $_.Groups[2].Value - [double] $_.Groups[1].Value) / [double] $_.Groups[1].Value } | Measure-Object -Maximum).Maximum
            $changes = ($paced | ForEach-Object { [int] $_.Groups[4].Value } | Measure-Object -Maximum).Maximum
            Write-Host ("hit test: crew: the moving followers' steps were {0:P0} off the ground they went in their worst 5 s (within 15%), and they changed gait {1} times at most in 5 s (within 8): {2}" -f $off, $changes, $(if ($off -le 0.15 -and $changes -le 8) { 'ok' } else { 'FAILED' }))
        } else {
            Write-Host 'hit test: crew: San Andreas paced no follower: FAILED'
        }
    }
    if (-not $finished) { throw "The hit test didn't finish within $TimeoutSeconds s." }
} finally {
    Stop-Games
    $kept = Get-Content $ini | Where-Object { $_ -notmatch '^(\[Debug\]|HitTest)' }
    Set-Content -Path $ini -Encoding ascii -Value ($kept -join "`r`n").TrimEnd()
}
