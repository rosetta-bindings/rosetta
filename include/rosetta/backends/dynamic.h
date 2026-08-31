// Copyright (c) fmaerten@gmail.com
// License: MIT

// Dynamic object-model backend — emits rosetta's IR as runtime DATA.
//
// Every other backend turns the IR into calls into some framework: pybind11
// class_ registrations, N-API property descriptors, sol2 usertypes, Qt
// property tables. This one turns it into the IR again — a static
// rosetta::dyn::MetaClass per bound type, with one captureless-lambda thunk per
// member — so the target process can ask what exists and call it by name.
//
// Why that is worth a backend rather than a library:
//
//   * ONE WRAPPER PER LANGUAGE. A Python / Lua / JS binding written against
//     <rosetta/dynamic.h> is a few hundred lines that speak Any and ArgList,
//     and it does not grow when the bound library does. Today each of those is
//     N generated registration blocks, and each backend re-derives its own
//     marshalling rules (~16.5k lines across the tree — docs/MAIN-TODO.md §3).
//
//   * UI BY QUERY. Walking registry().classes() and reading MetaField::range /
//     choices / readonly / doc is how you build a property inspector with no
//     generated code at all. rosetta already contains three hand-rolled
//     versions of exactly this (visitors/qml_reflected_object.h,
//     runtime/qt_widgets.h, runtime/imgui.h), each against a
//     different Any type.
//
//   * OVERLOADS COME BACK. The metadata keeps the whole overload set and
//     rosetta::dyn::resolve() scores it against the actual arguments. The
//     name-keyed targets (node, wasm, C#, Java, REST, lua) can only register a
//     name once and drop every sibling (coverage::overloads::first_only); the
//     dynamic model has no such limit, and a failed call names every candidate.
//
// Registered under the "dynamic" target. The emitted sources are stock C++20 —
// no reflection on the target, exactly like the -expanded backends — because
// what gets written out is aggregate initializers, not splices. The emitted
// tree is a LIBRARY plus a small `inspect` demo that prints the whole registry,
// which is the shortest demonstration that the metadata is real.
//
// Part of the generate pipeline (included by inline/generate.hxx after the
// shared render helpers and after backends/python.h, whose qualify_std() it
// reuses). The implementation lives in inline/dynamic.hxx.

#pragma once

namespace rosetta {
    namespace backend {
        using namespace gen_detail; // shared render / IR helpers

        struct Dynamic : Backend {
            void        emit(const GenContext &c) const override;
            std::string render(const GenContext &c) const override;
        };

    } // namespace backend
} // namespace rosetta

#include "inline/dynamic.hxx"
