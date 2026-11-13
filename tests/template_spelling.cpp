// Copyright (c) fmaerten@gmail.com
// License: MIT

// Google Test suite for the QUALIFIED C++ SPELLING of class template
// specializations.
//
// std::meta::display_string_of() is not a C++ spelling. For a template-id it
// renders every ARGUMENT stripped of its namespaces, so binding
// `Horizon<std::vector<lookup::math::Point2D>>` used to emit
//
//     lookup::interpolation::implicit::Horizon<vector<Point2D, allocator<Point2D>>>
//      └── outer namespace restored by the scope walk    └── arguments left bare
//
// which does not compile. The expanded backends paper over bare identifiers with
// `using namespace` directives, but those only cover the namespaces of BOUND
// types — so binding more classes could fix the user half and never the `std::`
// half. qualified_type_spelling() composes the template-id structurally instead
// (template_of / template_arguments_of, recursively), the way sequence_spelling()
// already does.
//
// The tests below pin the spelling itself; that the spelling COMPILES is pinned
// harder by the static_asserts, which use each emitted form as a real type.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <cstddef>
#include <gtest/gtest.h>
#include <rosetta/generate.h>
#include <string>
#include <type_traits>
#include <vector>

// The shape that motivated this: a class template in a nested namespace,
// specialized on a std:: container of a user type from a DIFFERENT namespace.
namespace tsp::math {
    struct Point2D {
        double x = 0, y = 0;
    };
} // namespace tsp::math

namespace tsp::interp::implicit {

    template <typename T> struct Horizon {
        T                 data;
        tsp::math::Point2D centroid() const { return {}; }
    };

    // A NON-type template parameter: its argument is a value, not a type.
    template <int N> struct FieldND {
        double v[N]{};
    };

    // Nested inside a class, to check the scope walk keeps the owner.
    struct Outer {
        template <typename T> struct Inner {
            T t;
        };
    };

} // namespace tsp::interp::implicit

using HorizonV = tsp::interp::implicit::Horizon<std::vector<tsp::math::Point2D>>;
using Field2   = tsp::interp::implicit::FieldND<2>;
using InnerPt  = tsp::interp::implicit::Outer::Inner<tsp::math::Point2D>;
using HorizonN = tsp::interp::implicit::Horizon<tsp::interp::implicit::FieldND<3>>;

template <> struct rosetta::binding_info<HorizonV> {
    static constexpr const char *header = "tsp.h";
    static constexpr const char *expose = "HorizonV";
};
template <> struct rosetta::binding_info<Field2> {
    static constexpr const char *header = "tsp.h";
    static constexpr const char *expose = "Field2";
};
template <> struct rosetta::binding_info<InnerPt> {
    static constexpr const char *header = "tsp.h";
    static constexpr const char *expose = "InnerPt";
};
template <> struct rosetta::binding_info<HorizonN> {
    static constexpr const char *header = "tsp.h";
    static constexpr const char *expose = "HorizonN";
};

// ---- the spelling is COMPILABLE ---------------------------------------------
//
// Each alias below is the exact text the tests assert on, used as a real type.
// If qualified_type_spelling() ever regresses to something that only looks
// right, these stop compiling — a sharper failure than a string comparison.

using SpelledHorizonV = tsp::interp::implicit::Horizon<
    std::vector<tsp::math::Point2D, std::allocator<tsp::math::Point2D>>>;
static_assert(std::is_same_v<SpelledHorizonV, HorizonV>);

using SpelledField2 = tsp::interp::implicit::FieldND<2>;
static_assert(std::is_same_v<SpelledField2, Field2>);

using SpelledInnerPt = tsp::interp::implicit::Outer::Inner<tsp::math::Point2D>;
static_assert(std::is_same_v<SpelledInnerPt, InnerPt>);

using SpelledHorizonN = tsp::interp::implicit::Horizon<tsp::interp::implicit::FieldND<3>>;
static_assert(std::is_same_v<SpelledHorizonN, HorizonN>);

namespace {

    std::string qualified_of_expose(const rosetta::GenContext &c, const char *expose) {
        for (const auto &k : c.classes) {
            if (k.expose == expose) {
                return k.qualified;
            }
        }
        return "<not bound>";
    }

    rosetta::GenContext context() {
        return rosetta::gen_detail::make_context<HorizonV, Field2, InnerPt, HorizonN>("tsp");
    }

} // namespace

// ---- template arguments keep their namespaces --------------------------------

TEST(TemplateSpelling, StdContainerArgumentIsFullyQualified) {
    const std::string q = qualified_of_expose(context(), "HorizonV");
    EXPECT_EQ(q, "tsp::interp::implicit::Horizon<std::vector<tsp::math::Point2D, "
                 "std::allocator<tsp::math::Point2D>>>");
}

TEST(TemplateSpelling, NoBareArgumentIdentifierSurvives) {
    const std::string q = qualified_of_expose(context(), "HorizonV");
    // The exact failure that shipped: an unqualified `vector<` / `Point2D`
    // inside the template-id, which only compiles if a `using namespace`
    // happens to cover it — and nothing ever covers std::.
    EXPECT_EQ(q.find("<vector<"), std::string::npos);
    EXPECT_EQ(q.find("<Point2D"), std::string::npos);
    EXPECT_EQ(q.find(" Point2D"), std::string::npos);
}

TEST(TemplateSpelling, LibcxxInlineNamespaceIsNotEmitted) {
    // libc++ declares everything in the inline namespace std::__1. Walking the
    // scope verbatim would emit `std::__1::vector`, which is correct here and
    // breaks the moment the generated source is built against libstdc++ or
    // MSVC — the entire point of the expanded backends.
    const std::string q = qualified_of_expose(context(), "HorizonV");
    EXPECT_EQ(q.find("__"), std::string::npos) << "an implementation-reserved namespace leaked: " << q;
}

// ---- other template shapes ----------------------------------------------------

TEST(TemplateSpelling, NonTypeArgumentIsRenderedAsItsValue) {
    EXPECT_EQ(qualified_of_expose(context(), "Field2"), "tsp::interp::implicit::FieldND<2>");
}

TEST(TemplateSpelling, TemplateNestedInAClassKeepsItsOwner) {
    EXPECT_EQ(qualified_of_expose(context(), "InnerPt"),
              "tsp::interp::implicit::Outer::Inner<tsp::math::Point2D>");
}

TEST(TemplateSpelling, NestedTemplateArgumentIsQualifiedRecursively) {
    EXPECT_EQ(qualified_of_expose(context(), "HorizonN"),
              "tsp::interp::implicit::Horizon<tsp::interp::implicit::FieldND<3>>");
}

// ---- and the backends actually emit it ----------------------------------------

TEST(TemplateSpelling, ExpandedBackendsEmitTheQualifiedTemplateId) {
    const auto        c = context();
    const std::string expected =
        "tsp::interp::implicit::Horizon<std::vector<tsp::math::Point2D, "
        "std::allocator<tsp::math::Point2D>>>";
    for (const char *lang : {"python", "nanobind", "wasm",
                             "lua", "julia"}) {
        const std::string s = rosetta::backend_registry().at(lang)->render(c);
        EXPECT_NE(s.find(expected), std::string::npos) << lang << " lost the qualification";
        EXPECT_EQ(s.find("Horizon<vector<"), std::string::npos)
            << lang << " emitted an unqualified template argument";
    }
}

// ---- a plain (non-template) class is unchanged --------------------------------

namespace tsp::plain {
    struct Simple {
        int v = 0;
    };
} // namespace tsp::plain
template <> struct rosetta::binding_info<tsp::plain::Simple> {
    static constexpr const char *header = "tsp.h";
};

TEST(TemplateSpelling, PlainClassSpellingIsUnchanged) {
    const auto c = rosetta::gen_detail::make_context<tsp::plain::Simple>("tsp");
    EXPECT_EQ(c.classes.front().qualified, "tsp::plain::Simple");
}

// ---- portable integer spelling ------------------------------------------------
//
// std::size_t / std::ptrdiff_t are reflected as the LP64 types they alias where
// the bindings are generated (macOS, Linux): `unsigned long` / `long`. Emitted
// that way, the overload-selecting casts do not compile on Windows (LLP64), so
// they are spelled back as the aliases.

TEST(TemplateSpelling, PortableIntegerSpelling) {
    using rosetta::gen_detail::portable_integers;
    if constexpr (sizeof(long) != sizeof(std::size_t)) {
        GTEST_SKIP() << "generated on an LLP64 host: long is left alone";
    }
    EXPECT_EQ(portable_integers("double (unsigned long) const"), "double (std::size_t) const");
    EXPECT_EQ(portable_integers("long"), "std::ptrdiff_t");
    EXPECT_EQ(portable_integers("unsigned long int"), "std::size_t");
    EXPECT_EQ(portable_integers("const unsigned long &"), "const std::size_t &");
    EXPECT_EQ(portable_integers("std::vector<unsigned long>"), "std::vector<std::size_t>");
    // the same on every platform: untouched
    EXPECT_EQ(portable_integers("unsigned long long"), "unsigned long long");
    EXPECT_EQ(portable_integers("long long"), "long long");
    EXPECT_EQ(portable_integers("long double"), "long double");
    // a word that merely contains "long" is an identifier, not the type
    EXPECT_EQ(portable_integers("longitude (along)"), "longitude (along)");
}

namespace tsp::ints {
    struct Grid {
        double at(std::size_t i) const { return static_cast<double>(i); }
        double at(std::size_t i, std::size_t j) const { return static_cast<double>(i + j); }
        std::ptrdiff_t shift(std::ptrdiff_t d) const { return d; }
        unsigned long long big(unsigned long long v) const { return v; }
    };
} // namespace tsp::ints
template <> struct rosetta::binding_info<tsp::ints::Grid> {
    static constexpr const char *header = "tsp.h";
};

TEST(TemplateSpelling, OverloadCastsSpellSizeTPortably) {
    if constexpr (sizeof(long) != sizeof(std::size_t)) {
        GTEST_SKIP() << "generated on an LLP64 host";
    }
    const auto c = rosetta::gen_detail::make_context<tsp::ints::Grid>("tsp");
    for (const char *lang : {"nanobind", "wasm"}) {
        const std::string s = rosetta::backend_registry().at(lang)->render(c);
        EXPECT_NE(s.find("(tsp::ints::Grid::*)(std::size_t) const"), std::string::npos) << lang;
        EXPECT_EQ(s.find("unsigned long)"), std::string::npos) << lang;
        EXPECT_EQ(s.find("unsigned long,"), std::string::npos) << lang;
    }
    // embind keys a method by name and keeps the first overload only; nanobind
    // binds the whole set, so the two-argument cast is there
    const std::string nb = rosetta::backend_registry().at("nanobind")->render(c);
    EXPECT_NE(nb.find("(tsp::ints::Grid::*)(std::size_t, std::size_t) const"), std::string::npos);
    // embind casts every method, overloaded or not: ptrdiff_t and the 64-bit
    // type that is the same everywhere
    const std::string js = rosetta::backend_registry().at("wasm")->render(c);
    EXPECT_NE(js.find("std::ptrdiff_t (tsp::ints::Grid::*)(std::ptrdiff_t) const"), std::string::npos);
    EXPECT_NE(js.find("(tsp::ints::Grid::*)(unsigned long long) const"), std::string::npos);
}
