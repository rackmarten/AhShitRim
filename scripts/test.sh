#!/usr/bin/env bash
# Runs the game-free protocol tests on Linux (the linux-tests preset: Ninja, gcc or clang, doctest
# fetched by CMake). Needs cmake 3.21+, ninja and a C++23 compiler; the link and the launcher's
# Pester tests are Windows-only and run in scripts/test.ps1. Set AHSR_SA_PATH to a San Andreas
# install to run the HUD test against its files too.
#   scripts/test.sh
set -euo pipefail

cd "$(dirname "$0")/../protocol"
cmake --preset linux-tests
cmake --build --preset linux-tests
ctest --preset linux-tests
