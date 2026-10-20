# plugin-sdk's plugin_sa static library: built from the submodule's sources, or, when
# PLUGIN_SA_LIB names an existing plugin_sa.lib, that one linked as it is (CI caches it, keyed on
# the plugin-sdk commit, this file and the MSVC toolset; see scripts/build.ps1 -PluginSaCache).
# A prebuilt one is a Release build (static CRT /MT), so it's for Release builds only.
set(PLUGIN_SA_LIB "" CACHE FILEPATH "A prebuilt plugin_sa.lib (Release) to link instead of building plugin-sdk")
set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/extern/plugin-sdk)

if(PLUGIN_SA_LIB AND EXISTS "${PLUGIN_SA_LIB}")
    message(STATUS "plugin_sa: linking the prebuilt ${PLUGIN_SA_LIB}")
    add_library(plugin_sa STATIC IMPORTED)
    set_target_properties(plugin_sa PROPERTIES IMPORTED_LOCATION "${PLUGIN_SA_LIB}")
    set(PLUGIN_SA_USAGE INTERFACE)
else()
    # Mirrors the plugin_sa project of extern/plugin-sdk/tools/premake/premake5.lua, minus its
    # generated precompiled header.
    file(GLOB_RECURSE PLUGIN_SA_SOURCES CONFIGURE_DEPENDS
        ${SDK}/plugin_sa/*.cpp
        ${SDK}/shared/*.cpp
        ${SDK}/hooking/*.cpp
        ${SDK}/safetyhook/*.cpp
        ${SDK}/safetyhook/*.c
    )
    add_library(plugin_sa STATIC ${PLUGIN_SA_SOURCES})
    target_compile_options(plugin_sa PRIVATE /sdl- /MP /W0)
    # Set here rather than taken from sa/CMakeLists.txt, so this file alone decides how the
    # library is built (CI's cache key hashes it).
    set_target_properties(plugin_sa PROPERTIES
        CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON  # /std:c++latest, which plugin-sdk needs
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # static CRT
    set(PLUGIN_SA_USAGE PUBLIC)
endif()

target_include_directories(plugin_sa ${PLUGIN_SA_USAGE}
    ${SDK}/plugin_sa
    ${SDK}/plugin_sa/game_sa
    ${SDK}/plugin_sa/game_sa/enums
    ${SDK}/plugin_sa/game_sa/rw
    ${SDK}/safetyhook
    ${SDK}/shared
    ${SDK}/shared/game
    ${SDK}/shared/dxsdk
)
target_compile_definitions(plugin_sa ${PLUGIN_SA_USAGE}
    GTASA PLUGIN_SGV_10US RW _DX9_SDK_INSTALLED _MBCS
    _CRT_SECURE_NO_WARNINGS _CRT_NON_CONFORMING_SWPRINTFS _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
    GTAGAME_NAME="San Andreas" GTAGAME_ABBR="SA" GTAGAME_ABBRLOW="sa"
    GTAGAME_PROTAGONISTNAME="CJ" GTAGAME_CITYNAME="San Andreas"
)
target_compile_options(plugin_sa ${PLUGIN_SA_USAGE} /wd4073)  # "initializers put in library initialization area"
