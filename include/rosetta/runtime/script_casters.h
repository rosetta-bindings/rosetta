// Copyright (c) fmaerten@gmail.com
// License: MIT
//
// Every per-language type caster for rosetta::script::Value, in one include.
//
// WHAT THEY DO. They turn
//
//     m.set("opacity", meta.Value.number(0.5))
//     m.get("opacity").value().as_number()
//
// into
//
//     m.set("opacity", 0.5)
//     m.get("opacity").value()
//
// WHY THEY ARE HAND-WRITTEN AND EVERYTHING ELSE IS GENERATED. Value is type
// erasure: one C++ type standing for "whatever the host language just handed
// us". Reflection can describe it — it cannot know that a Python float, a Lua
// number and a JS Number should all become the same box. That mapping is
// per-language knowledge, so it is written per language, once.
//
// Only half of each caster is really per-language, though. READING a host value
// is irreducible: only Python knows what PyFloat_Check means. WRITING one is the
// same shape everywhere — switch on the kind, call the host's constructor — and
// that half is rosetta::script::visit() in <rosetta/script.h>. Each file below
// supplies a sink of nine small methods and nothing else, so no two casters can
// drift apart on whether an enum is an integer or what an unclaimed type
// renders as.
//
//     script/python.h   pybind11::detail::type_caster<Value>
//     script/lua.h      sol::stack::unqualified_{getter,pusher,checker}
//     script/node.h     rosetta::to_napi / from_napi specializations
//     script/wasm.h     emscripten::internal::BindingType<Value>   (opt-in)
//
// ---------------------------------------------------------------------------
// HOW THEY GET IN
//
// One manifest line, in the project that binds <rosetta/script.h>:
//
//     "module_init": { "headers": ["rosetta/runtime/script_casters.h", ...] }
//
// module_init is the only hook that emits an #include AFTER the framework header
// and BEFORE the bound classes — exactly the window a caster needs, with the
// framework's customization points declared and no binding instantiated yet.
//
// module_init.headers is global rather than per-target, so this include reaches
// every backend. That is why each file guards its whole body on a macro only its
// own target defines (PYBIND11_VERSION_MAJOR, SOL_VERSION_STRING, NAPI_VERSION,
// __EMSCRIPTEN__): the three that are not the current target expand to nothing,
// and so does the whole header for the backends with no caster concept (julia,
// csharp, java) and the documentation targets, which ignore module_init
// entirely. Include one file directly instead if you would rather be explicit.
//
// ---------------------------------------------------------------------------
// ONE THING THE MANIFEST MUST STILL DO
//
// Value has to stay in the manifest's `classes` list. The type gate decides at
// generation time whether a member is bindable and cannot know a caster will
// exist, so dropping Value would silently skip every member that mentions it —
// Outcome::value, Instance::set, Instance::call, the lot. The cost is one
// vestigial `Value` type per module that nothing ever produces, because the
// casters intercept every crossing.

#pragma once

#include "script/lua.h"
#include "script/node.h"
#include "script/python.h"
#include "script/wasm.h"
