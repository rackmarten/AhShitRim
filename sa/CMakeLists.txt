# San Andreas half: a 32-bit ASI plugin for GTA: San Andreas 1.0 US, built on plugin-sdk.
# Visual Studio generator with -A Win32 (see CMakePresets.json), so no x86 developer shell is needed.
cmake_minimum_required(VERSION 3.21)

project(
    AhShitRimSA
    VERSION 0.1.0
    LANGUAGES C CXX
)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "sa/ builds a 32-bit ASI plugin. Configure it with -A Win32 (use the CMake presets).")
endif()

set(CMAKE_CXX_STANDARD 23)  # /std:c++latest, which plugin-sdk needs
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT, like plugin-sdk's own projects: the ASI carries no runtime DLL dependencies.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---- plugin-sdk (plugin_sa), git submodule ----
# Mirrors the plugin_sa project of extern/plugin-sdk/tools/premake/premake5.lua, minus its
# generated precompiled header.
set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/extern/plugin-sdk)
file(GLOB_RECURSE PLUGIN_SA_SOURCES CONFIGURE_DEPENDS
    ${SDK}/plugin_sa/*.cpp
    ${SDK}/shared/*.cpp
    ${SDK}/hooking/*.cpp
    ${SDK}/safetyhook/*.cpp
    ${SDK}/safetyhook/*.c
)
add_library(plugin_sa STATIC ${PLUGIN_SA_SOURCES})
target_include_directories(plugin_sa PUBLIC
    ${SDK}/plugin_sa
    ${SDK}/plugin_sa/game_sa
    ${SDK}/plugin_sa/game_sa/enums
    ${SDK}/plugin_sa/game_sa/rw
    ${SDK}/safetyhook
    ${SDK}/shared
    ${SDK}/shared/game
    ${SDK}/shared/dxsdk
)
target_compile_definitions(plugin_sa PUBLIC
    GTASA PLUGIN_SGV_10US RW _DX9_SDK_INSTALLED _MBCS
    _CRT_SECURE_NO_WARNINGS _CRT_NON_CONFORMING_SWPRINTFS _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
    GTAGAME_NAME="San Andreas" GTAGAME_ABBR="SA" GTAGAME_ABBRLOW="sa"
    GTAGAME_PROTAGONISTNAME="CJ" GTAGAME_CITYNAME="San Andreas"
)
target_compile_options(plugin_sa PRIVATE /sdl- /MP /W0)
target_compile_options(plugin_sa PUBLIC /wd4073)  # "initializers put in library initialization area"

# ---- The ASI plugin ----
add_library(AhShitRim SHARED src/main.cpp src/window.cpp src/runner.cpp src/chunk_world.cpp src/cj_body.cpp src/model_sender.cpp
    src/rw_texture.cpp src/vehicles.cpp src/audio.cpp ../protocol/cpp/link.cpp)
set_target_properties(AhShitRim PROPERTIES SUFFIX ".asi")
target_include_directories(AhShitRim PRIVATE src ../protocol/cpp)
target_link_libraries(AhShitRim PRIVATE plugin_sa)
# plugin-sdk headers (angle brackets) are not held to /W4.
target_compile_options(AhShitRim PRIVATE /W4 /external:anglebrackets /external:W0 /utf-8 /MP /Zi)
target_compile_definitions(AhShitRim PRIVATE AHSHITRIM_VERSION="${PROJECT_VERSION}")
# Ship the .pdb next to the .asi in Release too, for crash dumps.
target_link_options(AhShitRim PRIVATE /DEBUG /OPT:REF /OPT:ICF /MAP)
