# Standalone build of the auto-pybind11 example.
#
# From this directory:
#   cmake -G Ninja -B build && cmake --build build
#   python3 test_reflected_person.py
#
# Requires the clang-p2996 fork at $HOME/devs/c++/clang-p2996/build
# and pybind11 installed (e.g. `pip install pybind11`).

cmake_minimum_required(VERSION 3.28)

# Reflection toolchain: clang-p2996 when installed, else GCC 16+.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/RosettaReflection.cmake)

project(auto_pybind_example CXX)

rosetta_reflection_setup()

# Pin to the active `python3` so pybind11 doesn't pick up a different
# interpreter (e.g. Homebrew 3.14) when several Pythons coexist. Without
# this the built .so can be ABI-incompatible with the runtime python.
execute_process(
    COMMAND python3 -c "import sys; print(sys.executable)"
    OUTPUT_VARIABLE Python_EXECUTABLE
    OUTPUT_STRIP_TRAILING_WHITESPACE)
set(Python3_EXECUTABLE "${Python_EXECUTABLE}" CACHE FILEPATH "" FORCE)
set(PYBIND11_FINDPYTHON ON)

# Locate pybind11. If it's installed via pip, hint cmake at python3.
if(NOT pybind11_DIR)
    execute_process(
        COMMAND ${Python_EXECUTABLE} -m pybind11 --cmakedir
        OUTPUT_VARIABLE pybind11_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
find_package(pybind11 REQUIRED CONFIG)

# NO_EXTRAS disables LTO, strip, visibility=hidden, etc. Required because
# pybind11_add_module force-adds `-flto`, and Apple's system linker
# (libLTO 17) can't parse bitcode produced by the clang-p2996 fork (LLVM 21+).
pybind11_add_module(reflected_person NO_EXTRAS auto_pybind.cpp)

target_include_directories(reflected_person PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include)

target_compile_options(reflected_person PRIVATE
    ${ROSETTA_REFLECTION_COMPILE_OPTIONS} ${ROSETTA_ANNOTATION_FLAGS})

target_link_options(reflected_person PRIVATE ${ROSETTA_REFLECTION_LINK_OPTIONS})

# Place the .so right next to the Python script so it can be imported
# without setting PYTHONPATH. Copying after build is more robust than
# LIBRARY_OUTPUT_DIRECTORY, which doesn't always cover BUNDLE outputs on macOS.
add_custom_command(TARGET reflected_person POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:reflected_person>
        ${CMAKE_CURRENT_SOURCE_DIR}/$<TARGET_FILE_NAME:reflected_person>)
