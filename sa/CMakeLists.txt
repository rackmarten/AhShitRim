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
include(cmake/plugin_sa.cmake)

# ---- The ASI plugin ----
add_library(AhShitRim SHARED src/main.cpp src/window.cpp src/runner.cpp src/chunk_world.cpp src/cj_body.cpp src/cj_weapons.cpp src/cj_shots.cpp src/model_sender.cpp
    src/rw_texture.cpp src/vehicles.cpp src/audio.cpp src/scenes.cpp src/vitals.cpp ../protocol/cpp/link.cpp)
set_target_properties(AhShitRim PROPERTIES SUFFIX ".asi")
target_include_directories(AhShitRim PRIVATE src ../protocol/cpp)
target_link_libraries(AhShitRim PRIVATE plugin_sa)
# plugin-sdk headers (angle brackets) are not held to /W4.
target_compile_options(AhShitRim PRIVATE /W4 /external:anglebrackets /external:W0 /utf-8 /MP /Zi)
target_compile_definitions(AhShitRim PRIVATE AHSHITRIM_VERSION="${PROJECT_VERSION}")
# Ship the .pdb next to the .asi in Release too, for crash dumps.
target_link_options(AhShitRim PRIVATE /DEBUG /OPT:REF /OPT:ICF /MAP)
