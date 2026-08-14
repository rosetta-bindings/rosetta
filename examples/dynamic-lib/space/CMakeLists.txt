# A pure, stock-C++ library. NO rosetta, NO reflection, NO annotations — this
# builds with any ordinary compiler. It produces a library in ./bin that the
# rosetta-generated bindings (../rosetta) link against.
#
# Native (stock compiler) -> a SHARED library the python/node bindings dlopen:
#   cmake -S . -B build && cmake --build build
#   -> bin/libspace.{dylib,so}
#
# WebAssembly (same emsdk as the wasm binding) -> a STATIC archive:
# wasm cannot link a native .dylib/.so and has no dynamic-linking rpath, so the
# bodies must be archived and statically linked into the module instead.
#   emcmake cmake -S . -B build-wasm && cmake --build build-wasm
#   -> bin/libspace.a
cmake_minimum_required(VERSION 3.18)
project(space CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Under Emscripten a shared library is not linkable into a wasm module; build a
# static archive so the wasm binding can pull the bodies in directly.
if(EMSCRIPTEN)
    add_library(space STATIC
        src/Vector3.cpp
        src/BoundingBox.cpp)
else()
    add_library(space SHARED
        src/Vector3.cpp
        src/BoundingBox.cpp)
endif()

target_include_directories(space PUBLIC include)

# Drop the library straight into ./bin (next to include/ and src/), so the
# bindings can point their -L / rpath at a stable, predictable location. The
# wasm static archive (libspace.a) and the native shared library coexist here —
# each toolchain's linker picks the form it can use.
set_target_properties(space PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
