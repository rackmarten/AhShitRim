<#
.SYNOPSIS
    Builds the SKSE plugin (64-bit: CMake + Ninja + vcpkg) and the San Andreas ASI plugin
    (32-bit: CMake, Visual Studio generator, Win32).
.EXAMPLE
    scripts/build.ps1                       # both, Release
    scripts/build.ps1 -Target sa -Configuration Debug
    scripts/build.ps1 -Target sa -PluginSaCache .plugin-sa  # CI: link (or else keep) a prebuilt plugin_sa.lib
    scripts/build.ps1 -Target skse -NoTestKit               # without skse-test-kit, as scripts/package.ps1 builds
    powershell.exe -File scripts/build.ps1  # from WSL
#>
param(
    [ValidateSet('all', 'skse', 'sa')] [string] $Target = 'all',
    [ValidateSet('Release', 'Debug')] [string] $Configuration = 'Release',
    # A folder for plugin-sdk's plugin_sa.lib (Release only): linked when it's there, put there
    # after building it when it isn't. CI caches the folder (.github/workflows/build.yml).
    [string] $PluginSaCache,
    # Leaves skse-test-kit out of the SKSE plugin. Without this it's linked whenever the submodule is checked out
    # (docs/DEV_SETUP.md, "Test sessions"); a release never has it.
    [switch] $NoTestKit
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
    # The test kit's submodule is never fetched by itself (it's private for now): use it when it's there.
    $kit = -not $NoTestKit -and (Test-Path (Join-Path $RepoRoot 'skse/extern/skse-test-kit/CMakeLists.txt'))
    if (-not $NoTestKit -and -not $kit) {
        Write-Host 'skse-test-kit is not checked out: building without test sessions (git submodule update --init --checkout skse/extern/skse-test-kit adds them).' -ForegroundColor Yellow
    }
    Push-Location (Join-Path $RepoRoot 'skse')
    try {
        Invoke-Checked cmake @('--preset', $preset, "-DAHSHITRIM_TEST_KIT=$(if ($kit) { 'ON' } else { 'OFF' })")
        Invoke-Checked cmake @('--build', '--preset', $preset)
    } finally { Pop-Location }
    Write-Host "SKSE plugin: skse/build/$preset/AhShitRim.dll$(if ($kit) { ' (with skse-test-kit)' })"
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
        # MSBuild's /m takes every core; CMAKE_BUILD_PARALLEL_LEVEL (which Ninja's builds follow) limits it too.
        $parallel = if ($env:CMAKE_BUILD_PARALLEL_LEVEL) { "/m:$env:CMAKE_BUILD_PARALLEL_LEVEL" } else { '/m' }
        Invoke-Checked cmake @('--build', '--preset', $preset, '--', $parallel, '/nologo', '/v:minimal')
        if ($cachedLib -and -not $prebuilt) {
            New-Item -ItemType Directory -Force (Split-Path $cachedLib) | Out-Null
            Copy-Item -Force (Join-Path $RepoRoot 'sa/build/Release/plugin_sa.lib') $cachedLib
            Write-Host "plugin_sa: kept $cachedLib for the next build"
        }
    } finally { Pop-Location }
    Write-Host "San Andreas plugin: sa/build/$Configuration/AhShitRim.asi"
}
