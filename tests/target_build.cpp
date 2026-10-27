// Copyright (c) fmaerten@gmail.com
// License: MIT

// Google Test suite for the PER-TARGET build overrides: a manifest target's
// "compile_options", "user_include", "user_lib" and (wasm only) "exceptions".
//
// The motivating library is VTK, whose native and WebAssembly flavours are two
// different builds: two install trees (so two include dirs and two library
// lists) and, on the wasm side, -fwasm-exceptions forced on every object — a
// module built with emscripten's JS exception model (the template default)
// cannot even link against it. One top-level user_lib / user_include could not
// describe both, and the wasm template had no compile-flag hook at all.
//
// Two halves: the manifest LOADER (what the keys parse into, and what it
// refuses), then the EMITTED CMake (what each target's project ends up with,
// including generate()'s per-target replacement of the shared lists).
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <manifest.h>
#include <rosetta/generate.h>
#include <sstream>
#include <stdexcept>
#include <string>

namespace tbns {
    class Thing {
    public:
        Thing() = default;
        double value() const { return 0.0; }
    };
} // namespace tbns

template <> struct rosetta::binding_info<tbns::Thing> {
    static constexpr const char *header = "Thing.h";
};

namespace {

    // ---- loader fixtures ----------------------------------------------------

    struct Tree {
        fs::path root;

        explicit Tree(const std::string &tag) {
            static int n = 0;
            root         = fs::temp_directory_path() /
                   ("rosetta_target_build_" + tag + "_" + std::to_string(++n));
            fs::remove_all(root);
            fs::create_directories(root);
            std::ofstream(root / "Point.h") << "struct Point {};\n";
        }
        ~Tree() {
            std::error_code ec;
            fs::remove_all(root, ec);
        }

        fs::path manifest(const std::string &targets) const {
            const fs::path p = root / "manifest.json";
            std::ofstream(p) << "{\n"
                             << "  \"user_include\": \".\",\n"
                             << "  \"rosetta_include\": \".\",\n"
                             << "  \"module_name\": \"geom\",\n"
                             << "  \"user_lib\": {\"name\": \"shared_lib\", \"dir\": \"lib\"},\n"
                             << "  \"targets\": [" << targets << "],\n"
                             << "  \"classes\": [{\"header\": \"Point.h\"}]\n}\n";
            return p;
        }
    };

    const TargetEntry &target_of(const Manifest &m, const std::string &lang) {
        for (const auto &t : m.targets) {
            if (t.lang == lang) {
                return t;
            }
        }
        throw std::runtime_error("no such target: " + lang);
    }

    std::string load_error(const Tree &tree, const std::string &targets) {
        try {
            load(tree.manifest(targets));
        } catch (const std::exception &e) {
            return e.what();
        }
        return {};
    }

    // ---- emitter fixtures ---------------------------------------------------

    std::string read_all(const fs::path &p) {
        std::ifstream     in(p);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    // Emit one backend from a hand-adjusted context; hand back its CMakeLists.
    std::string cmake_of(const char *lang, const std::function<void(rosetta::GenContext &)> &set) {
        // Named after the running test: ctest -j runs every test in its own
        // process, so a per-process counter alone would collide.
        static int     n   = 0;
        const fs::path dir = fs::temp_directory_path() /
                             ("rosetta_tb_emit_" +
                              std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) +
                              "_" + std::to_string(++n));
        fs::remove_all(dir);
        auto c    = rosetta::gen_detail::make_context<tbns::Thing>("tb");
        c.out_dir = dir;
        set(c);
        rosetta::backend_registry().at(lang)->emit(c);
        const std::string out = read_all(dir / (std::string(lang) == "python" ? "python" : lang) /
                                         "CMakeLists.txt");
        fs::remove_all(dir);
        return out;
    }

    bool has(const std::string &hay, const std::string &needle) {
        return hay.find(needle) != std::string::npos;
    }

} // namespace

// ---- the loader ---------------------------------------------------------------

TEST(TargetBuild, EveryKeyParsesAndPathsResolveAgainstTheManifest) {
    Tree           tree("parse");
    const Manifest m = load(tree.manifest(
        R"({"lang": "python"},
           {"lang": "wasm", "compile_options": ["-DVTK_WASM", "-O2"],
            "user_include": ["vtk-wasm/include", "extra"],
            "user_lib": [{"name": "vtkCommonCore", "dir": "vtk-wasm/lib", "link": "static"}],
            "exceptions": "wasm"})"));

    const TargetEntry &py = target_of(m, "python");
    EXPECT_TRUE(py.compile_options.empty());
    EXPECT_TRUE(py.user_include.empty());
    EXPECT_TRUE(py.user_libs.empty()); // keeps the shared list
    EXPECT_TRUE(py.exceptions.empty());

    const TargetEntry &wa = target_of(m, "wasm");
    EXPECT_EQ(wa.compile_options, (std::vector<std::string>{"-DVTK_WASM", "-O2"}));
    ASSERT_EQ(wa.user_include.size(), 2u);
    EXPECT_EQ(fs::path(wa.user_include[0]), fs::weakly_canonical(tree.root / "vtk-wasm/include"));
    ASSERT_EQ(wa.user_libs.size(), 1u);
    EXPECT_EQ(wa.user_libs[0].name, "vtkCommonCore");
    EXPECT_EQ(fs::path(wa.user_libs[0].dir), fs::weakly_canonical(tree.root / "vtk-wasm/lib"));
    EXPECT_EQ(wa.user_libs[0].link, "static");
    EXPECT_EQ(wa.exceptions, "wasm");

    // The shared list is untouched: the generator driver links it.
    ASSERT_EQ(m.user_libs.size(), 1u);
    EXPECT_EQ(m.user_libs[0].name, "shared_lib");
}

TEST(TargetBuild, AStringUserIncludeAndAnObjectUserLibAreTheShorthands) {
    Tree           tree("short");
    const Manifest m = load(tree.manifest(
        R"({"lang": "node", "user_include": "inc", "user_lib": {"name": "x", "dir": "d"}})"));
    const TargetEntry &t = target_of(m, "node");
    ASSERT_EQ(t.user_include.size(), 1u);
    ASSERT_EQ(t.user_libs.size(), 1u);
    EXPECT_EQ(t.user_libs[0].name, "x");
}

TEST(TargetBuild, ExceptionsIsAWasmKeyWithTwoValues) {
    Tree tree("exc");
    EXPECT_TRUE(has(load_error(tree, R"({"lang": "python", "exceptions": "wasm"})"),
                    "only a \"wasm\" target"));
    EXPECT_TRUE(has(load_error(tree, R"({"lang": "wasm", "exceptions": "native"})"),
                    "must be \"js\""));
    EXPECT_TRUE(load_error(tree, R"({"lang": "wasm", "exceptions": "js"})").empty());
}

TEST(TargetBuild, MalformedOverridesAreRefused) {
    Tree tree("bad");
    EXPECT_TRUE(has(load_error(tree, R"({"lang": "python", "compile_options": "-O2"})"),
                    "\"compile_options\" must be an array"));
    EXPECT_TRUE(has(load_error(tree, R"({"lang": "python", "compile_options": [""]})"),
                    "must not be empty"));
    EXPECT_TRUE(has(load_error(tree, R"({"lang": "python", "user_lib": {"dir": "d"}})"),
                    "a target's \"user_lib\""));
    EXPECT_TRUE(has(load_error(tree, R"({"lang": "python", "user_lib": []})"),
                    "must not be empty"));
}

// ---- the emitted CMake -----------------------------------------------------------

TEST(TargetBuild, CompileOptionsReachTheNativeBindingTarget) {
    const std::string cm = cmake_of(
        "python", [](rosetta::GenContext &c) { c.compile_options = {"-DONE", "-ftwo"}; });
    EXPECT_TRUE(has(cm, "target_compile_options(${ROSETTA_BINDING_TARGET} PRIVATE\n"
                        "    \"-DONE\"\n    \"-ftwo\"\n)"))
        << cm;
}

TEST(TargetBuild, CompileOptionsReachTheWasmBindingTarget) {
    const std::string cm =
        cmake_of("wasm", [](rosetta::GenContext &c) { c.compile_options = {"-DONE"}; });
    EXPECT_TRUE(has(cm, "target_compile_options(tb PRIVATE\n    \"-DONE\")")) << cm;
}

TEST(TargetBuild, TheWasmTemplateDefaultsToTheJsExceptionModel) {
    const std::string cm = cmake_of("wasm", [](rosetta::GenContext &) {});
    EXPECT_TRUE(has(cm, "-sDISABLE_EXCEPTION_CATCHING=0"));
    EXPECT_FALSE(has(cm, "-fwasm-exceptions"));
    EXPECT_FALSE(has(cm, "{{WASM_EXCEPTIONS}}"));
}

// Native wasm EH is all or nothing: the binding TU must be compiled with it
// AND the link must use it — and the JS-model flag, which emcc rejects next to
// -fwasm-exceptions, must be gone.
TEST(TargetBuild, NativeWasmExceptionsSwitchBothSides) {
    const std::string cm =
        cmake_of("wasm", [](rosetta::GenContext &c) { c.exceptions = "wasm"; });
    EXPECT_TRUE(has(cm, "target_compile_options(tb PRIVATE -fwasm-exceptions)")) << cm;
    EXPECT_TRUE(has(cm, "-sALLOW_MEMORY_GROWTH=1 -fwasm-exceptions")) << cm;
    EXPECT_FALSE(has(cm, "DISABLE_EXCEPTION_CATCHING")) << cm;
}

// generate() is where a target's own include dirs and libraries replace the
// shared ones — prepended includes, replaced libraries — so drive it whole.
TEST(TargetBuild, GeneratePrependsTargetIncludesAndReplacesTargetLibraries) {
    const fs::path dir = fs::temp_directory_path() / "rosetta_tb_generate";
    fs::remove_all(dir);

    rosetta::GenerateOptions opt;
    opt.out_dir         = dir;
    opt.user_include    = {"/shared/include"};
    opt.rosetta_include = "/rosetta/include";
    opt.user_libs       = {{"shared_lib", "/shared/lib", "shared"}};
    rosetta::TargetSpec py{.lang = "python", .name = "tb"};
    rosetta::TargetSpec wa{.lang = "wasm", .name = "tb"};
    wa.user_include = {"/wasm/include"};
    wa.user_libs    = {{"wasm_lib", "/wasm/lib", "static"}};
    wa.exceptions   = "wasm";
    opt.targets     = {py, wa};
    rosetta::generate<tbns::Thing>(opt);

    const std::string pyc = read_all(dir / "python" / "CMakeLists.txt");
    const std::string wac = read_all(dir / "wasm" / "CMakeLists.txt");
    fs::remove_all(dir);

    EXPECT_TRUE(has(pyc, "/shared/include"));
    EXPECT_FALSE(has(pyc, "/wasm/include"));
    EXPECT_TRUE(has(pyc, "\"shared_lib\""));
    EXPECT_FALSE(has(pyc, "\"wasm_lib\""));

    const auto own    = wac.find("/wasm/include");
    const auto shared = wac.find("/shared/include");
    ASSERT_NE(own, std::string::npos);
    ASSERT_NE(shared, std::string::npos);
    EXPECT_LT(own, shared) << "the target's own include dir must win the lookup";
    EXPECT_TRUE(has(wac, "\"wasm_lib\""));
    EXPECT_FALSE(has(wac, "\"shared_lib\""));
    EXPECT_TRUE(has(wac, "-fwasm-exceptions"));
}

// ---- "holders" (holder policies, rosetta/holder.h) --------------------------------

namespace {
    std::string holders_error(const Tree &tree, const std::string &holders) {
        const fs::path p = tree.root / "manifest.json";
        std::ofstream(p) << "{\n  \"user_include\": \".\",\n  \"rosetta_include\": \".\",\n"
                         << "  \"module_name\": \"geom\",\n  \"targets\": [\"python\"],\n"
                         << "  \"holders\": " << holders << ",\n"
                         << "  \"classes\": [{\"header\": \"Point.h\"}]\n}\n";
        try {
            load(p);
        } catch (const std::exception &e) {
            return e.what();
        }
        return {};
    }
} // namespace

TEST(ManifestHolders, AFullPolicyParses) {
    Tree           tree("holders");
    const fs::path p = tree.root / "manifest.json";
    std::ofstream(p) << R"J({
      "user_include": ".", "rosetta_include": ".", "module_name": "geom",
      "targets": ["python"],
      "holders": [{"base": "vtkObjectBase", "holder": "vtkSmartPointer",
                   "header": "vtkSmartPointer.h", "get": "GetPointer",
                   "retain": "Register(nullptr)", "release": "UnRegister(nullptr)",
                   "factory": "New", "owned_returns": ["New*", "CreateArray"]}],
      "classes": [{"header": "Point.h"}]})J";
    const Manifest m = load(p);
    ASSERT_EQ(m.holders.size(), 1u);
    const HolderEntry &h = m.holders[0];
    EXPECT_EQ(h.base, "vtkObjectBase");
    EXPECT_EQ(h.holder, "vtkSmartPointer");
    EXPECT_EQ(h.get, "GetPointer");
    EXPECT_EQ(h.retain, "Register(nullptr)");
    EXPECT_EQ(h.release, "UnRegister(nullptr)");
    EXPECT_EQ(h.factory, "New");
    EXPECT_EQ(h.owned_returns, (std::vector<std::string>{"New*", "CreateArray"}));
}

TEST(ManifestHolders, TheRequiredKeysAreRequired) {
    Tree tree("holders_req");
    EXPECT_TRUE(has(holders_error(tree, R"J([{"holder": "P", "retain": "r()", "release": "u()"}])J"),
                    "needs \"base\""));
    EXPECT_TRUE(has(holders_error(tree, R"J([{"base": "B", "retain": "r()", "release": "u()"}])J"),
                    "needs \"holder\""));
    EXPECT_TRUE(has(holders_error(tree, R"J([{"base": "B", "holder": "P", "release": "u()"}])J"),
                    "needs \"retain\""));
    EXPECT_TRUE(has(holders_error(tree, R"J({"base": "B"})J"), "must be an array"));
}

TEST(ManifestHolders, TwoPoliciesCannotShareABase) {
    Tree tree("holders_dup");
    const std::string one = R"J({"base": "B", "holder": "P", "retain": "r()", "release": "u()"})J";
    EXPECT_TRUE(has(holders_error(tree, "[" + one + ", " + one + "]"), "same base"));
}
