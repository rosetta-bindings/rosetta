# Standalone build of the auto-QML example.
#
# From this directory:
#   cmake -G Ninja -B build && cmake --build build
#   ./build/auto_qml
#
# Requires:
#   - The clang-p2996 fork at $HOME/devs/c++/clang-p2996/build
#   - Qt 6 (defaults to $HOME/Qt/6.5.3/macos; override with -DQT_DIR=...)

cmake_minimum_required(VERSION 3.28)

# Reflection toolchain: clang-p2996 when installed, else GCC 16+.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/RosettaReflection.cmake)

set(QT_DIR "$ENV{HOME}/Qt/6.5.3/macos"
    CACHE PATH "Qt 6 install prefix")

project(auto_qml_example CXX)

rosetta_reflection_setup()

# moc / rcc need to run on the wrapper QObject header.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

list(PREPEND CMAKE_PREFIX_PATH "${QT_DIR}")
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.5)

# The generic QML backend now lives in the rosetta include tree.
set(ROSETTA_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../include")
set(ROSETTA_QML "${ROSETTA_INCLUDE}/rosetta/visitors")

# The Q_OBJECT bridge sits in an include dir rather than next to main.cpp,
# so list its absolute path here for AUTOMOC to pick it up and moc it.
qt_add_executable(auto_qml
    main.cpp
    ${ROSETTA_QML}/qml_reflected_object.h
)

# Ship the generic inspector markup under a stable resource name
# (Inspector.qml) even though it lives outside this source dir.
set_source_files_properties(${ROSETTA_QML}/qml/Inspector.qml
    PROPERTIES QT_RESOURCE_ALIAS Inspector.qml)

qt_add_qml_module(auto_qml
    URI ReflectedQml
    VERSION 1.0
    QML_FILES ${ROSETTA_QML}/qml/Inspector.qml
)

target_include_directories(auto_qml PRIVATE ${ROSETTA_INCLUDE})

target_link_libraries(auto_qml PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2)

# Reflection flags are only needed on TUs that include <rosetta/walk.h>.
# Scoping them to the source file keeps moc-generated code away from
# experimental compiler features.
set(_rosetta_main_opts ${ROSETTA_REFLECTION_COMPILE_OPTIONS} ${ROSETTA_ANNOTATION_FLAGS}
    -Wno-deprecated-declarations)
set_source_files_properties(main.cpp PROPERTIES
    COMPILE_OPTIONS "${_rosetta_main_opts}")

target_link_options(auto_qml PRIVATE ${ROSETTA_REFLECTION_LINK_OPTIONS})
