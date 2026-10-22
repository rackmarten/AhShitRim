#!/usr/bin/env bash
# Runs an msvc-wine tool (lib, link) with a command line too long for Windows.
#
#   scripts/wine/long-args.sh <msvc-wine tool> <args...>
#
# CMake on a Linux host puts every object on the command line, and a big library's (CommonLibSSE's
# lib.exe call) is over Windows' 32767 characters: Wine then never starts the tool and msvc-wine's
# wrapper waits on its output forever. So past 30000 characters the arguments go into a response
# file instead, with absolute paths rewritten to z:/ as msvc-wine's wine-msvc.sh does on the command
# line (it never looks inside a response file). Shorter command lines pass straight through.
# scripts/wine/msvc-wine.cmake points CMAKE_AR and CMAKE_LINKER at it through small generated
# wrappers.
set -euo pipefail

tool=$1
shift

len=0
for a; do len=$((len + ${#a} + 1)); done
if [ "$len" -lt 30000 ]; then
    exec "$tool" "$@"
fi

# The response file sits in the build directory, so its own path is short and relative.
rsp=$(mktemp -p . long-args.XXXXXX.rsp)
trap 'rm -f "$rsp"' EXIT
for a; do
    path=
    case "$a" in
        [-/][A-Za-z]/*) path=${a#??} ;;
        [-/][A-Za-z][A-Za-z]/*) path=${a#???} ;;
        [-/][A-Za-z][A-Za-z][A-Za-z]*:/*) path=${a#*:} ;;
        /*) path=$a ;;
    esac
    if [ -n "$path" ] && [ -d "$(dirname "$path")" ] && [ "$(dirname "$path")" != "/" ]; then
        a=${a%"$path"}z:$path
    fi
    printf '"%s"\n' "$a"
done >"$rsp"

"$tool" "@$rsp"
