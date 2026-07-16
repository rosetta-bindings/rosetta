// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Opt-in registration of FOREIGN sequence containers (a library's own
// vector type — GEO::vector<T>, an aligned/pooled vector, …) so they cross
// the binding boundary like a std::vector of their element:
//
//   template <typename T>
//   struct rosetta::is_sequence<GEO::vector<T>> : std::true_type {};
//
// (or, manifest-driven: "sequences": ["GEO::vector"] — rosetta_gen emits the
// specialization above into the generated driver.)
//
// A registered type must be default-constructible and provide size(),
// resize(n), begin()/end() (const and mutable) and value_type; the element
// must itself be marshalable (arithmetic, bool, std::string or a bound
// enum). The *-expanded runtime backends then marshal it by COPY through a
// std::vector<value_type> at the boundary — scripts see a plain array/list;
// the adapter builds the foreign container before the call and flattens it
// after. Because the copy gives the callee a real lvalue, a mutable
// `Seq&` parameter binds too (input-only: in-place mutations are discarded,
// exactly like pybind11's stl.h casters for std::vector&).
//
// Deliberately opt-in (no structural detection): a vector-ish BOUND class
// must keep binding as a class, and only the author knows a container is
// spellable as `Namespace::Template<value_type>` — which is how the
// generated adapters name it (extra defaulted template parameters are fine,
// a container needing two explicit arguments is not).

#pragma once

#include <type_traits>

namespace rosetta {

    template <typename T> struct is_sequence : std::false_type {};

} // namespace rosetta
