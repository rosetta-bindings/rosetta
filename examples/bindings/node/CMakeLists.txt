# Standalone build of the auto-N-API example.
#
# From this directory:
#   npm install                        # pulls node-addon-api + cmake-js
#   npx cmake-js compile               # configures + builds (sets CMAKE_JS_INC)
#   node test_reflected_person.js
#
# Requires the clang-p2996 fork at $HOME/devs/c++/clang-p2996/build
# (override with -DCLANG_P2996_ROOT=...).

cmake_minimum_required(VERSION 3.28)

# Reflection toolchain: clang-p2996 when installed, else GCC 16+.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/RosettaReflection.cmake)

project(auto_napi_example CXX)

rosetta_reflection_setup()

# node-addon-api headers — installed by `npm install` into node_modules.
if(NOT NODE_ADDON_API_DIR)
    execute_process(
        COMMAND node -p "require('node-addon-api').include"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE NODE_ADDON_API_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX REPLACE "^\"|\"$" "" NODE_ADDON_API_DIR "${NODE_ADDON_API_DIR}")
endif()

# Build target as a .node addon. CMAKE_JS_INC/CMAKE_JS_SRC/CMAKE_JS_LIB
# are populated by cmake-js when invoked via `npx cmake-js compile`.
add_library(reflected_person SHARED auto_napi.cpp ${CMAKE_JS_SRC})

set_target_properties(reflected_person PROPERTIES
    PREFIX ""
    SUFFIX ".node")

target_include_directories(reflected_person PRIVATE
    ${CMAKE_JS_INC}
    ${NODE_ADDON_API_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include)

target_link_libraries(reflected_person PRIVATE ${CMAKE_JS_LIB})

target_compile_definitions(reflected_person PRIVATE
    NAPI_VERSION=8)

target_compile_options(reflected_person PRIVATE
    ${ROSETTA_REFLECTION_COMPILE_OPTIONS} ${ROSETTA_ANNOTATION_FLAGS})

target_link_options(reflected_person PRIVATE ${ROSETTA_REFLECTION_LINK_OPTIONS})

if(APPLE)
    # Node symbols are looked up at load time inside the host process.
    target_link_options(reflected_person PRIVATE
        -Wl,-undefined,dynamic_lookup)
endif()

# Copy the .node next to the JS test so `require('./reflected_person.node')`
# works without juggling output directories.
add_custom_command(TARGET reflected_person POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:reflected_person>
        ${CMAKE_CURRENT_SOURCE_DIR}/$<TARGET_FILE_NAME:reflected_person>)
