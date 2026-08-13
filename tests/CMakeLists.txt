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

# The other half of inheritance: GenClass::bases reaching py::class_<T, Base>
# and nb::class_<T, Base>, so a derived instance converts to its base at the
# boundary. Includes nanobind's single-base rule. Verifies the emitted source.
add_executable(base_classes base_classes.cpp)
target_include_directories(base_classes PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_compile_options(base_classes PRIVATE -fannotation-attributes)
target_link_libraries(base_classes PRIVATE GTest::gtest_main)
gtest_discover_tests(base_classes)

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

# Qualified C++ spelling of class template specializations: template arguments
# keep their namespaces, and libc++'s std::__1 never leaks into emitted code.
add_executable(template_spelling template_spelling.cpp)
target_include_directories(template_spelling PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_compile_options(template_spelling PRIVATE -fannotation-attributes)
target_link_libraries(template_spelling PRIVATE GTest::gtest_main)
gtest_discover_tests(template_spelling)

# Overload support in the reflection walk: a class's whole overload set reaches
# the IR, C++ name hiding is honoured across bases, and what the walk cannot bind
# is recorded rather than silently dropped.
add_executable(overload overload.cpp)
target_include_directories(overload PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_compile_options(overload PRIVATE -fannotation-attributes)
target_link_libraries(overload PRIVATE GTest::gtest_main)
gtest_discover_tests(overload)

# Overload selection on FREE functions (manifest "signature"): one member of a
# namespace-scope overload set reaches the IR without a reflection of it, and the
# backends that form a function pointer disambiguate with a static_cast.
add_executable(free_overload free_overload.cpp)
target_include_directories(free_overload PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_compile_options(free_overload PRIVATE -fannotation-attributes)
target_link_libraries(free_overload PRIVATE GTest::gtest_main)
gtest_discover_tests(free_overload)

# std::filesystem::path as a string boundary + std::shared_ptr on the
# caster-less backends (embind smart_ptr, the N-API adopt path, sol2's native
# handling) — the two gaps that kept a factory API off those targets.
add_executable(path_shared path_shared.cpp)
target_include_directories(path_shared PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_compile_options(path_shared PRIVATE -fannotation-attributes)
target_link_libraries(path_shared PRIVATE GTest::gtest_main)
gtest_discover_tests(path_shared)

# The two manifest fields for what is not a binding: "module_init" (statements
# run at module load) and "generated_headers" (a header the bound library's own
# build system would have produced).
add_executable(module_init module_init.cpp)
target_include_directories(module_init PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_compile_options(module_init PRIVATE -fannotation-attributes)
target_link_libraries(module_init PRIVATE GTest::gtest_main)
gtest_discover_tests(module_init)

# Out-parameters (manifest "out_params"): a value a function returns through a
# reference leaves the exposed signature and joins the return — a tuple in
# Python, multiple returns in Lua, an array in JS. Never inferred.
add_executable(out_params out_params.cpp)
target_include_directories(out_params PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_compile_options(out_params PRIVATE -fannotation-attributes)
target_link_libraries(out_params PRIVATE GTest::gtest_main)
gtest_discover_tests(out_params)

# The machine-readable coverage report (coverage.json): every bind/skip decision
# is recorded with a reason, and the JSON parses.
add_executable(coverage coverage.cpp)
target_include_directories(coverage PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_compile_options(coverage PRIVATE -fannotation-attributes)
target_link_libraries(coverage PRIVATE GTest::gtest_main nlohmann_json::nlohmann_json)
gtest_discover_tests(coverage)

# Foreign-sequence marshalling (rosetta::is_sequence — GEO::vector-style
# containers through a std::vector boundary). Verifies generated sources.
add_executable(sequence sequence.cpp)
target_include_directories(sequence PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_compile_options(sequence PRIVATE -fannotation-attributes)
target_link_libraries(sequence PRIVATE GTest::gtest_main)
gtest_discover_tests(sequence)

# External-library link block (manifest "user_lib", one library or a list of
# them). Verifies the CMake text emitted into every compiled backend.
add_executable(user_lib user_lib.cpp)
target_include_directories(user_lib PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_compile_options(user_lib PRIVATE -fannotation-attributes)
target_link_libraries(user_lib PRIVATE GTest::gtest_main)
gtest_discover_tests(user_lib)

# std::shared_ptr marshalling (GenType::is_shared_ptr + the python-expanded
# holder registration), the pybind11 override-macro comma fix, and nanobind's
# per-header STL casters / abstract-class constructor guard. Generated sources.
add_executable(shared_ptr shared_ptr.cpp)
target_include_directories(shared_ptr PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_compile_options(shared_ptr PRIVATE -fannotation-attributes)
target_link_libraries(shared_ptr PRIVATE GTest::gtest_main)
gtest_discover_tests(shared_ptr)

# The manifest "expose" rename on free functions and extension methods
# (make_function's override + every backend's exposed-name / qualified-call
# split). Verifies generated sources.
add_executable(expose expose.cpp)
target_include_directories(expose PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_compile_options(expose PRIVATE -fannotation-attributes)
target_link_libraries(expose PRIVATE GTest::gtest_main)
gtest_discover_tests(expose)

# Enumerations nested inside a class: GenEnum::qualified must carry the
# enclosing class, while name_space stays namespace-only (it becomes a
# `using namespace`). Verifies generated sources.
# Foreign-library interop ("interop": ["eigen"]): the walk marks types by
# enclosing namespace, so the suite runs against a stub Eigen namespace and
# needs no Eigen dependency.
add_executable(interop_eigen interop_eigen.cpp)
target_include_directories(interop_eigen PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_compile_options(interop_eigen PRIVATE -fannotation-attributes)
target_link_libraries(interop_eigen PRIVATE GTest::gtest_main)
gtest_discover_tests(interop_eigen)

# Per-target artifact directory (manifest target "out_dir"): the built module
# is copied where the project wants it, wasm's .js+.wasm pair included.
add_executable(out_dir out_dir.cpp)
target_include_directories(out_dir PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_compile_options(out_dir PRIVATE -fannotation-attributes)
target_link_libraries(out_dir PRIVATE GTest::gtest_main)
gtest_discover_tests(out_dir)

# Per-target runtime pins (manifest "python" / "requires_python" /
# "napi_version" / "node_engine") in the emitted CMake, pyproject and package.json.
add_executable(runtime_pins runtime_pins.cpp)
target_include_directories(runtime_pins PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_compile_options(runtime_pins PRIVATE -fannotation-attributes)
target_link_libraries(runtime_pins PRIVATE GTest::gtest_main)
gtest_discover_tests(runtime_pins)

# Reference returns in the Python family: a non-const lvalue-ref return of a
# bound class binds with reference_internal instead of being copied.
add_executable(ref_return ref_return.cpp)
target_include_directories(ref_return PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_compile_options(ref_return PRIVATE -fannotation-attributes)
target_link_libraries(ref_return PRIVATE GTest::gtest_main)
gtest_discover_tests(ref_return)

# Foreign 2-D matrices (rosetta::is_matrix — Eigen::MatrixXd-shaped grids
# through a vector-of-rows boundary). Verifies generated sources.
add_executable(matrix matrix.cpp)
target_include_directories(matrix PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_compile_options(matrix PRIVATE -fannotation-attributes)
target_link_libraries(matrix PRIVATE GTest::gtest_main)
gtest_discover_tests(matrix)

# The flat-array escape hatch: a concrete interop-owned type ALSO registered as
# a sequence, so the caster-less backends bind it as an array while the Python
# family keeps numpy. Same stub-Eigen trick as above.
add_executable(interop_sequence interop_sequence.cpp)
target_include_directories(interop_sequence PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_compile_options(interop_sequence PRIVATE -fannotation-attributes)
target_link_libraries(interop_sequence PRIVATE GTest::gtest_main)
gtest_discover_tests(interop_sequence)

add_executable(nested_enum nested_enum.cpp)
target_include_directories(nested_enum PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_compile_options(nested_enum PRIVATE -fannotation-attributes)
target_link_libraries(nested_enum PRIVATE GTest::gtest_main)
gtest_discover_tests(nested_enum)

