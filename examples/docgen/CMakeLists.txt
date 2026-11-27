# Standalone build of the auto-docgen example.
#
# From this directory:
#   cmake -G Ninja -B build && cmake --build build
#   ./build/auto_docgen              # print Markdown to stdout
#   ./build/auto_docgen > Person.md  # capture to a file

cmake_minimum_required(VERSION 3.28)

# Reflection toolchain: clang-p2996 when installed, else GCC 16+.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/RosettaReflection.cmake)

project(auto_docgen_example CXX)

rosetta_reflection_setup()

add_executable(auto_docgen main.cpp)

target_include_directories(auto_docgen PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include)

target_compile_options(auto_docgen PRIVATE
    ${ROSETTA_REFLECTION_COMPILE_OPTIONS} ${ROSETTA_ANNOTATION_FLAGS})

target_link_options(auto_docgen PRIVATE ${ROSETTA_REFLECTION_LINK_OPTIONS})
