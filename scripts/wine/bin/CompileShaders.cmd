#!/usr/bin/env bash
# DirectXTK's build runs its Src/Shaders/CompileShaders.cmd by name from that folder, which only
# works on Windows. The overlay triplet puts this folder first on PATH for that port, so this runs
# instead: the real batch file under Wine's cmd, with fxc.exe from msvc-wine's Windows SDK.
set -euo pipefail
fxc="$(ls -d "${MSVC_DIR:-$HOME/msvc}"/kits/10/bin/10.*/x64/fxc.exe | sort -V | tail -1)"
export LegacyShaderCompiler="$(winepath -w "$fxc")"
export CompileShadersOutput="$(winepath -w "${CompileShadersOutput:-Compiled}")"
export WINEDEBUG="${WINEDEBUG:--all}"
exec wine cmd /c CompileShaders.cmd "$@"
