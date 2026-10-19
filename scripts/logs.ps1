<#
.SYNOPSIS
    Prints the tail of the SKSE plugin log and the San Andreas plugin log.
.EXAMPLE
    scripts/logs.ps1 -Lines 100
    scripts/logs.ps1 -Target sa
#>
param(
    [ValidateSet('all', 'skse', 'sa')] [string] $Target = 'all',
    [int] $Lines = 40
)

. "$PSScriptRoot/common.ps1"

function Show-Tail($title, $path) {
    Write-Host "==== $title ($path)" -ForegroundColor Cyan
    if (Test-Path $path) { Get-Content -Tail $Lines $path }
    else { Write-Host '(not found yet; start the game once)' }
}

if ($Target -in 'all', 'skse') {
    $skseDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games/Skyrim Special Edition/SKSE'
    Show-Tail 'SKSE plugin' (Join-Path $skseDir 'AhShitRim.log')
    Show-Tail 'SKSE loader' (Join-Path $skseDir 'skse64.log')
}

if ($Target -in 'all', 'sa') {
    $sa = Get-RequiredSetting (Get-LocalConfig) 'SaPath'
    Show-Tail 'San Andreas plugin' (Join-Path $sa 'scripts/AhShitRim.log')
}
