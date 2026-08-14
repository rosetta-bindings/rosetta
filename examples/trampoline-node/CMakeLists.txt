# Standalone build of the N-API *trampoline* example.
#
# `auto_napi.cpp` is produced by rosetta's Node backend: because Shape's methods
# are virtual, the backend emitted a Js_Shape trampoline (subclassing Shape and
# rosetta::NapiTrampoline) and bound the class with it, so a JavaScript subclass
# can override the C++ virtuals.
#
# From this directory:
#   npm install            # pulls node-addon-api + cmake-js
#   npx cmake-js compile    # configures + builds (sets CMAKE_JS_INC)
#   node test_shapes.js
#
# Requires node-addon-api and NOTHING else: the generated binding is fully
# expanded, so a stock compiler builds it (rosetta's include dir is still on the
# path for runtime/node.h, which is ordinary C++20). Only rosetta's own
# generator needs the clang-p2996 fork.

cmake_minimum_required(VERSION 3.28)

project(auto_napi_trampoline_example CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(NOT NODE_ADDON_API_DIR)
    execute_process(
        COMMAND node -p "require('node-addon-api').include"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE NODE_ADDON_API_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX REPLACE "^\"|\"$" "" NODE_ADDON_API_DIR "${NODE_ADDON_API_DIR}")
endif()

add_library(shapes SHARED auto_napi.cpp ${CMAKE_JS_SRC})

set_target_properties(shapes PROPERTIES PREFIX "" SUFFIX ".node")

target_include_directories(shapes PRIVATE
    ${CMAKE_JS_INC}
    ${NODE_ADDON_API_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include)

target_link_libraries(shapes PRIVATE ${CMAKE_JS_LIB})

target_compile_definitions(shapes PRIVATE NAPI_VERSION=8)

if(APPLE)
    target_link_options(shapes PRIVATE -Wl,-undefined,dynamic_lookup)
endif()

add_custom_command(TARGET shapes POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:shapes>
        ${CMAKE_CURRENT_SOURCE_DIR}/$<TARGET_FILE_NAME:shapes>)
