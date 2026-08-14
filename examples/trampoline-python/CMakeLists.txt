# Standalone build of the pybind11 *trampoline* example.
#
# `auto_pybind.cpp` is produced by rosetta's Python backend (rosetta::generate):
# because Shape's methods are virtual, the backend emitted a Py_Shape trampoline
# and bound the class as py::class_<Shape, Py_Shape>, so a Python subclass can
# override the C++ virtuals.
#
# From this directory:
#   cmake -G Ninja -B build && cmake --build build
#   python3 test_shapes.py
#
# Requires pybind11 (e.g. `pip install pybind11`) and NOTHING else: the
# generated binding is fully expanded, so a stock compiler builds it. Only
# rosetta's own generator needs the clang-p2996 fork.

cmake_minimum_required(VERSION 3.28)

project(auto_pybind_trampoline_example CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Pin to the active `python3` so pybind11 doesn't pick up a different
# interpreter when several Pythons coexist.
execute_process(
    COMMAND python3 -c "import sys; print(sys.executable)"
    OUTPUT_VARIABLE Python_EXECUTABLE
    OUTPUT_STRIP_TRAILING_WHITESPACE)
set(Python3_EXECUTABLE "${Python_EXECUTABLE}" CACHE FILEPATH "" FORCE)
set(PYBIND11_FINDPYTHON ON)

if(NOT pybind11_DIR)
    execute_process(
        COMMAND ${Python_EXECUTABLE} -m pybind11 --cmakedir
        OUTPUT_VARIABLE pybind11_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
find_package(pybind11 REQUIRED CONFIG)

pybind11_add_module(shapes auto_pybind.cpp)

# Place the module next to the Python script so it imports without PYTHONPATH.
add_custom_command(TARGET shapes POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:shapes>
        ${CMAKE_CURRENT_SOURCE_DIR}/$<TARGET_FILE_NAME:shapes>)
