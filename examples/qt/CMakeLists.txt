# Standalone build of the auto-Qt-Widgets example.
#
# From this directory:
#   cmake -G Ninja -B build && cmake --build build
#   ./build/auto_qt
#
# Requires:
#   - The clang-p2996 fork at $HOME/devs/c++/clang-p2996/build
#   - Qt 6 (defaults to $HOME/Qt/6.5.3/macos; override with -DQT_DIR=...)

cmake_minimum_required(VERSION 3.28)

# Reflection toolchain: clang-p2996 when installed, else GCC 16+.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/RosettaReflection.cmake)

set(QT_DIR "$ENV{HOME}/Qt/6.5.3/macos"
    CACHE PATH "Qt 6 install prefix")

project(auto_qt_example CXX)

rosetta_reflection_setup()

# AUTOMOC is enabled because Qt's own widget headers contain Q_OBJECT.
# Our own code defines no Q_OBJECT class — the visitor wires lambdas
# directly into QPushButton::clicked etc.
set(CMAKE_AUTOMOC ON)

list(PREPEND CMAKE_PREFIX_PATH "${QT_DIR}")
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(auto_qt main.cpp)

target_include_directories(auto_qt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include)

target_link_libraries(auto_qt PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets)

# Reflection flags only on the TU that includes <rosetta/walk.h>;
# the deprecation warning is libc++26 flagging Qt's use of is_trivial_v.
set(_rosetta_main_opts ${ROSETTA_REFLECTION_COMPILE_OPTIONS} ${ROSETTA_ANNOTATION_FLAGS}
    -Wno-deprecated-declarations)
set_source_files_properties(main.cpp PROPERTIES
    COMPILE_OPTIONS "${_rosetta_main_opts}")

target_link_options(auto_qt PRIVATE ${ROSETTA_REFLECTION_LINK_OPTIONS})
