// Copyright (c) fmaerten@gmail.com
// License: MIT

// Google Test suite for SPLIT binding sources (GenerateOptions::binding_shards):
// python, wasm and julia cut their class registrations into part files that
// compile in parallel — what makes a library of a thousand classes buildable.
// The parts must be listed in the emitted CMake, called in manifest order
// (bases before derived), and carry the preamble every TU needs. One slice must
// reproduce the single file of before, byte for byte.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <rosetta/generate.h>
#include <string>

namespace shns {
    struct A {
        int a = 0;
    };
    struct B : A {
        int b() const { return 1; }
    };
    struct C {
        double c = 0;
    };
    struct D {
        double d() const { return 2; }
    };
} // namespace shns

template <> struct rosetta::binding_info<shns::A> {
    static constexpr const char *header = "sh.h";
};
template <> struct rosetta::binding_info<shns::B> {
    static constexpr const char *header = "sh.h";
};
template <> struct rosetta::binding_info<shns::C> {
    static constexpr const char *header = "sh.h";
};
template <> struct rosetta::binding_info<shns::D> {
    static constexpr const char *header = "sh.h";
};

namespace {
    namespace fs = std::filesystem;

    std::string read_all(const fs::path &p) {
        std::ifstream in(p);
        return {std::istreambuf_iterator<char>(in), {}};
    }

    bool has(const std::string &hay, const std::string &needle) {
        return hay.find(needle) != std::string::npos;
    }

    fs::path emit(const char *lang, std::size_t shards) {
        const fs::path dir = fs::temp_directory_path() /
                             ("rosetta_shard_" + std::string(lang) + "_" + std::to_string(shards));
        fs::remove_all(dir);
        auto c = rosetta::gen_detail::make_context<shns::A, shns::B, shns::C, shns::D>("sh");
        c.out_dir        = dir;
        c.binding_shards = shards;
        rosetta::backend_registry().at(lang)->emit(c);
        return dir / lang;
    }
} // namespace

TEST(Sharding, SlicesAreContiguousAndCoverEveryClass) {
    auto c           = rosetta::gen_detail::make_context<shns::A, shns::B, shns::C, shns::D>("sh");
    c.binding_shards = 3;
    const auto s     = rosetta::gen_detail::class_slices(c);
    ASSERT_EQ(s.size(), 3u);
    EXPECT_EQ(s.front().first, 0u);
    EXPECT_EQ(s.back().second, 4u);
    for (std::size_t i = 1; i < s.size(); ++i) {
        EXPECT_EQ(s[i].first, s[i - 1].second);
    }
    c.binding_shards = 0; // auto: a small module stays in one file
    EXPECT_EQ(rosetta::gen_detail::class_slices(c).size(), 1u);
}

TEST(Sharding, PythonSplitsIntoPartsCalledInOrder) {
    const fs::path dir  = emit("python", 2);
    const std::string m = read_all(dir / "auto_pybind.cpp");
    const std::string p0 = read_all(dir / "auto_pybind_part_0.cpp");
    const std::string p1 = read_all(dir / "auto_pybind_part_1.cpp");
    const std::string cm = read_all(dir / "CMakeLists.txt");
    EXPECT_LT(m.find("rosetta_py_part_0(m);"), m.find("rosetta_py_part_1(m);"));
    EXPECT_TRUE(has(p0, "void rosetta_py_part_0(py::module_ &m) {"));
    EXPECT_TRUE(has(p0, "py::class_<shns::A>")) << "A and B are the first slice";
    EXPECT_TRUE(has(p1, "py::class_<shns::C>"));
    EXPECT_FALSE(has(m, "py::class_<")) << "the module entry registers no class itself";
    EXPECT_TRUE(has(p1, "#include <pybind11/pybind11.h>")) << "every part has the preamble";
    EXPECT_TRUE(has(cm, "auto_pybind.cpp auto_pybind_part_0.cpp auto_pybind_part_1.cpp")) << cm;
}

TEST(Sharding, WasmSplitsIntoBindingBlocks) {
    const fs::path    dir = emit("wasm", 2);
    const std::string p1  = read_all(dir / "auto_emscripten_part_1.cpp");
    EXPECT_TRUE(has(p1, "EMSCRIPTEN_BINDINGS(sh_part_1) {"));
    EXPECT_TRUE(has(p1, "emscripten::class_<shns::C>"));
    EXPECT_TRUE(has(read_all(dir / "CMakeLists.txt"),
                    "add_executable(sh auto_emscripten.cpp auto_emscripten_part_0.cpp "
                    "auto_emscripten_part_1.cpp"));
}

TEST(Sharding, JuliaKeepsAddTypeInTheEntryAndRebuildsWrappersInParts) {
    const fs::path    dir = emit("julia", 2);
    const std::string m   = read_all(dir / "auto_jlcxx.cpp");
    const std::string p0  = read_all(dir / "auto_jlcxx_part_0.cpp");
    EXPECT_TRUE(has(m, "mod.add_type<shns::C>")) << "phase 1 stays in the module entry";
    EXPECT_LT(m.find("mod.add_type<shns::D>"), m.find("rosetta_jl_part_0(mod);"));
    EXPECT_TRUE(has(p0, "jlcxx::TypeWrapper<shns::A> c_A(mod, jlcxx::julia_type<shns::A>()->super, "
                        "jlcxx::julia_type<shns::A>());"))
        << p0;
    EXPECT_TRUE(has(read_all(dir / "CMakeLists.txt"),
                    "add_library(sh SHARED auto_jlcxx.cpp auto_jlcxx_part_0.cpp auto_jlcxx_part_1.cpp)"));
}

TEST(Sharding, OneSliceIsTheSingleFileOfBefore) {
    const fs::path dir = emit("python", 1);
    EXPECT_FALSE(fs::exists(dir / "auto_pybind_part_0.cpp"));
    EXPECT_TRUE(has(read_all(dir / "auto_pybind.cpp"), "py::class_<shns::A>"));
    EXPECT_TRUE(has(read_all(dir / "CMakeLists.txt"), "NO_EXTRAS auto_pybind.cpp)"));
}
