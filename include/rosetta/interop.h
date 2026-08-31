// Copyright (c) fmaerten@gmail.com
// License: MIT

// Opt-in interoperability with a FOREIGN MATH/CONTAINER LIBRARY whose types the
// target language's binding framework can already marshal on its own:
//
//   template <> struct rosetta::interop_enabled<rosetta::eigen_interop>
//       : std::true_type {};
//
// (or, manifest-driven: "interop": ["eigen"] — rosetta_gen emits the
// specialization above into the generated bindings.h.)
//
// The problem it solves: a method taking or returning Eigen::VectorXd is an
// ordinary class to the reflection walk, so it used to be described as kind
// "object" — which every backend happily bound, and which then threw at CALL
// time ("Unable to convert ...") because no caster for it was ever registered.
// The workaround was a hand-written extension file per class flattening every
// Eigen type to std::vector<double>.
//
// Enabling an interop instead marks those types in the IR (GenType::interop),
// with kind left "unknown" — the is_pointer / is_sequence pattern. Two things
// follow, and they are the whole feature:
//
//   * a backend WITHOUT a caster for the library (node, wasm, lua, …) keeps
//     skipping the member, which is a fix: a skipped method is honest, a bound
//     one that always throws is not;
//   * a backend WITH one (python via <pybind11/eigen.h>,
//     nanobind via <nanobind/eigen/dense.h>) emits that include and
//     binds the exact C++ spelling unchanged — no adapter, no copy. On the
//     script side the type is a numpy array, in both directions, matrices
//     included.
//
// Recognition is by ENCLOSING NAMESPACE, not by a list of type names, so one
// declaration covers everything the library owns — VectorXd, MatrixXd,
// Vector3d, Map, Ref, Block, Array, the sparse types — including the spellings
// a signature reaches for that no hand-maintained list would have predicted.
//
// Adding another library is a tag plus a namespace here; nothing else in the
// pipeline is library-specific.

#pragma once

#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace rosetta {

    // One tag type per supported foreign library. The tag is what a manifest
    // name ("eigen") and a C++ namespace ("Eigen") are mapped through.
    struct eigen_interop {};

    template <typename Tag> struct interop_enabled : std::false_type {};

    namespace interop_detail {

        // Routes a tag through a dependent name. The specializations live in the
        // generated bindings.h, which is parsed AFTER this header, so a direct
        // `interop_enabled<eigen_interop>::value` in a function body would be a
        // non-dependent lookup bound at this header's parse point — i.e. always
        // false, with the opt-in silently doing nothing. Naming the tag as
        // `dep_tag<eigen_interop, Dep>::type` makes the lookup dependent, so it
        // resolves when the WALK instantiates the caller. Same reason
        // rosetta::is_sequence works: it is only ever queried as
        // is_sequence<U>, where U comes from the enclosing template.
        template <typename Tag, typename> struct dep_tag {
            using type = Tag;
        };

        // The interop owning namespace `ns`, or "" when none is enabled for it.
        // Matches the namespace itself and anything nested below it, so a type
        // surfacing from a detail namespace (Eigen::internal::…) is recognized
        // as belonging to the same library. `Dep` is the type being walked; it
        // is unused except to make the trait lookups above dependent.
        template <typename Dep> inline std::string owner_of(const std::string &ns) {
            const auto owns = [&](std::string_view lib) {
                return ns == lib ||
                       (ns.size() > lib.size() && ns.compare(0, lib.size(), lib) == 0 &&
                        ns.compare(lib.size(), 2, "::") == 0);
            };
            if constexpr (interop_enabled<typename dep_tag<eigen_interop, Dep>::type>::value) {
                if (owns("Eigen")) {
                    return "eigen";
                }
            }
            return {};
        }

    } // namespace interop_detail

} // namespace rosetta
