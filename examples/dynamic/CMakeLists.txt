# Builds the hand-written consumers NEXT TO the generated metadata.
#
# Everything under bindings/ is regenerated output and must never be edited —
# so the consumers live here, outside it, and simply compile the generated
# translation unit alongside their own. That is the same layering the README
# describes for extending a generated binding in C++.
#
# Three targets, all from ONE set of generated tables:
#   demo    — terminal session (always built)
#   inspect — the registry walker the backend emits for free (always built)
#   viewer  — Qt 3D view + generated property panel + console (needs Qt 6)
#
# Stock C++20 throughout: no reflection, no clang-p2996. The only rosetta
# header involved is <rosetta/dynamic.h>, which is ordinary C++.
cmake_minimum_required(VERSION 3.18)
project(scene_dynamic_demo CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(GENERATED "${CMAKE_CURRENT_SOURCE_DIR}/bindings/dynamic")
if(NOT EXISTS "${GENERATED}/auto_dynamic.cpp")
    message(FATAL_ERROR
        "bindings/dynamic/auto_dynamic.cpp is missing — run ./run.sh, or generate it with\n"
        "    ../../bin/rosetta_gen manifest.json gen && cmake -S gen -B gen/build && "
        "cmake --build gen/build && ./generator bindings")
endif()

# The generated metadata, built once and shared by every consumer.
add_library(scene_meta STATIC ${GENERATED}/auto_dynamic.cpp)
target_include_directories(scene_meta PUBLIC
    ${GENERATED}                               # auto_dynamic.h
    ${CMAKE_CURRENT_SOURCE_DIR}                # scene.h (for the generated TU), interp.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include) # <rosetta/dynamic.h>

add_executable(demo demo.cpp)
target_link_libraries(demo PRIVATE scene_meta)
target_compile_options(demo PRIVATE -Wall -Wextra)

add_executable(inspect ${GENERATED}/inspect.cpp)
target_link_libraries(inspect PRIVATE scene_meta)

# ---------------------------------------------------------------------------
# Qt viewer — optional
# ---------------------------------------------------------------------------
# Defaults to rosetta's usual Qt prefix; override with -DQT_DIR=/path/to/Qt/6.x/<platform>.
set(QT_DIR "$ENV{HOME}/Qt/6.8.3/macos" CACHE PATH "Qt 6 install prefix")
list(PREPEND CMAKE_PREFIX_PATH "${QT_DIR}")

# macOS 15 removed the AGL framework, but Qt's FindWrapOpenGL.cmake still falls
# back to a bare `-framework AGL` when it cannot find one, which fails at link
# time. Pre-seeding its cache variable with the (still present) OpenGL framework
# short-circuits that fallback; linking OpenGL twice is harmless. Remove this
# once Qt ships the fix.
# (Test the SDK, not the running system: macOS still ships an AGL.framework
# stub outside the SDK, so a plain find_library finds one the linker cannot use.)
if(APPLE AND CMAKE_OSX_SYSROOT AND
   NOT EXISTS "${CMAKE_OSX_SYSROOT}/System/Library/Frameworks/AGL.framework")
    find_library(_rosetta_gl NAMES OpenGL)
    set(WrapOpenGL_AGL "${_rosetta_gl}" CACHE FILEPATH
        "Stand-in for the AGL framework the macOS 15 SDK removed" FORCE)
endif()

find_package(Qt6 6.5 QUIET COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
if(NOT Qt6_FOUND)
    message(STATUS
        "Qt 6 not found — skipping the `viewer` target. "
        "Pass -DQT_DIR=/path/to/Qt/6.x/<platform> to build it.")
    return()
endif()

message(STATUS "Qt 6 found at ${QT_DIR} — building the `viewer` target")
set(CMAKE_AUTOMOC ON)

# The Q_OBJECT classes live in headers, so list them: AUTOMOC only scans
# headers it is told about (or that share a basename with a source file).
add_executable(viewer
    qt/viewer.cpp
    qt/mainwindow.h
    qt/sceneview.h
    qt/propertypanel.h
    qt/console.h)

target_link_libraries(viewer PRIVATE
    scene_meta
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)
