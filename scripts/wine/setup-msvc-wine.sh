#!/usr/bin/env bash
# Sets up a Linux machine (Debian/Ubuntu, x86-64) to build both plugins with the real MSVC under
# Wine (mstorsjo/msvc-wine), for scripts/wine/build.sh. See docs/DEV_SETUP.md, "Building on Linux
# with msvc-wine". Idempotent: re-running it skips what's already there.
#
#   scripts/wine/setup-msvc-wine.sh               # apt packages (sudo), MSVC + Windows SDK, vcpkg
#   MSVC_VERSION=18.0 scripts/wine/setup-msvc-wine.sh   # a specific Visual Studio release's toolset
#
# Downloading MSVC means accepting Microsoft's Visual Studio licence
# (https://go.microsoft.com/fwlink/?LinkId=2086102); this script accepts it for you. The toolchain
# isn't redistributable, so it stays on this machine.
#
# Settings (environment):
#   MSVC_DIR       where MSVC goes (default ~/msvc)
#   MSVC_VERSION   a Visual Studio release vsdownload.py knows (e.g. 17.14 for toolset 14.44);
#                  default: the newest Visual Studio 2026 toolset, which is what CI's
#                  windows-latest runner builds with
#   VCPKG_ROOT     where vcpkg goes (default ~/vcpkg)
#   WINEPREFIX     the Wine prefix MSVC runs in (default ~/.wine-msvc, apart from any game prefix)
set -euo pipefail

MSVC_DIR="${MSVC_DIR:-$HOME/msvc}"
VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
export WINEPREFIX="${WINEPREFIX:-$HOME/.wine-msvc}"
export WINEDEBUG="${WINEDEBUG:--all}"
# msvc-wine's master on the day this was written; bump it deliberately.
MSVC_WINE_REF=514f8ea34842cd6d831804d0e9658d3a32870ae1
MSVC_WINE_SRC="$MSVC_DIR.src"

step() { printf '\n== %s\n' "$*"; }

step "apt packages"
# wine64 + winbind: msvc-wine's prerequisites (winbind for cl's mspdbsrv with /Zi).
# cmake, ninja-build: the build. git, curl, zip, unzip, tar, pkg-config: vcpkg.
packages=(wine wine64 python3 msitools ca-certificates winbind cmake ninja-build git curl zip unzip tar pkg-config)
missing=()
for p in "${packages[@]}"; do
    dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q 'install ok installed' || missing+=("$p")
done
if [ ${#missing[@]} -gt 0 ]; then
    sudo apt-get update -q
    sudo apt-get install -y -q "${missing[@]}"
else
    echo "all there"
fi

step "msvc-wine $MSVC_WINE_REF"
if [ ! -d "$MSVC_WINE_SRC/.git" ]; then
    git clone -q https://github.com/mstorsjo/msvc-wine.git "$MSVC_WINE_SRC"
fi
git -C "$MSVC_WINE_SRC" fetch -q origin
git -C "$MSVC_WINE_SRC" checkout -q "$MSVC_WINE_REF"

step "MSVC and the Windows SDK into $MSVC_DIR"
if [ -x "$MSVC_DIR/bin/x64/cl" ] && [ -x "$MSVC_DIR/bin/x86/cl" ]; then
    echo "already installed: $(ls "$MSVC_DIR/VC/Tools/MSVC" | tr '\n' ' ')"
else
    args=(--accept-license --dest "$MSVC_DIR" --architecture x64 x86)
    [ -n "${MSVC_VERSION:-}" ] && args+=(--msvc-version "$MSVC_VERSION")
    case "${MSVC_VERSION:-}" in 17.*) args+=(--major 17) ;; esac
    "$MSVC_WINE_SRC/vsdownload.py" "${args[@]}"
    "$MSVC_WINE_SRC/install.sh" "$MSVC_DIR"
fi

step "Wine prefix $WINEPREFIX"
[ -d "$WINEPREFIX" ] || wineboot -i >/dev/null 2>&1 || true
wineserver -w

step "vcpkg into $VCPKG_ROOT"
if [ ! -d "$VCPKG_ROOT/.git" ]; then
    git clone -q https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
else
    git -C "$VCPKG_ROOT" pull -q --ff-only
fi
"$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics

step "check"
cat >/tmp/msvc-wine-check.cpp <<'EOF'
#include <cstdio>
int main() { std::printf("hello from MSVC %d\n", _MSC_VER); }
EOF
for arch in x64 x86; do
    (cd /tmp && "$MSVC_DIR/bin/$arch/cl" /nologo /EHsc msvc-wine-check.cpp /Femsvc-wine-check-$arch.exe >/dev/null &&
        wine msvc-wine-check-$arch.exe)
done
rm -f /tmp/msvc-wine-check*
echo "Done. Build with scripts/wine/build.sh."
