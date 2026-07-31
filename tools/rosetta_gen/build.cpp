// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// ---------------------------------------------------------------------------
// --build: the whole pipeline in one command (portable — std::system +
// std::filesystem only, so the same code drives sh on POSIX and cmd on
// Windows). Emits the generator project, builds and runs it, then compiles
// every generated backend with its own build shape: plain CMake for most,
// npm for node, emcmake for wasm, a dotnet/mvn second stage for csharp/java.
// A backend whose toolchain is absent is SKIPPED (not an error); a failed
// build is reported in the summary and --build exits non-zero after trying
// the rest.
// ---------------------------------------------------------------------------

#include "build.h"
#include "emit.h"
#include "manifest.h"
#include "util.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

struct BuildOptions {
    fs::path                 manifest;
    fs::path                 gen_dir;      // default <manifest dir>/gen
    fs::path                 bindings_dir; // default <manifest dir>/bindings
    std::string              p2996_root;   // -DCLANG_P2996_ROOT for every configure
    std::string              qt_dir;       // -DQT_DIR for qt-/qml-expanded
    std::string              jobs;         // cmake --build --parallel N
    std::vector<std::string> only, skip;   // backend (target lang) filters
    std::vector<std::string> cmake_args;   // extra args for every configure
    bool                     fresh = false;
    bool                     wheel = false; // also run make_wheel.py (the expanded python backends)
    fs::path                 wheel_dir;     // shared --outdir for every make_wheel.py (implies wheel)
};

// Do this backend's generated projects carry a make_wheel.py? Only the two
// expanded Python backends emit one — the *thin* `python` / `nanobind` targets
// re-run the reflection walk at build time and so need a C++26 toolchain on
// whatever machine builds them, which is the opposite of a portable wheel.
static bool is_wheel_backend(const std::string &lang) {
    return lang == "python-expanded" || lang == "nanobind-expanded";
}

// The interpreter to run make_wheel.py with. Probed rather than assumed, in a
// platform-appropriate order: `python3` is the POSIX spelling, while on Windows
// it is usually the Store stub and `python` (or the `py` launcher) is the real
// one. Empty when none is on PATH.
static std::string find_python() {
#ifdef _WIN32
    const char *candidates[] = {"python", "python3", "py"};
#else
    const char *candidates[] = {"python3", "python"};
#endif
    for (const char *p : candidates) {
        if (have_tool(p)) {
            return p;
        }
    }
    return {};
}

static std::vector<std::string> split_list(const std::string &s) {
    std::vector<std::string> out;
    std::string::size_type   pos = 0;
    while (pos <= s.size()) {
        const auto comma = s.find(',', pos);
        const auto end   = (comma == std::string::npos) ? s.size() : comma;
        if (end > pos) {
            out.push_back(s.substr(pos, end - pos));
        }
        pos = end + 1;
    }
    return out;
}

static bool contains(const std::vector<std::string> &v, const std::string &s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

// The interpreter a target's wheel is built FOR. A wheel is tagged for whatever
// interpreter runs make_wheel.py, so a manifest that pins "python" must pin it
// here too — configuring CMake with python3.11 and then packaging with the
// probed python3.12 would produce a cp312 wheel from a 3.11 build. A bare
// version becomes pythonX.Y, resolved on PATH like any other command.
static std::string target_python(const TargetEntry &t, const std::string &fallback) {
    if (t.python.empty()) {
        return fallback;
    }
    return t.python.find_first_not_of("0123456789.") == std::string::npos ? "python" + t.python
                                                                          : t.python;
}

static int run_build(const BuildOptions &opt) {
    const Manifest m            = load(opt.manifest);
    const fs::path manifest_dir = fs::absolute(opt.manifest).parent_path();
    const fs::path gen_dir = opt.gen_dir.empty() ? manifest_dir / "gen" : opt.gen_dir;
    const fs::path bindings_dir =
        opt.bindings_dir.empty() ? manifest_dir / "bindings" : opt.bindings_dir;

    if (opt.fresh) {
        fs::remove_all(gen_dir);
        fs::remove_all(bindings_dir);
    }

    // Configure/build fragments shared by every CMake invocation.
    std::string conf;
    for (const auto &a : opt.cmake_args) {
        conf += " " + q(a);
    }
    if (!opt.p2996_root.empty()) {
        conf += " " + q("-DCLANG_P2996_ROOT=" + opt.p2996_root);
    }
    const std::string par = opt.jobs.empty() ? "" : " --parallel " + q(opt.jobs);

    auto cmake_build = [&](const fs::path &src, const std::string &extra = "") {
        return run_cmd("cmake -S " + q(src) + " -B " + q(src / "build") + conf + extra) == 0 &&
               run_cmd("cmake --build " + q(src / "build") + par) == 0;
    };

    // 1. generator project sources.
    emit_generator_project(m, gen_dir);

    // 2. build and run the generator (clang-p2996).
    std::fprintf(stderr, "== building the generator\n");
    if (!cmake_build(gen_dir)) {
        std::fprintf(stderr, "rosetta_gen: generator build failed\n");
        return 1;
    }
    const fs::path generator = find_generator(gen_dir.parent_path());
    if (generator.empty()) {
        std::fprintf(stderr, "rosetta_gen: generator binary not found beside %s\n",
                     gen_dir.string().c_str());
        return 1;
    }
    std::fprintf(stderr, "== generating bindings -> %s\n", bindings_dir.string().c_str());
    if (run_cmd(q(generator) + " " + q(bindings_dir)) != 0) {
        std::fprintf(stderr, "rosetta_gen: binding generation failed\n");
        return 1;
    }

    // 3. build each declared backend.
    std::vector<std::pair<std::string, std::string>> results; // backend -> verdict
    bool failed = false;
    auto record = [&](const std::string &b, const std::string &v) {
        results.emplace_back(b, v);
    };
    auto attempt = [&](const std::string &b, bool ok, const std::string &what = "") {
        if (ok) {
            record(b, "OK" + what);
        } else {
            record(b, "FAILED" + what);
            failed = true;
        }
    };

    // The manifest's "wheel" / "wheel_dir" are DEFAULTS for --wheel /
    // --wheel-dir: a project that always ships wheels says so once instead of
    // on every command line. The flags still win, and only ever toward doing
    // MORE — a manifest cannot silently turn packaging off for a run that asked
    // for it. A manifest "wheel_dir" implies wheeling, exactly as the flag does.
    const bool     wheel     = opt.wheel || m.wheel || !m.wheel_dir.empty();
    const fs::path wheel_dir = opt.wheel_dir.empty() ? m.wheel_dir : opt.wheel_dir;

    // --wheel: resolved once, and only when asked for — the probe spawns a
    // process. A --wheel that can never fire says so rather than doing nothing
    // quietly; the check honours --only / --skip, since those are the likelier
    // reason no wheel backend survives to be built.
    const std::string python = wheel ? find_python() : std::string{};
    // --wheel-dir: one directory for every backend's wheels instead of a dist/
    // per binding dir. Resolved against the *invocation* cwd here, before any
    // per-backend run_cmd changes directory — make_wheel.py chdirs to its own
    // location, so a relative --outdir would land inside the binding dir and
    // defeat the point of collecting them.
    std::string wheel_out;
    if (!wheel_dir.empty()) {
        wheel_out = " --outdir " + q(fs::absolute(wheel_dir));
    }
    if (wheel &&
        std::none_of(m.targets.begin(), m.targets.end(), [&](const TargetEntry &t) {
            return is_wheel_backend(t.lang) &&
                   (opt.only.empty() || contains(opt.only, t.lang)) &&
                   !contains(opt.skip, t.lang);
        })) {
        std::fprintf(stderr,
                     "rosetta_gen: --wheel has no effect — no python-expanded / "
                     "nanobind-expanded target is being built\n");
    }

    std::vector<std::string> done; // a lang may appear once per manifest only, but be safe
    for (const auto &t : m.targets) {
        const std::string &lang = t.lang;
        if (contains(done, lang)) {
            continue;
        }
        done.push_back(lang);
        if (!opt.only.empty() && !contains(opt.only, lang)) {
            continue;
        }
        if (contains(opt.skip, lang)) {
            record(lang, "SKIPPED (--skip)");
            continue;
        }
        const fs::path dir = bindings_dir / lang;
        if (!fs::exists(dir)) {
            record(lang, "SKIPPED (nothing generated)");
            continue;
        }
        std::fprintf(stderr, "== building %s\n", lang.c_str());

        if (lang == "html" || lang == "markdown" || lang == "typescript" ||
            lang == "paraview" || lang == "openapi" || lang == "json-schema") {
            record(lang, "OK (nothing to compile)");
        } else if (lang == "node" || lang == "node-expanded") {
            if (!have_tool("npm")) {
                record(lang, "SKIPPED (npm not found)");
                continue;
            }
            std::string build = "npm run build";
            if (!opt.p2996_root.empty()) {
                build += " -- " + q("--CDCLANG_P2996_ROOT=" + opt.p2996_root);
            }
            attempt(lang, run_cmd("npm install", dir) == 0 && run_cmd(build, dir) == 0);
        } else if (lang == "wasm" || lang == "wasm-expanded") {
            if (!have_tool("emcc")) {
                record(lang, "SKIPPED (emcc not found — activate emsdk)");
                continue;
            }
            std::string extra;
            for (const auto &a : opt.cmake_args) {
                extra += " " + q(a);
            }
            attempt(lang, run_cmd("emcmake cmake -S " + q(dir) + " -B " + q(dir / "build") +
                                  extra) == 0 &&
                              run_cmd("cmake --build " + q(dir / "build") + par) == 0);
        } else if (lang == "julia" || lang == "julia-expanded") {
            // the generated CMake locates JlCxx by running julia (CxxWrap.prefix_path())
            if (!have_tool("julia")) {
                record(lang, "SKIPPED (julia not found)");
                continue;
            }
            attempt(lang, cmake_build(dir));
        } else if (lang == "csharp" || lang == "csharp-expanded") {
            if (!cmake_build(dir)) {
                attempt(lang, false);
            } else if (!have_tool("dotnet")) {
                record(lang, "OK (native only — dotnet not found)");
            } else {
                attempt(lang, run_cmd("dotnet build " + q(t.name + ".csproj"), dir) == 0,
                        " (dotnet)");
            }
        } else if (lang == "java" || lang == "java-expanded") {
            if (!cmake_build(dir)) {
                attempt(lang, false);
            } else if (!have_tool("mvn")) {
                record(lang, "OK (native only — mvn not found)");
            } else {
                attempt(lang, run_cmd("mvn -q package", dir) == 0, " (mvn)");
            }
        } else if (lang == "qt-expanded" || lang == "qml-expanded") {
            std::string extra;
            if (!opt.qt_dir.empty()) {
                extra = " " + q("-DQT_DIR=" + opt.qt_dir);
            }
            attempt(lang, cmake_build(dir, extra));
        } else {
            // python, nanobind, rest, json, lua-expanded, imgui-expanded, …
            if (!cmake_build(dir)) {
                attempt(lang, false);
            } else if (!wheel || !is_wheel_backend(lang)) {
                attempt(lang, true);
            } else if (target_python(t, python).empty()) {
                record(lang, "OK (no wheel — no python interpreter found)");
            } else {
                // The wheel is a second, independent build: scikit-build-core
                // re-runs this same CMakeLists in its own tree (SKBUILD set),
                // so it neither reuses nor disturbs the build/ dir above.
                std::fprintf(stderr, "== packaging %s\n", lang.c_str());
                attempt(lang,
                        run_cmd(q(target_python(t, python)) + " make_wheel.py" + wheel_out, dir) ==
                            0,
                        " (wheel)");
            }
        }
    }

    std::fprintf(stderr, "\n== summary\n");
    for (const auto &[b, v] : results) {
        std::fprintf(stderr, "   %-20s %s\n", b.c_str(), v.c_str());
    }
    return failed ? 1 : 0;
}

const char *kBuildOptions =
    "  --gen-dir DIR            generator project dir   (default: <manifest dir>/gen)\n"
    "  --bindings-dir DIR       generated bindings dir  (default: <manifest dir>/bindings)\n"
    "  --clang-p2996-root PATH  -DCLANG_P2996_ROOT for every CMake configure\n"
    "  --qt-dir PATH            -DQT_DIR for the qt-/qml-expanded builds\n"
    "  --only a,b / --skip a,b  restrict the backend builds to / exclude these\n"
    "  --cmake-arg ARG          extra argument for every CMake configure (repeatable)\n"
    "  --jobs N                 parallel build jobs\n"
    "  --fresh                  wipe the gen and bindings dirs first\n"
    "  --wheel                  also build a Python wheel for the python-expanded /\n"
    "                           nanobind-expanded targets (runs their make_wheel.py;\n"
    "                           wheels land in <bindings>/<lang>/dist)\n"
    "  --wheel-dir DIR          collect every wheel in DIR instead of a per-backend\n"
    "                           dist/ (implies --wheel)\n";

static void print_build_usage(std::FILE *to) {
    std::fprintf(to, "usage: rosetta_gen --build <manifest.json> [options]\n%s", kBuildOptions);
}

int build_main(int argc, char **argv) {
    BuildOptions opt;
    for (int i = 2; i < argc; ++i) {
        const std::string a    = argv[i];
        auto              next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "rosetta_gen: %s needs a value\n", a.c_str());
                print_build_usage(stderr);
                std::exit(1);
            }
            return argv[++i];
        };
        if (a == "--gen-dir") {
            opt.gen_dir = next();
        } else if (a == "--bindings-dir") {
            opt.bindings_dir = next();
        } else if (a == "--clang-p2996-root") {
            opt.p2996_root = next();
        } else if (a == "--qt-dir") {
            opt.qt_dir = next();
        } else if (a == "--only") {
            opt.only = split_list(next());
        } else if (a == "--skip") {
            opt.skip = split_list(next());
        } else if (a == "--cmake-arg") {
            opt.cmake_args.push_back(next());
        } else if (a == "--jobs") {
            opt.jobs = next();
        } else if (a == "--fresh") {
            opt.fresh = true;
        } else if (a == "--wheel") {
            opt.wheel = true;
        } else if (a == "--wheel-dir") {
            opt.wheel_dir = next();
            opt.wheel     = true; // asking where the wheels go is asking for wheels
        } else if (a == "-h" || a == "--help") {
            print_build_usage(stdout);
            return 0;
        } else if (a[0] == '-') {
            std::fprintf(stderr, "rosetta_gen: unknown --build option %s\n", a.c_str());
            print_build_usage(stderr);
            return 1;
        } else if (opt.manifest.empty()) {
            opt.manifest = a;
        } else {
            std::fprintf(stderr, "rosetta_gen: unexpected argument %s\n", a.c_str());
            print_build_usage(stderr);
            return 1;
        }
    }
    if (opt.manifest.empty()) {
        print_build_usage(stderr);
        return 1;
    }
    if (!fs::exists(opt.manifest)) {
        std::fprintf(stderr, "rosetta_gen: manifest not found: %s\n",
                     opt.manifest.string().c_str());
        return 1;
    }
    try {
        return run_build(opt);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "rosetta_gen: %s\n", e.what());
        return 1;
    }
}
