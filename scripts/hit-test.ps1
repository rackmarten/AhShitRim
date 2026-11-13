<#
.SYNOPSIS
    The hit test on the test kit, hidden and never focused: Skyrim's attacks on CJ's vehicle with nobody at the keys
    (v31). Makes sure a test session runs (scripts/skytest.ps1 session: Riverwood with CJ there, San Andreas beside
    it; one already up for this build is reused), then runs scenarios/hits.json (CJ in a Sandking, a giant and two guards
    set on him; the car must take the blows) or scenarios/bust.json (two stars; a guard must run to the door and bust
    him) with skytest run, and prints the log lines that tell the story. Results in out/<time>-<scenario>/: results.csv
    (a row per check), step-<n>-rows.csv (the car's and the player's health every 2 s), summary.json. A session this
    script started is stopped at the end (-Keep leaves it up); one it found running is left running.
    Exit code: 0 passed, 1 a check failed, 2 Skyrim crashed, exited or hung (the crash log is named).
.EXAMPLE
    scripts/hit-test.ps1
    scripts/hit-test.ps1 -Scenario bust
    scripts/hit-test.ps1 -Seconds 40 -Keep
#>
param(
    [ValidateSet('hits', 'bust')] [string] $Scenario = 'hits',
    [int] $Seconds = 0,          # game seconds of attacks or stars (the scenario's own: 90 for hits, 60 for bust)
    [string] $Start = '',        # the session's start (skytest's -Start): skytest.psd1's own by default
    [int] $TimeoutSeconds = 300,
    [switch] $Keep
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/common.ps1"
$root = Split-Path -Parent $PSScriptRoot
$skytest = Join-Path $PSScriptRoot 'skytest.ps1'
$file = Join-Path $root "scenarios/$Scenario.json"
if ($Seconds -gt 0) {
    # The same scenario with the asr.hits step's seconds changed: written beside the results of earlier runs.
    $text = [IO.File]::ReadAllText($file)
    $changed = [regex]::Replace($text, '("cmd": "asr\.hits",[^}]*"seconds": )\d+', "`${1}$Seconds")
    if ($changed -eq $text) { throw "No asr.hits step with seconds in $file." }
    New-Item -ItemType Directory -Force (Join-Path $root 'out') | Out-Null
    $file = Join-Path $root "out/$Scenario-$Seconds-s.json"
    [IO.File]::WriteAllText($file, $changed, [Text.UTF8Encoding]::new($false))
}

# Literal parameters: an array splatted into the forwarder would pass "-Json" on as a plain word.
$json = if ($Start) { & $skytest session -Json -Start $Start } else { & $skytest session -Json }
if ($LASTEXITCODE) { exit $LASTEXITCODE }
$session = $json | ConvertFrom-Json
$ours = -not $session.Reused
Write-Host "$(if ($ours) { 'Started' } else { 'Reusing' }) the test session (Skyrim pid $($session.Pid), $($session.Start))."
$code = 1
try {
    $skse = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games\Skyrim Special Edition\SKSE\AhShitRim.log'
    $from = if (Test-Path $skse) { @(Get-Content $skse).Count } else { 0 }  # this run's lines only
    & $skytest run $file -Timeout $TimeoutSeconds
    $code = $LASTEXITCODE
    Write-Host "--- Skyrim ($skse)"
    Get-Content $skse -ErrorAction SilentlyContinue | Select-Object -Skip $from |
        Select-String -Pattern 'hit test:|hitboxes:|vitals: (CJ''s vehicle|Wasted)|police: .*(door|runs for|BUSTED|star)|car hits:' | ForEach-Object { $_.Line }
    $sa = Join-Path (Get-RequiredSetting (Get-LocalConfig) 'SaPath') 'scripts\AhShitRim.log'
    Write-Host "--- San Andreas ($sa)"
    Select-String -Path $sa -Pattern 'car hits: Skyrim|scenes: CJ (hit|has)|busted' -ErrorAction SilentlyContinue | Select-Object -Last 40 | ForEach-Object { $_.Line }
} finally {
    if ($ours -and -not $Keep) { & $skytest stop }
}
exit $code
