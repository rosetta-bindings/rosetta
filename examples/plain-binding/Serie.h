// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED
//
// A flat buffer of scalars grouped into items of `itemSize` consecutive values:
// itemSize 1 is a serie of scalars, 3 a serie of 3D vectors, 6 a serie of
// symmetric tensors. `count()` is the number of items, `size()` the raw number
// of scalars. Plus the functional trio — forEach / map / reduce.
//
// Self-contained, header-only, plain C++20: no rosetta include, no annotations,
// no registration. Nothing here knows it is about to be bound. Docs live out of
// line in Serie.ann.json, because the generated bindings #include this header
// and are compiled by a STOCK toolchain — an inline `[[ = rosetta::doc{…} ]]`
// would drag <experimental/meta> into every target.
//
// Declarations only; the bodies live in Serie.hxx, included at the bottom. That
// split is also a test of the binding: rosetta reads THIS file, so a member
// whose body it never sees must still bind — and does, because reflection reads
// declarations.
//
// Two deliberate differences from the shape this pattern usually takes in real
// code, both about crossing a language boundary:
//
//   * It OWNS its buffer. A serie that merely views someone else's vector is
//     cheaper in C++, but a view handed to Python outlives nothing in
//     particular — the lifetime rule stops being expressible at the boundary.
//   * `item()` returns a std::vector<double> COPY, not a std::span. No binding
//     framework has a span caster; the span is still there for C++ callers, on
//     the iterator. See README.md — rosetta currently reports a span-returning
//     member as bound, and it then fails at run time.

#pragma once

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace serie {

    class Serie {
    public:
        /// A window onto one item, for C++ callers. Never crosses a binding.
        using Item = std::span<const double>;

        Serie() = default;
        Serie(std::vector<double> values, std::size_t itemSize = 1);
        Serie(std::initializer_list<double> values, std::size_t itemSize = 1);

        /// Number of scalars per item (1 = scalars, 3 = vector3, 6 = tensor).
        std::size_t itemSize() const;
        /// Number of items.
        std::size_t count() const;
        /// Raw number of scalars: count() * itemSize().
        std::size_t size() const;
        bool        empty() const;

        /// The i-th item, as a copy of itemSize consecutive scalars.
        std::vector<double> item(std::size_t i) const;

        /// The i-th scalar of an itemSize == 1 serie (throws otherwise).
        double scalar(std::size_t i) const;

        /// The scalars of an itemSize == 1 serie (throws otherwise).
        const std::vector<double> &scalars() const;

        /// The flat buffer, whatever the itemSize.
        const std::vector<double> &raw() const;

        /// Appends the items of `other`; same itemSize required (an empty serie
        /// adopts the other's). Returns *this, so calls chain.
        Serie &append(const Serie &other);

        std::string describe() const;

        // ---- why there are no member templates here --------------------------
        //
        // The natural C++ spelling of this trio is a template — `map(F &&f)` —
        // and that CANNOT be bound: a template has nothing to reflect on until
        // something names an instance, so it never even becomes a candidate.
        // The three below take a std::function instead, which is a concrete type.
        //
        // Keeping BOTH — a template and a same-name non-template overload — does
        // not work either, and the failure is worth knowing: the template is
        // invisible to the IR but perfectly visible to C++ overload resolution,
        // so the emitted `&Serie::map` is ambiguous and the binding does not
        // compile. Either overload under a different name (`mapScalars`) or, as
        // here, let the std::function version be the only one.
        //
        // Nor should you overload on the callback's ARITY (one forEach per
        // itemSize). It compiles, and it does not work: a caster only asks
        // "is this callable", never "how many parameters", so the first overload
        // swallows every host function and the rest are unreachable. Take the
        // item as a std::vector<double> instead — one signature, any itemSize,
        // and both Python and JS destructure it. See README.md.
        //
        // Whether these bind at all comes down to whether the target can turn a
        // host callable into a std::function: python, node and wasm can, julia
        // cannot. See README.md, "Getting the templates back".

        /// forEach() over an itemSize == 1 serie, callable from a script.
        void forEach(const std::function<void(double)> &f) const;

        /// map() over an itemSize == 1 serie, callable from a script.
        Serie map(const std::function<double(double)> &f) const;

        /// reduce() over an itemSize == 1 serie, callable from a script.
        double reduce(const std::function<double(double, double)> &f, double init) const;

    private:
        void requireScalar(const char *method) const;

        std::vector<double> values_;
        std::size_t         itemSize_{1};
    };

    /**
     * Superposition: result = sum of alpha[j] * series[j].
     *
     * All series must share an itemSize and have either the same count or
     * count 1 — a count-1 serie is UNIFORM and broadcast against the others.
     * One alpha per serie.
     */
    Serie weightedSum(const std::vector<Serie> &series, const std::vector<double> &alpha);

} // namespace serie

#include "Serie.hxx"
