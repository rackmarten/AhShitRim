# Shared helpers for the build/deploy/log scripts. Dot-source it: . "$PSScriptRoot/common.ps1"

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot

# Reads local.props (MSBuild XML) into a hashtable. Copy local.props.example to create it.
function Get-LocalConfig {
    $path = Join-Path $RepoRoot 'local.props'
    if (-not (Test-Path $path)) {
        throw "local.props not found. Copy local.props.example to local.props in the repo root and set your game paths."
    }
    $config = @{}
    foreach ($group in ([xml](Get-Content -Raw $path)).Project.PropertyGroup) {
        foreach ($node in $group.ChildNodes) {
            if ($node.NodeType -eq 'Element') { $config[$node.LocalName] = $node.InnerText.Trim() }
        }
    }
    return $config
}

function Get-RequiredSetting($config, $name) {
    if (-not $config[$name]) { throw "$name is not set in local.props." }
    return $config[$name]
}

function Invoke-Checked {
    param([string] $File, [string[]] $Arguments)
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$File $($Arguments -join ' ') failed with exit code $LASTEXITCODE" }
}

# Enters the x64 MSVC developer environment of the newest VS / Build Tools install with the C++ tools.
# That shell also puts CMake, Ninja and the bundled vcpkg (VCPKG_ROOT) on PATH.
function Enter-DevShell {
    if ($env:VSCMD_ARG_TGT_ARCH -eq 'x64') { return }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found. Install Build Tools for Visual Studio 2022 (docs/DEV_SETUP.md)." }
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) { throw "No Visual Studio install with the C++ x64 tools found. Add the 'Desktop development with C++' workload." }
    # A stray quote in PATH makes VsDevCmd.bat fail on "(x86)" entries; strip quotes for this process only.
    $env:Path = $env:Path -replace '"', ''
    Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
    if (-not $env:VCPKG_ROOT -and $env:VCPKG_INSTALLATION_ROOT) { $env:VCPKG_ROOT = $env:VCPKG_INSTALLATION_ROOT }  # GitHub runners
    if (-not $env:VCPKG_ROOT) { throw "VCPKG_ROOT is not set. Install vcpkg (docs/DEV_SETUP.md) or use the copy bundled with Build Tools." }
}
