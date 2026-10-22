# CMake toolchain for MSVC under Wine (msvc-wine, set up by scripts/wine/setup-msvc-wine.sh).
# scripts/wine/build.sh passes it as the toolchain, or as vcpkg's chainload toolchain (see
# triplets/), with MSVC_WINE_ARCH set to x64 or x86. vcpkg's port builds get the architecture from
# their triplet's VCPKG_TARGET_ARCHITECTURE instead.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)

if(DEFINED VCPKG_TARGET_ARCHITECTURE)
    set(MSVC_WINE_ARCH ${VCPKG_TARGET_ARCHITECTURE})
elseif(NOT MSVC_WINE_ARCH)
    set(MSVC_WINE_ARCH x64)
endif()
if(MSVC_WINE_ARCH STREQUAL "x86")
    set(CMAKE_SYSTEM_PROCESSOR X86)
else()
    set(CMAKE_SYSTEM_PROCESSOR AMD64)
endif()

if(DEFINED ENV{MSVC_DIR})
    set(MSVC_WINE_ROOT "$ENV{MSVC_DIR}")
else()
    set(MSVC_WINE_ROOT "$ENV{HOME}/msvc")
endif()
set(_bin "${MSVC_WINE_ROOT}/bin/${MSVC_WINE_ARCH}")
if(NOT EXISTS "${_bin}/cl")
    message(FATAL_ERROR "No msvc-wine install at ${MSVC_WINE_ROOT}: run scripts/wine/setup-msvc-wine.sh")
endif()

# msvc-wine's wrappers set INCLUDE, LIB and the Wine paths themselves.
set(CMAKE_C_COMPILER "${_bin}/cl")
set(CMAKE_CXX_COMPILER "${_bin}/cl")
set(CMAKE_LINKER "${_bin}/link")
set(CMAKE_AR "${_bin}/lib")
set(CMAKE_RC_COMPILER "${_bin}/rc")
set(CMAKE_MT "${_bin}/mt")

# Tests (ctest) run the built .exe files under Wine.
set(CMAKE_CROSSCOMPILING_EMULATOR wine)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# CMake on a Linux host puts every object on the command line, and a big library's (CommonLibSSE's
# lib.exe call) is over Windows' 32767 characters: Wine then never starts the tool and msvc-wine's
# wrapper waits on its output forever. Response files keep the command lines short. Not for vcpkg's
# port builds: with it DirectXTK's failed.
if(NOT DEFINED VCPKG_TARGET_ARCHITECTURE)
    set(CMAKE_NINJA_FORCE_RESPONSE_FILE ON CACHE BOOL "")
endif()
