# Unit tests for the link: header layout, version check, handshake and heartbeat.
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
)
target_link_libraries(protocol_tests PRIVATE doctest::doctest)
if(MSVC)
    target_compile_options(protocol_tests PRIVATE /W4 /permissive- /utf-8)
endif()

enable_testing()
add_test(NAME protocol_tests COMMAND protocol_tests)
