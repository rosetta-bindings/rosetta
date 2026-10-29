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

find_package(Qt6 6.5 QUIET COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Network)
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
    qt/console.h
    qt/claudepanel.h
    qt/scenesync.h)

target_link_libraries(viewer PRIVATE
    scene_meta
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Network)

# ---- the Claude panel ------------------------------------------------------
# It talks to an in-process rosetta::mcp::Server (<rosetta/runtime/mcp.h>),
# which needs nlohmann/json; Lua adds run_script, Python (opt-in) run_python.
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3)
    FetchContent_MakeAvailable(json)
endif()
target_link_libraries(viewer PRIVATE nlohmann_json::nlohmann_json)

if(APPLE AND EXISTS /opt/homebrew)
    list(APPEND CMAKE_PREFIX_PATH /opt/homebrew)
endif()
if(NOT LUA_LIBRARIES)
    find_package(Lua 5.4 EXACT QUIET)
endif()
if(NOT LUA_LIBRARIES)
    find_package(Lua 5.3 QUIET)
endif()
if(LUA_LIBRARIES)
    message(STATUS "Claude panel: run_script enabled (Lua at ${LUA_INCLUDE_DIR})")
    target_include_directories(viewer PRIVATE ${LUA_INCLUDE_DIR})
    target_link_libraries(viewer PRIVATE ${LUA_LIBRARIES})
    target_compile_definitions(viewer PRIVATE ROSETTA_MCP_LUA=1)
endif()

# run_python is NOT sandboxed: Claude's scripts would run with your
# permissions. Opt in with -DROSETTA_MCP_PYTHON=ON.
option(ROSETTA_MCP_PYTHON "Claude panel: offer run_python (embeds CPython, not sandboxed)" OFF)
if(ROSETTA_MCP_PYTHON)
    find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Embed)
    execute_process(COMMAND "${Python_EXECUTABLE}" -c "import pybind11; print(pybind11.get_cmake_dir())"
        OUTPUT_VARIABLE _pybind11_dir OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    find_package(pybind11 CONFIG QUIET HINTS "${_pybind11_dir}")
    if(NOT pybind11_FOUND)
        include(FetchContent)
        FetchContent_Declare(pybind11
            GIT_REPOSITORY https://github.com/pybind/pybind11.git
            GIT_TAG        v3.0.1)
        FetchContent_MakeAvailable(pybind11)
    endif()
    target_link_libraries(viewer PRIVATE pybind11::embed)
    target_compile_definitions(viewer PRIVATE ROSETTA_MCP_PYTHON=1)
    message(STATUS "Claude panel: run_python enabled (${Python_EXECUTABLE}, not sandboxed)")
endif()

# ---- voice input (whisper.cpp) ---------------------------------------------
# A microphone button in the Claude panel: speech is transcribed LOCALLY by
# whisper.cpp and lands in the prompt box. Audio capture is miniaudio (one
# header: CoreAudio / WASAPI / ALSA+PulseAudio), so no Qt Multimedia is needed.
#   AUTO (default): enabled when an installed whisper.cpp is found
#                   (brew install whisper-cpp, vcpkg, or -Dwhisper_DIR=...)
#   ON:             also fetch and build whisper.cpp when none is installed
#   OFF:            no voice input
# The model is a separate download:  cmake -P fetch_whisper_model.cmake
set(ROSETTA_VOICE "AUTO" CACHE STRING "Claude panel voice input: AUTO, ON or OFF")
set_property(CACHE ROSETTA_VOICE PROPERTY STRINGS AUTO ON OFF)

if(NOT ROSETTA_VOICE STREQUAL "OFF")
    find_package(whisper CONFIG QUIET)
    if(NOT whisper_FOUND AND ROSETTA_VOICE STREQUAL "ON")
        include(FetchContent)
        # Plain variables, not cache entries: they shadow whisper's options
        # (CMP0077) without leaking into the rest of the build.
        set(WHISPER_BUILD_EXAMPLES OFF)
        set(WHISPER_BUILD_TESTS    OFF)
        set(WHISPER_BUILD_SERVER   OFF)
        set(BUILD_SHARED_LIBS      OFF) # one self-contained viewer binary
        set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
        FetchContent_Declare(whisper
            GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
            GIT_TAG        v1.8.7
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(whisper)
        set(whisper_FOUND TRUE)
    endif()

    if(whisper_FOUND)
        include(FetchContent)
        # Header only: SOURCE_SUBDIR points at a directory with no
        # CMakeLists.txt, so MakeAvailable downloads without building anything.
        FetchContent_Declare(miniaudio
            GIT_REPOSITORY https://github.com/mackron/miniaudio.git
            GIT_TAG        0.11.25
            GIT_SHALLOW    TRUE
            SOURCE_SUBDIR  no-build)
        FetchContent_MakeAvailable(miniaudio)

        target_sources(viewer PRIVATE qt/voice.h qt/voice.cpp)
        target_include_directories(viewer PRIVATE ${miniaudio_SOURCE_DIR})
        target_link_libraries(viewer PRIVATE whisper)
        target_compile_definitions(viewer PRIVATE
            ROSETTA_VOICE=1
            ROSETTA_VOICE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        # miniaudio's platform backends. Windows (WASAPI) needs nothing extra;
        # on Linux it dlopens ALSA / PulseAudio / JACK at run time.
        if(APPLE)
            target_link_libraries(viewer PRIVATE
                "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
        elseif(UNIX)
            find_package(Threads REQUIRED)
            target_link_libraries(viewer PRIVATE ${CMAKE_DL_LIBS} Threads::Threads m)
        endif()

        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/models/ggml-base.en.bin")
            message(STATUS "Claude panel: voice input enabled — download a model with "
                           "`cmake -P fetch_whisper_model.cmake` (or set ROSETTA_WHISPER_MODEL)")
        else()
            message(STATUS "Claude panel: voice input enabled (whisper.cpp)")
        endif()
    else()
        message(STATUS "Claude panel: no voice input (whisper.cpp not found; "
                       "-DROSETTA_VOICE=ON fetches and builds it)")
    endif()
endif()
