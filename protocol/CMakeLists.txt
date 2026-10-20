# Unit tests for the link (header layout, version check, handshake, heartbeat, slots, rings), the input
# mapping, the pose conversion, the ground, CJ's model and skeleton, and the HUD's fonts and layout.
# Standalone (no CommonLibSSE-NG, no plugin-sdk), so it builds in seconds. Run with scripts/test.ps1.
cmake_minimum_required(VERSION 3.21)

project(AhShitRimProtocolTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(doctest CONFIG REQUIRED)

add_executable(protocol_tests
    cpp/link.cpp
    tests/cpp/main.cpp
    tests/cpp/test_layout.cpp
    tests/cpp/test_link.cpp
    tests/cpp/test_input.cpp
    tests/cpp/test_pose.cpp
    tests/cpp/test_collision.cpp
    tests/cpp/test_ground.cpp
    tests/cpp/test_fall_rescue.cpp
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
    tests/cpp/test_wasted.cpp
    tests/cpp/test_cosave.cpp
)
target_link_libraries(protocol_tests PRIVATE doctest::doctest)
if(MSVC)
    target_compile_options(protocol_tests PRIVATE /W4 /permissive- /utf-8)
endif()

enable_testing()
add_test(NAME protocol_tests COMMAND protocol_tests)

# Stands in for the Skyrim plugin to drive a running San Andreas (scripts/fake-skyrim.ps1). Not a test.
add_executable(fake_skyrim cpp/link.cpp tools/fake_skyrim.cpp)
if(MSVC)
    target_compile_options(fake_skyrim PRIVATE /W4 /permissive- /utf-8)
endif()
