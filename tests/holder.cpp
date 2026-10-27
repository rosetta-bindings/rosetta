// Copyright (c) fmaerten@gmail.com
// License: MIT

// Google Test suite for HOLDER POLICIES — intrusively reference-counted
// classes (manifest "holders", rosetta::holder_policy, see rosetta/holder.h).
//
// The shape is VTK's: a root class with a PROTECTED destructor, objects made by
// a static factory carrying one reference, shared with Ref() / Unref(), and a
// library smart pointer that takes a reference when built from a raw pointer.
// Such classes used to be skipped by every backend (non-destructible); with a
// policy the python, wasm and julia backends bind them through that holder.
//
// The fixture below is a miniature of that world, with every case VTK raises:
//   - an abstract class in the middle (no factory, never constructible),
//   - a concrete class WITHOUT a factory of its own (it inherits its base's,
//     which returns the base type — not creatable),
//   - a borrowed pointer return (`first()` — the GetOutput() case),
//   - factories by name (`New`, `NewCopy`) — owned returns, skipped,
//   - a held-pointer parameter (`absorb(Shape *)` — SetInputData),
//   - the same static in two classes (`SafeDownCast` — every VTK class).
//
// Verifies the reflected IR and the generated sources (render), not a live
// build — mirroring shared_ptr.cpp. The live build is exercised against VTK.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <gtest/gtest.h>
#include <rosetta/generate.h>
#include <string>
#include <type_traits>

namespace rcns {
    class RefBase {
    public:
        void Ref() { ++count_; }
        void Unref() {
            if (--count_ == 0) {
                delete this;
            }
        }
        int count() const { return count_; }

    protected:
        RefBase()          = default;
        virtual ~RefBase() = default;

    private:
        int count_ = 1;
    };

    // The library's intrusive smart pointer.
    template <class T> class Ptr {
    public:
        Ptr() = default;
        Ptr(T *p) : p_(p) {
            if (p_) {
                p_->Ref();
            }
        }
        Ptr(const Ptr &o) : Ptr(o.p_) {}
        ~Ptr() {
            if (p_) {
                p_->Unref();
            }
        }
        Ptr &operator=(Ptr o) {
            std::swap(p_, o.p_);
            return *this;
        }
        T *Get() const { return p_; }
        T &operator*() const { return *p_; }

    private:
        T *p_ = nullptr;
    };

    class Shape : public RefBase {
    public:
        virtual double area() const = 0;
        static Shape  *SafeDownCast(RefBase *o) { return dynamic_cast<Shape *>(o); }

    protected:
        ~Shape() override = default;
    };

    class Circle : public Shape {
    public:
        static Circle *New() { return new Circle; }
        static Circle *SafeDownCast(RefBase *o) { return dynamic_cast<Circle *>(o); }
        double         area() const override { return 3.0 * radius * radius; }
        Circle        *NewCopy() const { return New(); }  // factory — owned return
        Shape         *first() { return this; }           // borrowed return
        void           absorb(Shape *other) { (void)other; } // held-pointer parameter
        double         radius = 1.0;

    protected:
        Circle()           = default;
        ~Circle() override = default;
    };

    // Concrete, but declares no New() of its own: `UnitCircle::New()` is
    // Circle's and returns Circle* — so this one is NOT creatable.
    class UnitCircle : public Circle {
    public:
        double unit() const { return 1.0; }

    protected:
        ~UnitCircle() override = default;
    };
} // namespace rcns

// What rosetta_gen emits for:
//   "holders": [{"base": "rcns::RefBase", "holder": "rcns::Ptr", "get": "Get",
//                "retain": "Ref()", "release": "Unref()", "factory": "New",
//                "owned_returns": ["New*"]}]
template <class T>
struct rosetta::holder_policy<T, std::enable_if_t<std::is_base_of_v<rcns::RefBase, T>>> {
    static constexpr bool        enabled   = true;
    static constexpr const char *holder    = "rcns::Ptr";
    static constexpr const char *header    = "ptr.h";
    static constexpr const char *get       = "Get";
    static constexpr const char *retain    = "Ref()";
    static constexpr const char *release   = "Unref()";
    static constexpr const char *factory   = "New";
    static constexpr const char *owned     = "New*";
    static constexpr bool        creatable = [] {
        if constexpr (requires { { T::New() } -> std::same_as<T *>; }) {
            return !std::is_abstract_v<T>;
        } else {
            return false;
        }
    }();
};

template <> struct rosetta::binding_info<rcns::RefBase> {
    static constexpr const char *header = "shapes.h";
};
template <> struct rosetta::binding_info<rcns::Shape> {
    static constexpr const char *header = "shapes.h";
};
template <> struct rosetta::binding_info<rcns::Circle> {
    static constexpr const char *header = "shapes.h";
};
template <> struct rosetta::binding_info<rcns::UnitCircle> {
    static constexpr const char *header = "shapes.h";
};

namespace {

    rosetta::GenContext ctx() {
        return rosetta::gen_detail::make_context<rcns::RefBase, rcns::Shape, rcns::Circle,
                                                 rcns::UnitCircle>("shapes");
    }

    std::string render(const char *lang) {
        return rosetta::backend_registry().at(lang)->render(ctx());
    }

    bool has(const std::string &hay, const std::string &needle) {
        return hay.find(needle) != std::string::npos;
    }

    const rosetta::GenClass &cls(const rosetta::GenContext &c, const std::string &name) {
        for (const auto &k : c.classes) {
            if (k.name == name) {
                return k;
            }
        }
        throw std::runtime_error("no class " + name);
    }

} // namespace

// ---- the IR ---------------------------------------------------------------------

TEST(Holder, ThePolicyReachesEveryDerivedClass) {
    const auto c = ctx();
    for (const char *n : {"RefBase", "Shape", "Circle", "UnitCircle"}) {
        const auto &k = cls(c, n);
        EXPECT_FALSE(k.is_destructible) << n;
        EXPECT_EQ(k.holder.holder, "rcns::Ptr") << n;
        EXPECT_EQ(k.holder.get, "Get") << n;
        EXPECT_EQ(k.holder.release, "Unref()") << n;
    }
}

// The compiler, not the manifest, decides creatability: an abstract class has
// none, and a class whose New() is inherited gets its BASE back from it.
TEST(Holder, OnlyAClassWithItsOwnFactoryIsCreatable) {
    const auto c = ctx();
    EXPECT_TRUE(cls(c, "Circle").holder.creatable);
    EXPECT_FALSE(cls(c, "Shape").holder.creatable);      // abstract
    EXPECT_FALSE(cls(c, "RefBase").holder.creatable);    // no New() at all
    EXPECT_FALSE(cls(c, "UnitCircle").holder.creatable); // New() returns Circle*
}

TEST(Holder, TheFactoryNameIsAlwaysAnOwnedReturn) {
    const auto  c = ctx();
    const auto &o = cls(c, "Circle").holder.owned_returns;
    EXPECT_NE(std::find(o.begin(), o.end(), "New*"), o.end());
    EXPECT_NE(std::find(o.begin(), o.end(), "New"), o.end());
}

TEST(Holder, OwnedReturnsMatchByGlob) {
    using rosetta::gen_detail::name_glob_match;
    EXPECT_TRUE(name_glob_match("New*", "New"));
    EXPECT_TRUE(name_glob_match("New*", "NewInstance"));
    EXPECT_FALSE(name_glob_match("New*", "GetNew"));
    EXPECT_TRUE(name_glob_match("Get?utput", "GetOutput"));
    EXPECT_FALSE(name_glob_match("Get?utput", "GetOutputPort"));
}

// ---- python (pybind11) -------------------------------------------------------------

TEST(Holder, PythonDeclaresTheIntrusiveHolderOnce) {
    const std::string s = render("python");
    EXPECT_TRUE(has(s, "PYBIND11_DECLARE_HOLDER_TYPE(T, rcns::Ptr<T>, true)")) << s;
    EXPECT_EQ(s.find("PYBIND11_DECLARE_HOLDER_TYPE"), s.rfind("PYBIND11_DECLARE_HOLDER_TYPE"));
    // Ptr spells its accessor Get(), not get().
    EXPECT_TRUE(has(s, "struct holder_helper<rcns::Ptr<T>>"));
    EXPECT_TRUE(has(s, "return p.Get();"));
    EXPECT_TRUE(has(s, "#include \"ptr.h\""));
}

TEST(Holder, PythonBindsEveryHeldClassWithItsHolder) {
    const std::string s = render("python");
    EXPECT_TRUE(has(s, "py::class_<rcns::RefBase, rcns::Ptr<rcns::RefBase>>"));
    EXPECT_TRUE(has(s, "py::class_<rcns::Circle, rcns::Ptr<rcns::Circle>, rcns::Shape>")) << s;
    // No trampoline for a class the library creates.
    EXPECT_FALSE(has(s, "Py_Circle"));
    EXPECT_FALSE(has(s, "Py_Shape"));
}

TEST(Holder, PythonConstructsThroughTheFactoryAndAdoptsItsReference) {
    const std::string s = render("python");
    EXPECT_TRUE(has(s, "c.def(py::init([]() {\n"
                       "            rcns::Circle *raw = rcns::Circle::New();\n"
                       "            rcns::Ptr<rcns::Circle> held(raw);\n"
                       "            raw->Unref();\n"
                       "            return held;"))
        << s;
    // Not creatable ⇒ no constructor at all (and never py::init<>()).
    EXPECT_FALSE(has(s, "rcns::UnitCircle::New()"));
    EXPECT_FALSE(has(s, "py::init<>()"));
}

TEST(Holder, PythonReturnsAHeldPointerWithTheDefaultPolicy) {
    const std::string s = render("python");
    const auto        at = s.find("c.def(\"first\"");
    ASSERT_NE(at, std::string::npos) << s;
    const std::string line = s.substr(at, s.find('\n', at) - at);
    EXPECT_FALSE(has(line, "reference_internal")) << line;
    // …and its held-pointer parameter needs no keep_alive: the callee refs it.
    const auto        ab    = s.find("c.def(\"absorb\"");
    ASSERT_NE(ab, std::string::npos);
    EXPECT_FALSE(has(s.substr(ab, s.find('\n', ab) - ab), "keep_alive"));
}

TEST(Holder, PythonSkipsTheFactoriesThemselves) {
    const std::string s = render("python");
    EXPECT_FALSE(has(s, "\"New\""));
    EXPECT_FALSE(has(s, "\"NewCopy\""));
}

// ---- wasm (embind) ------------------------------------------------------------------

TEST(Holder, WasmEmitsTheIntrusiveSmartPtrTraitAndRawDestructors) {
    const std::string s = render("wasm");
    EXPECT_TRUE(has(s, "struct smart_ptr_trait<rcns::Ptr<T>>")) << s;
    EXPECT_TRUE(has(s, "return sharing_policy::INTRUSIVE;"));
    EXPECT_TRUE(has(s, "static T *get(const pointer_type &p) { return p.Get(); }"));
    EXPECT_TRUE(has(s, "template <> inline void raw_destructor<rcns::Circle>(rcns::Circle *p) { p->Unref(); }"));
    EXPECT_TRUE(has(s, "template <> inline void raw_destructor<rcns::Shape>(rcns::Shape *p) { p->Unref(); }"));
}

TEST(Holder, WasmRegistersTheHolderAndTheFactoryConstructor) {
    const std::string s = render("wasm");
    EXPECT_TRUE(has(s, ".smart_ptr_constructor(\"Circle_ptr\", +[]() -> rcns::Ptr<rcns::Circle> {"))
        << s;
    EXPECT_TRUE(has(s, ".smart_ptr<rcns::Ptr<rcns::Shape>>(\"Shape_ptr\")"));
    EXPECT_TRUE(has(s, ".smart_ptr<rcns::Ptr<rcns::UnitCircle>>(\"UnitCircle_ptr\")"));
    EXPECT_FALSE(has(s, ".constructor<>()"));
}

TEST(Holder, WasmHandsAHeldPointerOutInsideItsHolder) {
    const std::string s = render("wasm");
    EXPECT_TRUE(has(s, ".function(\"first\", +[](rcns::Circle &self) { return "
                       "rcns::Ptr<rcns::Shape>(self.first()); }, emscripten::allow_raw_pointers())"))
        << s;
    EXPECT_TRUE(has(s, ".class_function(\"SafeDownCast\", +[]("));
    EXPECT_FALSE(has(s, "\"NewCopy\""));
}

// ---- julia (jlcxx) ------------------------------------------------------------------

TEST(Holder, JuliaFinalizesEveryHeldBoxByReleasingIt) {
    const std::string s = render("julia");
    EXPECT_TRUE(has(s, "mod.set_override_module(jlcxx::get_cxxwrap_module());")) << s;
    EXPECT_TRUE(has(s, "mod.method(\"__delete\", [](rcns::Circle *p) { p->Unref(); });"));
    EXPECT_TRUE(has(s, "mod.method(\"__delete\", [](rcns::RefBase *p) { p->Unref(); });"));
}

TEST(Holder, JuliaConstructsThroughTheFactory) {
    const std::string s = render("julia");
    EXPECT_TRUE(has(s, "c.constructor([]() { return rcns::Circle::New(); });")) << s;
    EXPECT_FALSE(has(s, "rcns::UnitCircle::New()"));
}

// Boxing tries the deepest class first: a UnitCircle must not come back as a
// Circle just because Circle was listed earlier.
TEST(Holder, JuliaBoxesAsTheMostDerivedBoundClass) {
    const std::string s     = render("julia");
    const auto        unit  = s.find("dynamic_cast<rcns::UnitCircle *>(r)");
    const auto        circ  = s.find("dynamic_cast<rcns::Circle *>(r)");
    const auto        shape = s.find("dynamic_cast<rcns::Shape *>(r)");
    ASSERT_NE(unit, std::string::npos) << s;
    EXPECT_LT(unit, circ);
    EXPECT_LT(circ, shape);
    EXPECT_TRUE(has(s, "r->Ref();"));
    EXPECT_TRUE(has(s, "c.method(\"first\", [](rcns::Circle &self) -> jl_value_t * { return "
                       "rosetta_jl_box_0(self.first()); });"));
}

TEST(Holder, JuliaAddsAByReferenceOverloadForHeldPointerParameters) {
    const std::string s = render("julia");
    EXPECT_TRUE(has(s, "c.method(\"absorb\", [](rcns::Circle &self, rcns::Shape &arg0) { "
                       "return self.absorb(&arg0); });"))
        << s;
}

// SafeDownCast(RefBase *) is a static of both Shape and Circle: as module-level
// Julia functions with one signature, the second would replace the first.
TEST(Holder, JuliaKeepsTheFirstOfTwoIdenticalStatics) {
    const std::string s = render("julia");
    EXPECT_TRUE(has(s, "rcns::Shape::SafeDownCast(")) << s;
    EXPECT_FALSE(has(s, "rcns::Circle::SafeDownCast(")) << s;
}

// ---- everything else keeps its old behaviour --------------------------------------

TEST(Holder, BackendsWithoutPolicySupportStillSkipHeldClasses) {
    const std::string s = render("node");
    EXPECT_FALSE(has(s, "rcns::Circle::New()"));
}
