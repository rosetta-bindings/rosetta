# Standalone build of the mini_moc example.
#
# From this directory:
#   cmake -G Ninja -B build && cmake --build build
#   ./build/moc

cmake_minimum_required(VERSION 3.28)

# Reflection toolchain: clang-p2996 when installed, else GCC 16+.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/RosettaReflection.cmake)

project(mini_moc_example CXX)

rosetta_reflection_setup()

add_executable(moc main.cpp)

target_include_directories(moc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include)

target_compile_options(moc PRIVATE
    ${ROSETTA_REFLECTION_COMPILE_OPTIONS} ${ROSETTA_ANNOTATION_FLAGS})

target_link_options(moc PRIVATE ${ROSETTA_REFLECTION_LINK_OPTIONS})
