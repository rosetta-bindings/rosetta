# Standalone build of the auto-REST example.
#
# From this directory:
#   cmake -G Ninja -B build && cmake --build build
#   ./build/auto_rest          # listens on 127.0.0.1:8080
#   bash test_rest.sh          # exercises the endpoints
#
# Pulls cpp-httplib and nlohmann/json via FetchContent — no system deps.

cmake_minimum_required(VERSION 3.28)

# Reflection toolchain: clang-p2996 when installed, else GCC 16+.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/RosettaReflection.cmake)

project(auto_rest_example CXX)

rosetta_reflection_setup()

include(FetchContent)
FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.18.1)
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3)
FetchContent_MakeAvailable(httplib json)

add_executable(auto_rest auto_rest.cpp)

target_include_directories(auto_rest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include)

target_link_libraries(auto_rest PRIVATE
    httplib::httplib
    nlohmann_json::nlohmann_json)

target_compile_options(auto_rest PRIVATE
    ${ROSETTA_REFLECTION_COMPILE_OPTIONS} ${ROSETTA_ANNOTATION_FLAGS})

target_link_options(auto_rest PRIVATE ${ROSETTA_REFLECTION_LINK_OPTIONS})
