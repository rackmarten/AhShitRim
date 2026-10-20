<#
.SYNOPSIS
    Builds the SKSE plugin (64-bit: CMake + Ninja + vcpkg) and the San Andreas ASI plugin
    (32-bit: CMake, Visual Studio generator, Win32).
.EXAMPLE
    scripts/build.ps1                       # both, Release
    scripts/build.ps1 -Target sa -Configuration Debug
    scripts/build.ps1 -Target sa -PluginSaCache .plugin-sa  # CI: link (or else keep) a prebuilt plugin_sa.lib
    powershell.exe -File scripts/build.ps1  # from WSL
#>
param(
    [ValidateSet('all', 'skse', 'sa')] [string] $Target = 'all',
    [ValidateSet('Release', 'Debug')] [string] $Configuration = 'Release',
    # A folder for plugin-sdk's plugin_sa.lib (Release only): linked when it's there, put there
    # after building it when it isn't. CI caches the folder (.github/workflows/build.yml).
    [string] $PluginSaCache
)

. "$PSScriptRoot/common.ps1"

$preset = $Configuration.ToLower()

# Clones whichever submodule is missing (a clone without --recurse-submodules).
function Initialize-Submodule($path, $marker) {
    if (-not (Test-Path (Join-Path $RepoRoot "$path/$marker"))) {
        Invoke-Checked git @('-C', $RepoRoot, 'submodule', 'update', '--init', '--recursive', $path)
    }
}

if ($Target -in 'all', 'skse') {
    Enter-DevShell
    Initialize-Submodule 'skse/extern/CommonLibSSE-NG' 'CMakeLists.txt'
    Push-Location (Join-Path $RepoRoot 'skse')
    try {
        Invoke-Checked cmake @('--preset', $preset)
        Invoke-Checked cmake @('--build', '--preset', $preset)
    } finally { Pop-Location }
    Write-Host "SKSE plugin: skse/build/$preset/AhShitRim.dll"
}

if ($Target -in 'all', 'sa') {
    # The Visual Studio generator finds MSBuild and the x86 compiler itself; CMake comes from the
    # developer shell (or PATH).
    Enter-DevShell
    Initialize-Submodule 'sa/extern/plugin-sdk' 'plugin_sa/plugin_sa.h'
    Push-Location (Join-Path $RepoRoot 'sa')
    try {
        # The preset names VS 2022's generator; under VS 2026 (GitHub's windows-latest) use its own.
        $generator = if ($env:VisualStudioVersion -eq '18.0') { @('-G', 'Visual Studio 18 2026') } else { @() }
        $cacheDir = if ([IO.Path]::IsPathRooted($PluginSaCache)) { $PluginSaCache } elseif ($PluginSaCache) { Join-Path $RepoRoot $PluginSaCache }
        $cachedLib = if ($cacheDir -and $Configuration -eq 'Release') { Join-Path $cacheDir 'plugin_sa.lib' }
        $prebuilt = if ($cachedLib -and (Test-Path $cachedLib)) { $cachedLib } else { '' }
        Invoke-Checked cmake (@('--preset', 'x86', "-DPLUGIN_SA_LIB=$prebuilt") + $generator)
        Invoke-Checked cmake @('--build', '--preset', $preset, '--', '/m', '/nologo', '/v:minimal')
        if ($cachedLib -and -not $prebuilt) {
            New-Item -ItemType Directory -Force (Split-Path $cachedLib) | Out-Null
            Copy-Item -Force (Join-Path $RepoRoot 'sa/build/Release/plugin_sa.lib') $cachedLib
            Write-Host "plugin_sa: kept $cachedLib for the next build"
        }
    } finally { Pop-Location }
    Write-Host "San Andreas plugin: sa/build/$Configuration/AhShitRim.asi"
}
