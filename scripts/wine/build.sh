#!/usr/bin/env bash
# Builds both plugins with the real MSVC under Wine on Linux, then runs the doctest suite (the
# link's tests included) under Wine. Needs scripts/wine/setup-msvc-wine.sh first. See
# docs/DEV_SETUP.md, "Building on Linux with msvc-wine".
#
#   scripts/wine/build.sh                  # SKSE plugin, San Andreas plugin, tests (Release)
#   scripts/wine/build.sh skse sa          # any of: skse sa tests
#
# Outputs: skse/build/wine-release/AhShitRim.dll, sa/build/wine-release/AhShitRim.asi.
#
# Settings (environment):
#   MSVC_DIR, VCPKG_ROOT, WINEPREFIX   as in setup-msvc-wine.sh (defaults ~/msvc, ~/vcpkg, ~/.wine-msvc)
#   JOBS                               parallel compiles (default: half the cores; cl.exe under Wine
#                                      needs ~1 GB each, and running out of memory crashes c2.dll)
#   BUSY_DIR                           a directory that holds a file per game session using this
#                                      machine; the build refuses to start while it isn't empty
#                                      (default: $XDG_RUNTIME_DIR/cue-gpu/holders). FORCE=1 skips it.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
WINE_DIR="$REPO/scripts/wine"
export MSVC_DIR="${MSVC_DIR:-$HOME/msvc}"
export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
export WINEPREFIX="${WINEPREFIX:-$HOME/.wine-msvc}"
export WINEDEBUG="${WINEDEBUG:--all}"
export VCPKG_DISABLE_METRICS=1
JOBS="${JOBS:-$(( ($(nproc) + 1) / 2 ))}"
BUSY_DIR="${BUSY_DIR:-${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/cue-gpu/holders}"

targets=("$@")
[ ${#targets[@]} -gt 0 ] || targets=(skse sa tests)

# Don't take the CPU from someone playing on this machine.
if [ "${FORCE:-}" != 1 ] && [ -d "$BUSY_DIR" ] && [ -n "$(ls -A "$BUSY_DIR")" ]; then
    echo "A game session is active ($(ls -A "$BUSY_DIR" | tr '\n' ' ')in $BUSY_DIR); not building. FORCE=1 overrides." >&2
    exit 2
fi
[ -x "$MSVC_DIR/bin/x64/cl" ] || { echo "No MSVC at $MSVC_DIR: run scripts/wine/setup-msvc-wine.sh" >&2; exit 1; }

for sub in skse/extern/CommonLibSSE-NG sa/extern/plugin-sdk; do
    [ -e "$REPO/$sub/.git" ] || git -C "$REPO" submodule update --init --recursive "$sub"
done

# One wineserver for the whole build instead of one per compiler call.
wineserver --persistent=300 2>/dev/null || true

timings=()
timed() {
    local name=$1 start=$SECONDS
    shift
    printf '\n== %s\n' "$name"
    "$@"
    timings+=("$(printf '%-6s %4ds' "$name" $((SECONDS - start)))")
}

vcpkg_args=(
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
    -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$WINE_DIR/msvc-wine.cmake"
    -DVCPKG_OVERLAY_TRIPLETS="$WINE_DIR/triplets"
    -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
    -DMSVC_WINE_ARCH=x64
)

build_skse() {
    cmake -S "$REPO/skse" -B "$REPO/skse/build/wine-release" -G Ninja -DCMAKE_BUILD_TYPE=Release "${vcpkg_args[@]}"
    cmake --build "$REPO/skse/build/wine-release" -j "$JOBS"
}

# The Visual Studio generator needs MSBuild, so Ninja with the x86 compiler instead.
build_sa() {
    cmake -S "$REPO/sa" -B "$REPO/sa/build/wine-release" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$WINE_DIR/msvc-wine.cmake" -DMSVC_WINE_ARCH=x86
    cmake --build "$REPO/sa/build/wine-release" -j "$JOBS"
}

# The same as scripts/test.ps1's "tests" preset (Debug, vcpkg's doctest), with ctest running the
# .exe under Wine.
build_tests() {
    cmake -S "$REPO/protocol" -B "$REPO/protocol/build/wine-tests" -G Ninja -DCMAKE_BUILD_TYPE=Debug "${vcpkg_args[@]}"
    cmake --build "$REPO/protocol/build/wine-tests" -j "$JOBS"
    ctest --test-dir "$REPO/protocol/build/wine-tests" --output-on-failure
}

for t in "${targets[@]}"; do
    case "$t" in
        skse | sa | tests) timed "$t" "build_$t" ;;
        *) echo "unknown target $t (skse, sa, tests)" >&2; exit 2 ;;
    esac
done

# What a loader looks at: the machine type and the exports (SKSE needs SKSEPlugin_Load and
# SKSEPlugin_Version; the ASI loader only needs a DLL).
summary() {
    local file=$1 arch=$2
    [ -f "$file" ] || return 0
    printf '\n%s: %s bytes\n' "${file#"$REPO"/}" "$(stat -c %s "$file")"
    "$MSVC_DIR/bin/$arch/dumpbin" /nologo /headers "$file" | grep -m1 -i 'machine' | sed 's/^ */  /'
    "$MSVC_DIR/bin/$arch/dumpbin" /nologo /exports "$file" | awk '/ordinal +hint/ {on=1; next} on && NF >= 4 {print "  export " $4}'
}
summary "$REPO/skse/build/wine-release/AhShitRim.dll" x64
summary "$REPO/sa/build/wine-release/AhShitRim.asi" x86

printf '\n== timings\n'
printf '%s\n' "${timings[@]}"
