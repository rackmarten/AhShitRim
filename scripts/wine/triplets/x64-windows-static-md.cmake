# vcpkg's x64-windows-static-md, built by MSVC under Wine (scripts/wine/build.sh overlays it on the
# built-in triplet of the same name, so the presets' triplet stays the same).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../msvc-wine.cmake")
set(VCPKG_ENV_PASSTHROUGH MSVC_DIR WINEPREFIX WINEDEBUG)

# DirectXTK compiles its shaders with a batch file: run it under Wine (see ../bin/CompileShaders.cmd).
if(PORT STREQUAL "directxtk")
    set(ENV{PATH} "${CMAKE_CURRENT_LIST_DIR}/../bin:$ENV{PATH}")
endif()
