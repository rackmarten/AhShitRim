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
set(CMAKE_RC_COMPILER "${_bin}/rc")
set(CMAKE_MT "${_bin}/mt")

# Tests (ctest) run the built .exe files under Wine.
set(CMAKE_CROSSCOMPILING_EMULATOR wine)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# lib and link go through long-args.sh, which moves a command line too long for Windows into a
# response file with its paths rewritten for Wine. CMAKE_AR and CMAKE_LINKER take one program, so
# each gets a two-line wrapper in the build directory. (CMAKE_NINJA_FORCE_RESPONSE_FILE doesn't
# work: msvc-wine never rewrites the -I/home/... paths inside a compile's response file.)
set(_wrap_dir "${CMAKE_BINARY_DIR}/msvc-wine-${MSVC_WINE_ARCH}")
foreach(_tool lib link)
    file(CONFIGURE OUTPUT "${_wrap_dir}/${_tool}"
        CONTENT "#!/bin/sh\nexec \"${CMAKE_CURRENT_LIST_DIR}/long-args.sh\" \"${_bin}/${_tool}\" \"$@\"\n"
        @ONLY)
    file(CHMOD "${_wrap_dir}/${_tool}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endforeach()
set(CMAKE_AR "${_wrap_dir}/lib")
set(CMAKE_LINKER "${_wrap_dir}/link")
