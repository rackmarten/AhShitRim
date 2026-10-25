<#
.SYNOPSIS
    Checks Skyrim's attacks on CJ's vehicle with nobody at the keys (v31): closes both games, deploys what was built
    last, turns on the SKSE plugin's hit test ([Debug] HitTest, skse/src/debug/HitTest.h), starts San Andreas and
    Skyrim, and waits while it loads the most recent save, goes to Riverwood, hands CJ over in a Sandking and either sets
    a giant and two guards on him (hits) or gives him two stars (bust). Then it prints what the logs say, closes both
    games and takes the hit test out of the ini again. Loads (never saves) the most recent save.
.EXAMPLE
    scripts/hit-test.ps1
    scripts/hit-test.ps1 -Scenario bust -Seconds 60
#>
param(
    [ValidateSet('hits', 'bust')] [string] $Scenario = 'hits',
    [int] $Seconds = 90,
    [string] $Start = 'Riverwood',
    [int] $TimeoutSeconds = 300
)

. "$PSScriptRoot/common.ps1"

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
Add-Content -Path $ini -Encoding ascii -Value @('', '[Debug]', "HitTest=$Scenario", "HitTestSeconds=$Seconds", "HitTestStart=$Start")

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
    Select-String -Path $skseLog -Pattern 'hit test:|hitboxes:|vitals: CJ''s vehicle|police: .*(door|runs for|BUSTED|star)|car hits:' | ForEach-Object { $_.Line }
    Write-Host "--- San Andreas ($saLog)"
    Select-String -Path $saLog -Pattern 'car hits: Skyrim|scenes: CJ (hit|has)|busted' | ForEach-Object { $_.Line }
    if (-not $finished) { throw "The hit test didn't finish within $TimeoutSeconds s." }
} finally {
    Stop-Games
    $kept = Get-Content $ini | Where-Object { $_ -notmatch '^(\[Debug\]|HitTest)' }
    Set-Content -Path $ini -Encoding ascii -Value ($kept -join "`r`n").TrimEnd()
}
