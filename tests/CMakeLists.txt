cmake_minimum_required(VERSION 3.28)

set(CLANG_P2996_ROOT "$ENV{HOME}/devs/c++/clang-p2996/build"
    CACHE PATH "Bloomberg clang-p2996 fork build directory")

if(NOT CMAKE_CXX_COMPILER)
    set(CMAKE_C_COMPILER   "${CLANG_P2996_ROOT}/bin/clang")
    set(CMAKE_CXX_COMPILER "${CLANG_P2996_ROOT}/bin/clang++")
endif()

project(introspection-cpp26 CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

add_compile_options(
    -freflection
    -freflection-latest
    -fexperimental-library
)

add_link_options(
    -nostdlib++
    -L${CLANG_P2996_ROOT}/lib
    -Wl,-rpath,${CLANG_P2996_ROOT}/lib
    -lc++
    -lc++abi
)

# GoogleTest is built from source (FetchContent) AFTER the options above so it
# shares this toolchain's libc++ / -fexperimental-library ABI. A prebuilt gtest
# (e.g. from Homebrew) is compiled against a different libc++ and would not link.
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# nlohmann/json — header-only, used by the JSON visitor (rosetta::to_json).
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

enable_testing()
include(GoogleTest)

add_executable(docgen docgen.cpp)
target_include_directories(docgen PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_compile_options(docgen PRIVATE -fannotation-attributes)

add_executable(moc moc.cpp)
target_include_directories(moc PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_compile_options(moc PRIVATE -fannotation-attributes)
target_link_libraries(moc PRIVATE GTest::gtest_main)
gtest_discover_tests(moc)

# Out-of-line annotations: metadata supplied via the ann_json_source<T>
# customization point (the same point rosetta_gen bakes a JSON side-car into).
add_executable(annotate_json annotate_json.cpp)
target_include_directories(annotate_json PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_compile_options(annotate_json PRIVATE -fannotation-attributes)
target_link_libraries(annotate_json PRIVATE GTest::gtest_main)
gtest_discover_tests(annotate_json)

# Inherited-member flattening in walk<T>(): single inheritance, shadowing,
# and virtual-diamond collapse. Verified via to_json/from_json and to_markdown.
add_executable(inheritance inheritance.cpp)
target_include_directories(inheritance PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_compile_options(inheritance PRIVATE -fannotation-attributes)
target_link_libraries(inheritance PRIVATE GTest::gtest_main nlohmann_json::nlohmann_json)
gtest_discover_tests(inheritance)

# Python backend trampoline emission for virtual methods (consumes virtual_spec).
# Verifies the generated auto_pybind.cpp source, not a live pybind build.
add_executable(python_trampoline python_trampoline.cpp)
target_include_directories(python_trampoline PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_compile_options(python_trampoline PRIVATE -fannotation-attributes)
target_link_libraries(python_trampoline PRIVATE GTest::gtest_main)
gtest_discover_tests(python_trampoline)

# Node backend trampoline emission for virtual methods (consumes virtual_spec).
# Verifies the generated auto_napi.cpp source (no live N-API build needed).
add_executable(node_trampoline node_trampoline.cpp)
target_include_directories(node_trampoline PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_compile_options(node_trampoline PRIVATE -fannotation-attributes)
target_link_libraries(node_trampoline PRIVATE GTest::gtest_main)
gtest_discover_tests(node_trampoline)

# Member-object property binding (mesh.vertices-style aliased references) and
# overload-set gating in the *-expanded backends. Verifies generated sources.
add_executable(member_object member_object.cpp)
target_include_directories(member_object PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_compile_options(member_object PRIVATE -fannotation-attributes)
target_link_libraries(member_object PRIVATE GTest::gtest_main)
gtest_discover_tests(member_object)
