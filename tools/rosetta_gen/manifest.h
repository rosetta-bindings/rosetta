// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// The manifest model: the structs a parsed manifest.json is loaded into,
// and load() itself (manifest.cpp).
//
// Manifest shape:
//   {
//     "user_include": "./geom",
//     "rosetta_include": "../../include",
//     "generator_name": "generator_geom",           // driver tool / CMake target
//     "module_name": "geom",                        // default binding module name
//     "cpp26_root": "$ENV{HOME}/clang-p2996/build", // optional: C++26/P2996
//                                                   // toolchain root for the thin
//                                                   // (reflection) backends. Default:
//                                                   // $ENV{HOME}/devs/c++/clang-p2996/build
//     "cpp26_cxx": "clang++",                       // optional: C++ compiler (name or
//                                                   //   path). Default ${root}/bin/clang++
//     "cpp26_cc":  "clang",                         // optional: C compiler.
//                                                   //   Default ${root}/bin/clang
//     "cpp26_lib": "/path/to/fork/lib",             // optional: dir with libc++/
//                                                   //   libc++abi (-L/-rpath).
//                                                   //   Default ${root}/lib
//     "qt_dir": "$ENV{HOME}/Qt/6.8.3/macos",        // optional: Qt 6 prefix for the
//                                                   //   qt-expanded / qml-expanded
//                                                   //   backends. Default that path.
//     "build_type": "Release",                      // optional: default CMAKE_BUILD_TYPE
//                                                   //   baked into every compiled backend's
//                                                   //   CMakeLists (Debug | Release |
//                                                   //   RelWithDebInfo | MinSizeRel);
//                                                   //   -DCMAKE_BUILD_TYPE=... still wins
//     "optimization": "-O2",                        // optional: explicit -O flag added
//                                                   //   after the build type's own flags
//                                                   //   (so it wins): -O0..-O3, -Os, -Oz,
//                                                   //   -Og, -Ofast
//     "user_lib": {                                 // optional: external library to link
//       "name": "space",                            //   the bindings against (libspace.*).
//       "dir":  "../space/bin",                     //   Use when the bound headers only
//       "link": "shared"                            //   declare the API and the bodies
//     },                                            //   live in a separately-compiled lib.
//                                                   //   `dir` is relative to the manifest.
//                                                   //   `link`: "shared" (default) | "static"
//                                                   //   | "dynamic" (alias of shared) — the
//                                                   //   preferred form, with fallback to
//                                                   //   whichever is built. wasm is always
//                                                   //   static (no native .so in wasm).
//     "compile_definitions": [                      // optional: preprocessor definitions
//       "XXX_USE_BUILTIN_DEPS",                     //   ("NAME" or "NAME=VALUE") applied to
//       "XXX_WITH_HLBFGS"                           //   the driver AND every compiled
//     ],                                            //   binding target (they reach the bound
//                                                   //   headers and user_sources alike).
//     "namespace": "stressinv",                     // optional: default namespace for
//                                                   //   class/function/extension names
//                                                   //   without a "::" of their own
//                                                   //   ("Serie" -> "stressinv::Serie";
//                                                   //   qualified names pass verbatim,
//                                                   //   leading "::" = global namespace)
//     "header_dir": "stressinv",                    // optional: dir fragment prepended
//                                                   //   to every entry header
//                                                   //   ("Serie.h" -> "stressinv/Serie.h")
//     "targets": [                                  // shared by every class
//       { "lang": "python", "name": "pygeom" },     // per-target module name
//       { "lang": "wasm-expanded",                  // optional per-target linker
//         "link_options": ["-lnodefs.js"] },        //   flags (only THIS target's
//       "node"                                      //   link line — flags are
//     ],                                            //   toolchain-specific)
//     "classes": [
//       { "name": "Model", "header": "Model.h",
//         "annotations": "Model.ann.json",          // optional out-of-line annotations
//         "extensions": [                           // optional: free functions (first
//           { "name": "ext::vertices",              //   param `Model&`) exposed as
//             "header": "model_ext.h",              //   instance methods — glue for
//             "doc": "..." }                        //   members that can't cross the
//         ] },                                      //   boundary (raw ptrs, overloads)
//       { "header": "Point.h" }                     // name derived from header stem
//     ],
//     "functions": [                                // optional: free (non-member) fns
//       { "name": "transform", "header": "common.h", "doc": "..." }
//     ],                                            // name may be qualified (api::add)
//     "sequences": [                                // optional: foreign sequence
//       "GEO::vector"                               //   containers (ONE type param) —
//     ]                                             //   marshal like std::vector<T>
//   }                                               //   (see rosetta/sequence.h)

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct FunctionEntry {
    std::string name;   // (optionally qualified) C++ function name, e.g. "api::add"
    std::string header; // header declaring it
    std::string doc;    // optional manifest doc string
};

struct ClassEntry {
    std::string name;
    std::string header;
    fs::path    annotations; // optional out-of-line annotation JSON (absolute); empty if none

    // Optional "final": true — no trampoline even when the class has public
    // virtual methods (they still bind as callable methods; host-language
    // overriding is off). Also what makes the class eligible as a node
    // member-object property when it has virtuals (the alias stores a T*).
    bool final_ = false;

    // Optional extension methods ("extensions"): free functions whose first
    // parameter is `<name>&`, exposed as instance methods of the class. This
    // is the escape hatch for a library whose own members can't cross the
    // boundary (raw-pointer accessors, attribute templates, overloaded
    // helpers): the glue shrinks to stateless free functions — no wrapper
    // class — and scripts keep holding the real C++ objects.
    std::vector<FunctionEntry> extensions;
};

struct TargetEntry {
    std::string lang; // "python", "node", "rest", "web"
    std::string name; // module / library name for this backend

    // Optional extra linker flags for THIS target only ("link_options").
    // Per-target — unlike compile_definitions — because link flags are
    // toolchain-specific: e.g. "-lnodefs.js" only makes sense on a wasm
    // target and would break a native link.
    std::vector<std::string> link_options;
};

struct Manifest {
    std::vector<fs::path>      user_include;    // one or more, absolute
    fs::path                   rosetta_include; // absolute
    std::string                generator_name;  // driver tool / CMake target name
    std::vector<TargetEntry>   targets;         // backends + per-backend module name
    std::vector<ClassEntry>    classes;
    std::vector<FunctionEntry> functions; // free functions to expose

    // Optional foreign sequence containers ("sequences"): qualified template
    // names with ONE type parameter ("GEO::vector"). For each, the generated
    // bindings.h emits
    //   template <typename T>
    //   struct rosetta::is_sequence<GEO::vector<T>> : std::true_type {};
    // so the container marshals like std::vector<T> in the opted-in expanded
    // backends (see rosetta/sequence.h for the container requirements).
    std::vector<std::string>   sequences;
    std::vector<std::string>   plugins;   // extra .cpp sources (absolute) for the driver
    std::vector<std::string>   user_sources; // user .cpp/.c sources (absolute) compiled into the bindings
    std::vector<std::string>   compile_definitions; // "NAME"/"NAME=VALUE" defs for driver + bindings
    std::string                cpp26_root; // optional C++26/P2996 toolchain root (verbatim)
    std::string                cpp26_cxx;  // optional C++ compiler (name or path)
    std::string                cpp26_cc;   // optional C compiler (name or path)
    std::string                cpp26_lib;  // optional fork stdlib dir (libc++/libc++abi)
    std::string                qt_dir;     // optional Qt 6 prefix (qt/qml backends)
    std::string                user_lib_name; // optional external lib to link bindings against
    std::string                user_lib_dir;  // optional dir holding it (absolute; -L / rpath)
    std::string                user_lib_link; // "shared" (default) | "static"; wasm always static
    std::string                build_type;    // optional default CMAKE_BUILD_TYPE for every binding
    std::string                optimization;  // optional explicit -O flag overriding the build type's

    // CMake target / binary basename.
    std::string target() const { return generator_name; }
};

// Parse and validate manifest.json (// and /* */ comments tolerated).
// Relative paths resolve from the manifest's own directory; `user_sources`
// entries may be shell globs, expanded here. Throws std::runtime_error on
// anything malformed.
Manifest load(const fs::path &manifest_path);
