// Copyright (c) fmaerten@gmail.com
// License: MIT

// Google Test suite for C++ shapes that a large real library (VTK) brought out,
// each of which used to break a generated binding:
//
//   - an overload set rosetta did not see as one: a `using Base::f;` beside a
//     local f() (vtkDataSetAttributes::RemoveArray), or a PROTECTED sibling of
//     a public method (vtkDataSetAlgorithm::GetInput). Either way `&T::f` is
//     ambiguous and the emitted line did not compile;
//   - one name carrying a static AND an instance overload
//     (vtkAbstractArray::GetDataTypeSize), which pybind11 refuses at IMPORT
//     time — failing the whole module;
//   - a signature naming an enum the module does not bind
//     (vtkDataArray::MemorySpace), which jlcxx rejects when the method is
//     REGISTERED — again failing the whole module load.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <gtest/gtest.h>
#include <rosetta/generate.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace lpns {
    struct Fields {
        void RemoveArray(const char *name) { (void)name; }
    };
    // `using` brings the base's RemoveArray(const char *) into the overload set
    // of `&Attributes::RemoveArray`, next to the local one.
    struct Attributes : Fields {
        using Fields::RemoveArray;
        void RemoveArray(int index) { (void)index; }
    };

    struct Algorithm {
        int GetInput() { return 0; }

    protected:
        int GetInput(int port) { return port; } // still part of the set
    };

    struct Array {
        virtual ~Array() = default;
        virtual int GetDataTypeSize() const { return 8; }
        static int  GetDataTypeSize(int type) { return type; }
    };

    // C strings, VTK-style: vtkSetStringMacro / vtkGetStringMacro, lookup by name.
    struct Named {
        const char *GetName() const { return name.c_str(); }
        void        SetName(const char *n) { name = n ? n : ""; }
        int         Find(const char *key, int fallback) const { return key ? 1 : fallback; }
        const char *label = "x"; // a FIELD of C-string type stays out
        std::string name;
    };

    // An `int` parameter defaulted to an enumerator of an enum the module
    // never binds (vtkDataArray::GetRange(int comp = vtkArrayComponents::...)).
    enum Components { AllComponents = -1 };
    struct Ranged {
        double range(int component = AllComponents) const { return component; }
    };

    // vtkFloatArray's shape: the bound base is reached only through an
    // unbound template (vtkAOSDataArrayTemplate<float>).
    struct ArrayBase {
        virtual ~ArrayBase() = default;
        int size() const { return 0; }
    };
    template <class T> struct ArrayTemplate : ArrayBase {
        T at(int) const { return T{}; }
    };
    struct FloatArray : ArrayTemplate<float> {};

    struct Space {
        enum MemorySpace { Host, Device }; // never bound
        int          where(MemorySpace s) const { return s; }
        int          plain() const { return 1; }
    };
} // namespace lpns

template <> struct rosetta::binding_info<lpns::Attributes> {
    static constexpr const char *header = "lp.h";
};
template <> struct rosetta::binding_info<lpns::Algorithm> {
    static constexpr const char *header = "lp.h";
};
template <> struct rosetta::binding_info<lpns::Array> {
    static constexpr const char *header = "lp.h";
};
template <> struct rosetta::binding_info<lpns::Named> {
    static constexpr const char *header = "lp.h";
};
template <> struct rosetta::binding_info<lpns::Ranged> {
    static constexpr const char *header = "lp.h";
};
template <> struct rosetta::binding_info<lpns::ArrayBase> {
    static constexpr const char *header = "lp.h";
};
template <> struct rosetta::binding_info<lpns::FloatArray> {
    static constexpr const char *header = "lp.h";
};
template <> struct rosetta::binding_info<lpns::Space> {
    static constexpr const char *header = "lp.h";
};
template <> struct rosetta::binding_info<lpns::Space::MemorySpace> {
    static constexpr const char *header = "lp.h";
};

namespace {
    const rosetta::GenMethod &method(const rosetta::GenClass &k, const std::string &name) {
        for (const auto &m : k.methods) {
            if (m.name == name) {
                return m;
            }
        }
        throw std::runtime_error("no method " + name);
    }

    bool has(const std::string &hay, const std::string &needle) {
        return hay.find(needle) != std::string::npos;
    }
} // namespace

TEST(LibraryPatterns, AUsingDeclarationMakesTheNameAnOverloadSet) {
    const auto k = rosetta::gen_detail::describe<lpns::Attributes>();
    EXPECT_TRUE(method(k, "RemoveArray").is_overloaded);
    // …so the emitted member pointer carries the disambiguating cast.
    const auto        c = rosetta::gen_detail::make_context<lpns::Attributes>("lp");
    const std::string s = rosetta::backend_registry().at("python")->render(c);
    EXPECT_TRUE(has(s, "static_cast<void (lpns::Attributes::*)(int)>(&lpns::Attributes::RemoveArray)"))
        << s;
}

TEST(LibraryPatterns, AProtectedSiblingMakesTheNameAnOverloadSet) {
    const auto k = rosetta::gen_detail::describe<lpns::Algorithm>();
    EXPECT_TRUE(method(k, "GetInput").is_overloaded);
}

TEST(LibraryPatterns, PythonBindsOnlyOneKindUnderAName) {
    const auto        c = rosetta::gen_detail::make_context<lpns::Array>("lp");
    const std::string s = rosetta::backend_registry().at("python")->render(c);
    const bool inst = has(s, "c.def(\"GetDataTypeSize\"");
    const bool stat = has(s, "c.def_static(\"GetDataTypeSize\"");
    EXPECT_TRUE(inst != stat) << "pybind11 refuses both under one name:\n" << s;
}

TEST(LibraryPatterns, JuliaSkipsASignatureNamingAnUnboundEnum) {
    const auto        c = rosetta::gen_detail::make_context<lpns::Space>("lp");
    const std::string s = rosetta::backend_registry().at("julia")->render(c);
    EXPECT_FALSE(has(s, "\"where\"")) << s;
    EXPECT_TRUE(has(s, "\"plain\""));
}

TEST(LibraryPatterns, JuliaKeepsASignatureNamingABoundEnum) {
    const auto c = rosetta::gen_detail::make_context<lpns::Space, lpns::Space::MemorySpace>("lp");
    const std::string s = rosetta::backend_registry().at("julia")->render(c);
    EXPECT_TRUE(has(s, "\"where\"")) << s;
}

// ---- C strings ----------------------------------------------------------------

TEST(LibraryPatterns, ACStringIsFlaggedInSignaturesOnly) {
    const auto k = rosetta::gen_detail::describe<lpns::Named>();
    EXPECT_TRUE(method(k, "GetName").ret.is_cstring);
    EXPECT_TRUE(method(k, "SetName").params[0].type.is_cstring);
    EXPECT_EQ(method(k, "SetName").params[0].type.kind, "unknown"); // non-adapting backends skip
    for (const auto &f : k.fields) {
        EXPECT_FALSE(f.type.is_cstring) << f.name;
    }
}

TEST(LibraryPatterns, CStringsCrossAsStringsInPythonWasmJulia) {
    const auto c = rosetta::gen_detail::make_context<lpns::Named>("lp");
    for (const char *lang : {"python", "wasm", "julia"}) {
        const std::string s = rosetta::backend_registry().at(lang)->render(c);
        EXPECT_TRUE(has(s, "\"GetName\"")) << lang;
        EXPECT_TRUE(has(s, "\"SetName\"")) << lang;
        EXPECT_TRUE(has(s, "\"Find\"")) << lang;
        EXPECT_TRUE(has(s, "std::string arg0")) << lang << ":\n" << s;
        EXPECT_TRUE(has(s, "char *str0 = arg0.data();")) << lang;
        EXPECT_TRUE(has(s, "return s ? std::string(s) : std::string();")) << lang;
        EXPECT_FALSE(has(s, "\"label\"")) << lang;
    }
}

TEST(LibraryPatterns, TypeScriptDeclaresACStringAsAString) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "rosetta_lp_typescript";
    fs::remove_all(dir);
    auto c    = rosetta::gen_detail::make_context<lpns::Named>("lp");
    c.out_dir = dir;
    rosetta::backend_registry().at("typescript")->emit(c);
    std::string s;
    for (const auto &e : fs::recursive_directory_iterator(dir)) {
        if (e.path().extension() == ".ts") {
            std::ifstream in(e.path());
            s.assign(std::istreambuf_iterator<char>(in), {});
        }
    }
    fs::remove_all(dir);
    EXPECT_TRUE(has(s, "GetName(): string")) << s;
    EXPECT_TRUE(has(s, "SetName(n: string): void")) << s;
}

// pybind converts an `arg` default to a Python object at IMPORT, through the
// default's own C++ type: an enumerator of an unbound enum failed the import.
TEST(LibraryPatterns, ANumericDefaultSpelledAsAnEnumeratorIsCastToTheParameterType) {
    rosetta::GenContext c = rosetta::gen_detail::make_context<lpns::Ranged>("lp");
    // What the doc-comment harvest fills in from the header.
    for (auto &m : c.classes[0].methods) {
        if (m.name == "range") {
            m.params[0].default_text = "lpns::AllComponents";
        }
    }
    const std::string s = rosetta::backend_registry().at("python")->render(c);
    EXPECT_TRUE(has(s, "py::arg(\"component\") = static_cast<int>(lpns::AllComponents)")) << s;
}

// The nearest BOUND ancestor stands in for an unbound direct base, so the class
// is still accepted where its bound base is expected.
TEST(LibraryPatterns, AnUnboundTemplateBaseIsBridgedToTheNearestBoundAncestor) {
    const auto k = rosetta::gen_detail::describe<lpns::FloatArray>();
    ASSERT_GE(k.ancestors.size(), 2u);
    EXPECT_EQ(k.ancestors.back(), "lpns::ArrayBase");

    namespace fs       = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "rosetta_lp_bridge";
    fs::remove_all(dir);
    rosetta::GenerateOptions opt;
    opt.out_dir = dir;
    opt.targets = {rosetta::TargetSpec{.lang = "python", .name = "lp"}};
    rosetta::generate<lpns::ArrayBase, lpns::FloatArray>(opt);
    std::ifstream     in(dir / "python" / "auto_pybind.cpp");
    const std::string s{std::istreambuf_iterator<char>(in), {}};
    fs::remove_all(dir);
    EXPECT_TRUE(has(s, "py::class_<lpns::FloatArray, lpns::ArrayBase>")) << s;
}
