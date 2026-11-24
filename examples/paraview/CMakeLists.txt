# Standalone build of the ParaView XML example.
#
# From this directory:
#   cmake -G Ninja -B build && cmake --build build
#   ./build/paraview_xml                   # print Server Manager XML to stdout
#   ./build/paraview_xml > Threshold.xml   # capture the plugin XML

cmake_minimum_required(VERSION 3.28)

# Reflection toolchain: clang-p2996 when installed, else GCC 16+.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/RosettaReflection.cmake)

project(paraview_xml_example CXX)

rosetta_reflection_setup()

add_executable(paraview_xml main.cpp)

target_include_directories(paraview_xml PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include)

target_compile_options(paraview_xml PRIVATE
    ${ROSETTA_REFLECTION_COMPILE_OPTIONS} ${ROSETTA_ANNOTATION_FLAGS})

target_link_options(paraview_xml PRIVATE ${ROSETTA_REFLECTION_LINK_OPTIONS})
