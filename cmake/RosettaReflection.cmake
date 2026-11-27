# RosettaReflection.cmake — the C++26 reflection toolchain for rosetta's own
# reflection-using builds (tests/, examples/docgen, examples/moc, ...).
#
# Two supported compilers:
#   * clang-p2996 (Bloomberg fork) — links the fork's own libc++.
#   * GCC 16+ (-freflection)       — its regular libstdc++; Linux, MinGW, ...
#
# Usage — include BEFORE project(), call the setup AFTER it:
#
#   include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RosettaReflection.cmake)
#   project(foo CXX)
#   rosetta_reflection_setup()
#   target_compile_options(t PRIVATE ${ROSETTA_REFLECTION_COMPILE_OPTIONS}
#                                    ${ROSETTA_ANNOTATION_FLAGS})
#   target_link_options(t PRIVATE ${ROSETTA_REFLECTION_LINK_OPTIONS})
#
# rosetta_reflection_setup() sets C++26 and fills three variables:
#   ROSETTA_REFLECTION_COMPILE_OPTIONS  — reflection on (every TU that reflects)
#   ROSETTA_REFLECTION_LINK_OPTIONS     — the fork's libc++; empty for GCC
#   ROSETTA_ANNOTATION_FLAGS            — P3394 [[=...]] annotations; the fork
#                                         gates them behind a flag, GCC 16
#                                         enables them with -freflection
#
# Compiler choice: an explicit -DCMAKE_CXX_COMPILER wins; otherwise the fork at
# CLANG_P2996_ROOT when it exists; otherwise CMake's default compiler, which
# rosetta_reflection_setup() then requires to be GCC 16+.
#
# (The generator projects emitted by rosetta_gen carry the same logic inline,
# so they stay self-contained — see tools/rosetta_gen/emit.cpp.)

set(CLANG_P2996_ROOT "$ENV{HOME}/devs/c++/clang-p2996/build"
    CACHE PATH "Bloomberg clang-p2996 fork build directory")

if(NOT CMAKE_CXX_COMPILER)
    if(EXISTS "${CLANG_P2996_ROOT}/bin/clang++")
        set(CMAKE_C_COMPILER   "${CLANG_P2996_ROOT}/bin/clang")
        set(CMAKE_CXX_COMPILER "${CLANG_P2996_ROOT}/bin/clang++")
    else()
        message(STATUS "rosetta: no clang-p2996 at ${CLANG_P2996_ROOT} "
                       "— using the default C++ compiler (needs GCC 16+)")
    endif()
endif()

macro(rosetta_reflection_setup)
    set(CMAKE_CXX_STANDARD 26)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
            message(FATAL_ERROR "rosetta: GCC ${CMAKE_CXX_COMPILER_VERSION} has no "
                "reflection — need GCC 16+ or clang-p2996 (-DCLANG_P2996_ROOT=...)")
        endif()
        # Raised constexpr budgets: the reflection walk runs at compile time.
        set(ROSETTA_REFLECTION_COMPILE_OPTIONS
            -freflection
            -fconstexpr-ops-limit=68719476736
            -fconstexpr-loop-limit=1000000000)
        set(ROSETTA_REFLECTION_LINK_OPTIONS "")
        if(WIN32)
            # MinGW: std::print's terminal helpers live in libstdc++exp. A
            # library (after the objects), not a link option (before them).
            link_libraries(stdc++exp)
        endif()
        set(ROSETTA_ANNOTATION_FLAGS "")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(ROSETTA_REFLECTION_COMPILE_OPTIONS
            -freflection
            -freflection-latest
            -fexperimental-library)
        set(ROSETTA_REFLECTION_LINK_OPTIONS
            -nostdlib++
            -L${CLANG_P2996_ROOT}/lib
            -Wl,-rpath,${CLANG_P2996_ROOT}/lib
            -lc++
            -lc++abi)
        set(ROSETTA_ANNOTATION_FLAGS -fannotation-attributes)
    else()
        message(FATAL_ERROR "rosetta: ${CMAKE_CXX_COMPILER_ID} has no C++26 "
            "reflection — need GCC 16+ or clang-p2996 (-DCLANG_P2996_ROOT=...)")
    endif()
endmacro()
