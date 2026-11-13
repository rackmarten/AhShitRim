<#
.SYNOPSIS
    Checks every kind of vehicle and CJ's gear against a running San Andreas without Skyrim: builds and runs
    protocol/tools/fake_vehicles, which links up as the Skyrim side, hands the player over on flat ground and,
    through the dev key, rides an NRG-500 and a BMX, refuses a boat, flies a Hunter (rotor, guns) and a Hydra
    (hover, forward flight, missiles), fires the Rhino, flies up on the jetpack and comes down on the parachute,
    rams a parked car with BSXSGGC on and spawns planes flying with AIR, asks for each pickup model, catches the
    jetpack it takes off as a dropped pickup, and walks off a 100 m drop with the parachute (it must skydive).
    $env:AHSHITRIM_ONLY = 'cheats', 'cliff', 'pickups', 'radio', 'handling', 'exit' or 'drive' runs only those. The handling check drives the Rhino and
    a Sandking on flat ground, humps and steep boulders and prints how each turns and rides; it fails if the Rhino doesn't turn
    about its middle on flat ground ($env:AHSHITRIM_GROUND = 'flat', 'rocks' or 'boulders' for one ground). The radio check: no station on a BMX,
    the station dialed in one vehicle comes on in the next (Radio Sovngarde too), and San Andreas's radio reaches a station dialed.
    'exit' gets CJ out of a Sandking, a ZR-350 and a Sanchez and logs his body every frame; it fails on a jump or a step back into the vehicle.
    'drive' drives a Sandking 60 s and a Sanchez 20 s and logs his body in the vehicle every frame, as San Andreas has it and as Skyrim
    draws them (each smoothed); it fails if he leaves his seat or jumps in it.
    Exits non-zero when a check fails. Start San Andreas first (scripts/launch-sa.ps1), and keep Skyrim closed.
.EXAMPLE
    scripts/fake-vehicles.ps1
#>
. "$PSScriptRoot/common.ps1"

Enter-DevShell
Push-Location (Join-Path $RepoRoot 'protocol')
try {
    Invoke-Checked cmake @('--preset', 'tests')
    Invoke-Checked cmake @('--build', '--preset', 'tests', '--target', 'fake_vehicles')
} finally { Pop-Location }
Invoke-Checked (Join-Path $RepoRoot 'protocol/build/tests/fake_vehicles.exe') @()
