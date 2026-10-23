# Unit tests for the link (header layout, version check, handshake, heartbeat, slots, rings), the input
# mapping, the pose conversion, the ground, CJ's model and skeleton, and the HUD's fonts and layout.
# Standalone (no CommonLibSSE-NG, no plugin-sdk), so it builds in seconds. Run with scripts/test.ps1 on
# Windows or scripts/test.sh on Linux, where the link (shared memory through Windows' API) is left out.
cmake_minimum_required(VERSION 3.21)

project(AhShitRimProtocolTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# vcpkg's doctest on Windows; the Linux preset downloads a pinned release instead, so it needs no vcpkg.
option(AHSR_FETCH_DOCTEST "Download doctest with FetchContent instead of find_package" OFF)
if(AHSR_FETCH_DOCTEST)
    include(FetchContent)
    FetchContent_Declare(doctest
        URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.12.tar.gz
        URL_HASH SHA256=73381c7aa4dee704bd935609668cf41880ea7f19fa0504a200e13b74999c2d70
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    # doctest asks for CMake 3.0, which CMake 4 refuses without this.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(doctest)
else()
    find_package(doctest CONFIG REQUIRED)
endif()

add_executable(protocol_tests
    tests/cpp/main.cpp
    tests/cpp/test_layout.cpp
    tests/cpp/test_vehicle_kinds.cpp
    tests/cpp/test_parachute.cpp
    tests/cpp/test_input.cpp
    tests/cpp/test_pose.cpp
    tests/cpp/test_collision.cpp
    tests/cpp/test_ground.cpp
    tests/cpp/test_fall_rescue.cpp
    tests/cpp/test_position_guard.cpp
    tests/cpp/test_skin_model.cpp
    tests/cpp/test_skin_build.cpp
    tests/cpp/test_body.cpp
    tests/cpp/test_texture_decode.cpp
    tests/cpp/test_vehicle.cpp
    tests/cpp/test_txd.cpp
    tests/cpp/test_hud.cpp
    tests/cpp/test_radio.cpp
    tests/cpp/test_activate.cpp
    tests/cpp/test_anchor.cpp
    tests/cpp/test_scene.cpp
    tests/cpp/test_health.cpp
    tests/cpp/test_voice_lines.cpp
    tests/cpp/test_wasted.cpp
    tests/cpp/test_cosave.cpp
    tests/cpp/test_weapons.cpp
    tests/cpp/test_img.cpp
    tests/cpp/test_flash.cpp
    tests/cpp/test_damage.cpp
    tests/cpp/test_explosives.cpp
    tests/cpp/test_vehicle_damage.cpp
    tests/cpp/test_smoke_model.cpp
    tests/cpp/test_puff_model.cpp
)
if(WIN32)
    target_sources(protocol_tests PRIVATE cpp/link.cpp tests/cpp/test_link.cpp)
endif()
target_link_libraries(protocol_tests PRIVATE doctest::doctest)
if(MSVC)
    target_compile_options(protocol_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(protocol_tests PRIVATE -Wall -Wextra)
endif()

enable_testing()
add_test(NAME protocol_tests COMMAND protocol_tests)

# Stands in for the Skyrim plugin to drive a running San Andreas (scripts/fake-skyrim.ps1). Not a test.
if(WIN32)
    add_executable(fake_skyrim cpp/link.cpp tools/fake_skyrim.cpp)
    if(MSVC)
        target_compile_options(fake_skyrim PRIVATE /W4 /permissive- /utf-8)
    endif()
endif()

# The same for CJ's weapons (scripts/fake-weapons.ps1). Not a test.
if(WIN32)
    add_executable(fake_weapons cpp/link.cpp tools/fake_weapons.cpp)
    if(MSVC)
        target_compile_options(fake_weapons PRIVATE /W4 /permissive- /utf-8)
    endif()
endif()

# The same for every kind of vehicle and CJ's gear (scripts/fake-vehicles.ps1). Not a test.
if(WIN32)
    add_executable(fake_vehicles cpp/link.cpp tools/fake_vehicles.cpp)
    if(MSVC)
        target_compile_options(fake_vehicles PRIVATE /W4 /permissive- /utf-8)
    endif()
endif()
