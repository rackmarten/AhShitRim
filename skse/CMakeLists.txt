cmake_minimum_required(VERSION 3.21)

project(
    AhShitRim
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CommonLibSSE-NG is a git submodule pinned to a release tag. On GitHub Actions it links the
# matching prebuilt bundle instead of compiling from source (see extern/CommonLibSSE-NG/PREBUILT.md).
set(BUILD_TESTS OFF CACHE BOOL "Skip CommonLibSSE-NG unit tests" FORCE)
add_subdirectory(extern/CommonLibSSE-NG commonlib EXCLUDE_FROM_ALL)
# Only defined by the prebuilt path upstream, so include the helper explicitly for source builds too.
include(extern/CommonLibSSE-NG/cmake/CommonLibSSE.cmake)

add_commonlibsse_plugin(
    ${PROJECT_NAME}
    NAME "AhShitRim"
    AUTHOR "Marek Uncik"
    SOURCES src/main.cpp src/link/LinkService.cpp src/player/PlayerSync.cpp src/player/Activation.cpp src/player/Conversation.cpp src/player/AmmuNation.cpp src/player/Looting.cpp src/player/GunRewards.cpp src/player/Respawns.cpp src/player/Vitals.cpp src/player/SaveState.cpp src/player/Arms.cpp src/player/CheatCodes.cpp src/player/Police.cpp src/player/Shots.cpp src/player/People.cpp src/player/Projectiles.cpp src/player/VehicleFx.cpp src/player/CarHits.cpp src/player/LockOn.cpp src/player/Riders.cpp src/player/Puffs.cpp src/body/MuzzleFlash.cpp src/collision/CollisionSampler.cpp src/collision/StaticMeshes.cpp src/body/ModelView.cpp src/hud/SaHud.cpp src/hud/Overlay.cpp src/audio/SaAudio.cpp src/debug/StreamTest.cpp ../protocol/cpp/link.cpp
)

target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
target_include_directories(${PROJECT_NAME} PRIVATE src ../protocol/cpp)
# The shared link code includes <Windows.h> itself and must not pick up CommonLib's PCH.
set_source_files_properties(../protocol/cpp/link.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
# So does the overlay, which needs the real d3d11.h.
set_source_files_properties(src/hud/Overlay.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
