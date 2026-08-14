// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Google Test suite for the `dynamic` backend — the emitter that turns the IR
// into runtime metadata (see include/rosetta/backends/dynamic_backend.h).
//
// The companion suite tests/dynamic.cpp exercises the RUNTIME with hand-built
// tables; this one checks that the emitter produces tables of that same shape.
// Verifies the generated source (render), not a live build — mirroring
// member_object.cpp and sequence.cpp.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <gtest/gtest.h>
#include <rosetta/generate.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

namespace dyn_test {

    enum class Mode { Fast = 0, Precise = 1 };

    struct Point {
        double x = 0;
        double y = 0;
        double norm() const { return x * x + y * y; }
    };

    struct Shape {
        [[= rosetta::doc{"Display name"}]] std::string name;
        [[= rosetta::range{0, 100}]] int              quality = 50;
        [[= rosetta::readonly{}]] std::string           id      = "s0";
        Mode                                          mode    = Mode::Fast;
        Point                                         origin;
        std::vector<double>                           weights;

        Shape() = default;
        Shape(std::string n, int q) : name(std::move(n)), quality(q) {}

        // An overload set. Every name-keyed backend keeps only the first of
        // these; the dynamic model keeps both.
        double at(int i) const { return static_cast<double>(i); }
        double at(int i, int j) const { return static_cast<double>(i * j); }

        // A reference return — must be handed back without a copy, pinned.
        Point       &ref_origin() { return origin; }
        static Shape make(const std::string &n) { return Shape(n, 1); }

        // Not marshalable: a std::function parameter. Described in the metadata
        // with a reason, rather than silently absent.
        void on_change(const std::function<void(int)> &cb) { cb(quality); }
    };

} // namespace dyn_test

template <> struct rosetta::binding_info<dyn_test::Point> {
    static constexpr const char *header = "shapes.h";
};
template <> struct rosetta::binding_info<dyn_test::Shape> {
    static constexpr const char *header = "shapes.h";
};
template <> struct rosetta::binding_info<dyn_test::Mode> {
    static constexpr const char *header = "shapes.h";
};

static std::string dynamic_source() {
    const auto c =
        rosetta::gen_detail::make_context<dyn_test::Point, dyn_test::Shape, dyn_test::Mode>(
            "shapes");
    return rosetta::backend_registry().at("dynamic")->render(c);
}

// ---------------------------------------------------------------------------
// Shape of the emitted tables
// ---------------------------------------------------------------------------

TEST(DynamicBackend, EmitsStockCppWithNoReflection) {
    const std::string s = dynamic_source();

    std::cerr << s << std::endl;

    EXPECT_NE(s.find("#include <rosetta/dynamic.h>"), std::string::npos);
    // The whole point: nothing that needs a C++26 toolchain on the target.
    EXPECT_EQ(s.find("<experimental/meta>"), std::string::npos);
    EXPECT_EQ(s.find("^^"), std::string::npos);
    EXPECT_EQ(s.find("[:"), std::string::npos);
}

TEST(DynamicBackend, ForwardDeclaresEveryClassSoTypeDescsCanLinkAtStaticInit) {
    const std::string s = dynamic_source();
    EXPECT_NE(s.find("extern const MetaClass k_dyn_test__Shape;"), std::string::npos);
    EXPECT_NE(s.find("extern const MetaClass k_dyn_test__Point;"), std::string::npos);
    // …and the descriptor for a bound class points straight at it.
    EXPECT_NE(s.find(".cls = &k_dyn_test__Point"), std::string::npos);
}

TEST(DynamicBackend, EveryClassCarriesItsOwnTypeDesc) {
    const std::string s = dynamic_source();
    // MetaClass::self is what lets an Object become a typed Any and be passed
    // as an argument to another class's method.
    std::size_t n = 0;
    for (std::size_t p = s.find(".self = &td_"); p != std::string::npos;
         p = s.find(".self = &td_", p + 1)) {
        ++n;
    }
    EXPECT_EQ(n, 2u) << "one per bound class (Point, Shape)";
}

TEST(DynamicBackend, RegistrationIsIdempotentAndLinks) {
    const std::string s = dynamic_source();
    EXPECT_NE(s.find("void register_all(rosetta::dyn::Registry &r)"), std::string::npos);
    EXPECT_NE(s.find("r.add_class(&k_dyn_test__Shape);"), std::string::npos);
    EXPECT_NE(s.find("r.add_enum(&k_dyn_test__Mode);"), std::string::npos);
    EXPECT_NE(s.find("r.link();"), std::string::npos);
}

// ---------------------------------------------------------------------------
// The capability that motivates the backend
// ---------------------------------------------------------------------------

TEST(DynamicBackend, KeepsEveryOverloadInsteadOfTheFirstOnly) {
    const std::string s = dynamic_source();
    EXPECT_NE(s.find(".overload_index = 0, .overload_count = 2"), std::string::npos);
    EXPECT_NE(s.find(".overload_index = 1, .overload_count = 2"), std::string::npos);
    // Both thunks exist, each calling with its own arity — and neither needs a
    // disambiguating static_cast, because the call is by name with concrete
    // argument types.
    EXPECT_NE(s.find("->at(value_cast<int>(a[0]))"), std::string::npos);
    EXPECT_NE(s.find("->at(value_cast<int>(a[0]), value_cast<int>(a[1]))"), std::string::npos);
    EXPECT_EQ(s.find("static_cast<double (dyn_test::Shape::*)"), std::string::npos);
}

TEST(DynamicBackend, UiMetadataReachesRuntime) {
    const std::string s = dynamic_source();
    EXPECT_NE(s.find(".doc = \"Display name\""), std::string::npos);
    EXPECT_NE(s.find(".range = {.has = true, .lo = 0, .hi = 100}"), std::string::npos);
    EXPECT_NE(s.find(".readonly = true"), std::string::npos);
    // An enum carries its enumerators, so a combobox needs no generated code.
    EXPECT_NE(s.find("{\"Fast\", 0}"), std::string::npos);
    EXPECT_NE(s.find("{\"Precise\", 1}"), std::string::npos);
}

TEST(DynamicBackend, ReadonlyFieldGetsNoSetter) {
    const std::string s = dynamic_source();
    const auto        id = s.find(".name = \"id\"");
    ASSERT_NE(id, std::string::npos);
    const auto next = s.find("{.name = ", id);
    EXPECT_EQ(s.find(".set =", id) > next, true) << "the readonly field must emit no setter";
}

// ---------------------------------------------------------------------------
// Ownership
// ---------------------------------------------------------------------------

TEST(DynamicBackend, ReferenceReturnsAndObjectFieldsPinTheParent) {
    const std::string s = dynamic_source();
    // A T& return borrows and carries the receiver's owner, so the child cannot
    // outlive the parent — the pin that node's Wrap<T> and wasm's raw-pointer
    // returns cannot express.
    EXPECT_NE(s.find("borrow_any(&td_"), std::string::npos);
    EXPECT_NE(s.find(", self.owner)"), std::string::npos);
    // A by-value return owns its copy instead.
    EXPECT_NE(s.find("make_any(&td_"), std::string::npos);
}

TEST(DynamicBackend, EmitsConstructorsIncludingTheImplicitDefault) {
    const std::string s = dynamic_source();
    EXPECT_NE(s.find("return new dyn_test::Shape(value_cast<std::string>(a[0]), "
                     "value_cast<int>(a[1]));"),
              std::string::npos);
    EXPECT_NE(s.find("return new dyn_test::Shape();"), std::string::npos);
    EXPECT_NE(s.find(".destroy = +[](void *p) { delete static_cast<dyn_test::Shape *>(p); }"),
              std::string::npos);
}

// ---------------------------------------------------------------------------
// Skips are described, not deleted
// ---------------------------------------------------------------------------

TEST(DynamicBackend, UnmarshalableMethodIsDescribedWithAReasonNotDropped) {
    const std::string s = dynamic_source();
    // The method still appears — a UI can list it and say why it is disabled.
    const auto at = s.find(".name = \"on_change\"");
    ASSERT_NE(at, std::string::npos);
    EXPECT_NE(s.find(".skip_reason = \"callback:", at), std::string::npos);
    // …and no thunk was emitted for it.
    const auto next = s.find("{.name = ", at + 1);
    EXPECT_TRUE(s.find(".invoke =", at) == std::string::npos ||
                s.find(".invoke =", at) > next);
}

TEST(DynamicBackend, SkipsAreRecordedInTheCoverageReport) {
    rosetta::coverage::reset();
    dynamic_source();
    bool found = false;
    for (const auto &sk : rosetta::coverage::log().skips) {
        if (sk.target == "dynamic" && sk.member == "on_change") {
            found = true;
            EXPECT_EQ(sk.reason, "unmarshalable_type");
        }
    }
    EXPECT_TRUE(found) << "a dynamic-backend skip must land in coverage.json";
    rosetta::coverage::reset();
}

// ---------------------------------------------------------------------------
// TypeDesc pooling
// ---------------------------------------------------------------------------

TEST(DynamicBackend, IdenticalTypesShareOneTypeDesc) {
    const std::string s = dynamic_source();
    // Point::x and Point::y are both `double`: exactly one descriptor for it.
    std::size_t n = 0;
    for (std::size_t p = s.find(".spelling = \"double\""); p != std::string::npos;
         p = s.find(".spelling = \"double\"", p + 1)) {
        ++n;
    }
    EXPECT_EQ(n, 1u) << "the TypeDesc pool must de-duplicate";
}
