<#
.SYNOPSIS
    Starts GTA: San Andreas (gta_sa.exe from SaPath in local.props) hidden: windowed and minimized,
    straight into an empty world with CJ on the test plane, and waits until the ASI says so.
    With -Visible it opens in a small 800x600 window instead (without taking the focus), to watch CJ.
.EXAMPLE
    scripts/launch-sa.ps1
    scripts/launch-sa.ps1 -Visible
#>
param(
    [switch] $Visible,
    [int] $TimeoutSeconds = 120
)

. "$PSScriptRoot/common.ps1"
$sa = Get-RequiredSetting (Get-LocalConfig) 'SaPath'
$exe = Join-Path $sa 'gta_sa.exe'
if (-not (Test-Path $exe)) { throw "gta_sa.exe not found in SaPath ($sa)." }
if (Get-Process gta_sa -ErrorAction SilentlyContinue) { throw 'San Andreas is already running.' }

$log = Join-Path $sa 'scripts/AhShitRim.log'
$launched = Get-Date
$arguments = if ($Visible) { @('-ahshitrim-visible') } else { @() }
$process = if ($arguments) {
    Start-Process -PassThru -WorkingDirectory $sa $exe -ArgumentList $arguments
} else {
    Start-Process -PassThru -WorkingDirectory $sa $exe
}

# The ASI rewrites its log at every start, so a ready line newer than the launch is this run's.
$deadline = $launched.AddSeconds($TimeoutSeconds)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 500
    if ($process.HasExited) {
        throw "San Andreas exited (code $($process.ExitCode)) before CJ was ready. Check scripts/logs.ps1 -Target sa."
    }
    if ((Test-Path $log) -and (Get-Item $log).LastWriteTime -gt $launched) {
        $ready = Select-String -Path $log -Pattern 'runner: CJ is on the test plane', 'runner: could not create CJ' -SimpleMatch | Select-Object -First 1
        if ($ready) {
            if ($ready.Line -like '*could not*') { throw "San Andreas started, but the ASI could not create CJ. Check scripts/logs.ps1 -Target sa." }
            Write-Host "San Andreas is up (pid $($process.Id)), CJ is on the test plane$(if ($Visible) { ', window visible' } else { ', minimized' })." -ForegroundColor Green
            return
        }
    }
}
throw "CJ not ready after $TimeoutSeconds s. Check scripts/logs.ps1 -Target sa."
