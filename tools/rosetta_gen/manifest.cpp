// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// load(): manifest.json -> Manifest (structs and field docs in manifest.h),
// plus the shell-glob expansion `user_sources` patterns go through.

#include "manifest.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>

using json = nlohmann::json;

// True if `s` carries shell-glob magic (so it must be expanded, not used as a
// literal path). Matches POSIX glob's special characters.
static bool is_glob_pattern(const std::string &s) {
    return s.find_first_of("*?[") != std::string::npos;
}

// Match one path component against a glob component: `*`, `?` and `[...]`
// (with `!`/`^` negation and `a-z` ranges), POSIX-glob style. Hand-rolled so
// the tool builds on Windows too (no <glob.h>/fnmatch there); iterative with
// single-`*` backtracking.
static bool glob_match(const std::string &pat, const std::string &str) {
    std::size_t p = 0, s = 0;
    std::size_t star_p = std::string::npos, star_s = 0;
    // like POSIX glob, a wildcard never matches a leading dot
    if (!str.empty() && str[0] == '.' && !pat.empty() && pat[0] != '.') {
        return false;
    }
    while (s < str.size()) {
        bool step = false;
        if (p < pat.size() && pat[p] == '[') {
            std::size_t q   = p + 1;
            bool        neg = false;
            if (q < pat.size() && (pat[q] == '!' || pat[q] == '^')) {
                neg = true;
                ++q;
            }
            bool       hit   = false;
            bool       first = true;
            const char c     = str[s];
            while (q < pat.size() && (pat[q] != ']' || first)) {
                first = false;
                if (q + 2 < pat.size() && pat[q + 1] == '-' && pat[q + 2] != ']') {
                    hit = hit || (pat[q] <= c && c <= pat[q + 2]);
                    q += 3;
                } else {
                    hit = hit || (pat[q] == c);
                    ++q;
                }
            }
            if (q < pat.size() && hit != neg) { // q at ']' (unterminated set never matches)
                p    = q + 1;
                ++s;
                step = true;
            }
        } else if (p < pat.size() && (pat[p] == '?' || pat[p] == str[s])) {
            ++p;
            ++s;
            step = true;
        } else if (p < pat.size() && pat[p] == '*') {
            star_p = p++;
            star_s = s;
            step   = true;
        }
        if (!step) {
            if (star_p == std::string::npos) {
                return false;
            }
            p = star_p + 1; // give the last '*' one more character
            s = ++star_s;
        }
    }
    while (p < pat.size() && pat[p] == '*') {
        ++p;
    }
    return p == pat.size();
}

// Expand a shell glob (relative to `base`) into the matching files, sorted for
// reproducible output. Used by `user_sources` so a manifest can say
// "src/algorithms/*.cpp" instead of listing every file. Expands `*`, `?` and
// `[...]` within a path component (not recursive `**`); a pattern that matches
// nothing yields an empty list (the caller warns). std::filesystem-based so it
// behaves identically on POSIX and Windows.
static std::vector<fs::path> expand_glob(const fs::path &base, const std::string &pattern) {
    const fs::path full = fs::absolute(base / fs::path(pattern)).lexically_normal();

    std::vector<fs::path> frontier{full.root_path()};
    for (const auto &part : full.relative_path()) {
        const std::string     comp = part.string();
        std::vector<fs::path> next;
        for (const auto &dir : frontier) {
            std::error_code ec;
            if (!is_glob_pattern(comp)) {
                fs::path p = dir / part;
                if (fs::exists(p, ec)) {
                    next.push_back(std::move(p));
                }
                continue;
            }
            for (const auto &entry : fs::directory_iterator(dir, ec)) {
                if (glob_match(comp, entry.path().filename().string())) {
                    next.push_back(entry.path());
                }
            }
        }
        frontier = std::move(next);
        if (frontier.empty()) {
            break;
        }
    }

    std::vector<fs::path> out;
    for (const auto &p : frontier) {
        out.push_back(fs::weakly_canonical(p));
    }
    std::sort(out.begin(), out.end());
    return out;
}

Manifest load(const fs::path &manifest_path) {
    std::ifstream in(manifest_path);
    if (!in) {
        throw std::runtime_error("cannot open " + manifest_path.string());
    }
    // Tolerate // and /* */ comments in the manifest.
    json j = json::parse(in, /*cb=*/nullptr, /*allow_exceptions=*/true,
                         /*ignore_comments=*/true);

    Manifest       m;
    const fs::path base = fs::absolute(manifest_path).parent_path();

    // `user_include` is either a single string ("../my_lib") or an array of
    // them (["../my_lib", "../shared"]). Each is resolved relative to the
    // manifest (or taken absolute) and added to the bindings' include path.
    {
        const auto &ui = j.at("user_include");
        auto        add = [&](const std::string &p) {
            m.user_include.push_back(fs::weakly_canonical(base / fs::path(p)));
        };
        if (ui.is_array()) {
            if (ui.empty()) {
                throw std::runtime_error("\"user_include\" array must not be empty");
            }
            for (const auto &e : ui) {
                add(e.get<std::string>());
            }
        } else {
            add(ui.get<std::string>());
        }
    }
    m.rosetta_include =
        fs::weakly_canonical(base / fs::path(j.at("rosetta_include").get<std::string>()));

    // `generator_name` is optional; falls back to the manifest's parent
    // directory name (the driver tool / CMake target name).
    m.generator_name = j.contains("generator_name") ? j.at("generator_name").get<std::string>()
                                                    : base.filename().string();

    // `module_name` is optional too; the default binding module name when a
    // target gives no `name`. Falls back to `generator_name`.
    const std::string module_name =
        j.contains("module_name") ? j.at("module_name").get<std::string>() : m.generator_name;

    // A target is either a bare string ("node") — using module_name — or an
    // object { "lang": ..., "name": ..., "link_options": [...] } overriding
    // the module name and optionally adding per-target linker flags.
    for (const auto &t : j.at("targets")) {
        TargetEntry e;
        if (t.is_string()) {
            e.lang = t.get<std::string>();
            e.name = module_name;
        } else {
            e.lang = t.at("lang").get<std::string>();
            e.name = t.contains("name") ? t.at("name").get<std::string>() : module_name;
            // Optional per-target linker flags (see TargetEntry::link_options).
            if (t.contains("link_options")) {
                for (const auto &o : t.at("link_options")) {
                    std::string flag = o.get<std::string>();
                    if (flag.empty()) {
                        throw std::runtime_error(
                            "\"link_options\" entries must not be empty");
                    }
                    e.link_options.push_back(std::move(flag));
                }
            }
        }
        m.targets.push_back(std::move(e));
    }

    // Optional shared defaults, factoring the per-entry repetition out of
    // "classes" / "functions" / "extensions":
    //   "namespace"  — default C++ namespace for entry names that carry no
    //                  "::" of their own ("Serie" → "stressinv::Serie"). A
    //                  name containing "::" is taken verbatim (so fully
    //                  qualified spellings — incl. nested classes — keep
    //                  working), and a leading "::" pins an entry to the
    //                  global namespace ("::Thing" → "Thing").
    //   "header_dir" — directory fragment prepended to every entry header
    //                  ("Serie.h" → "stressinv/Serie.h").
    const std::string default_ns =
        j.contains("namespace") ? j.at("namespace").get<std::string>() : std::string{};
    std::string header_dir =
        j.contains("header_dir") ? j.at("header_dir").get<std::string>() : std::string{};
    if (!header_dir.empty() && header_dir.back() != '/') {
        header_dir += '/';
    }
    auto qualify = [&](std::string n) {
        if (n.rfind("::", 0) == 0) {
            return n.substr(2); // explicit global namespace
        }
        if (!default_ns.empty() && n.find("::") == std::string::npos) {
            return default_ns + "::" + n;
        }
        return n;
    };

    for (const auto &c : j.at("classes")) {
        ClassEntry e;
        e.header = header_dir + c.at("header").get<std::string>();
        // `name` is optional; fall back to the header's basename (stem).
        e.name = qualify(c.contains("name") ? c.at("name").get<std::string>()
                                            : fs::path(e.header).stem().string());
        // `annotations` is optional: an out-of-line annotation JSON side-car
        // (doc/range/readonly/combobox keyed by member name). Baked into
        // bindings.h at generation time, so the user's header stays clean.
        if (c.contains("annotations")) {
            e.annotations =
                fs::weakly_canonical(base / fs::path(c.at("annotations").get<std::string>()));
        }
        // `final` is optional: suppress the trampoline (see ClassEntry::final_).
        if (c.contains("final")) {
            e.final_ = c.at("final").get<bool>();
        }
        // `extensions` is optional: free functions (first parameter `Cls&`)
        // exposed as instance methods of this class — same entry shape as
        // "functions" (see ClassEntry::extensions).
        if (c.contains("extensions")) {
            for (const auto &x : c.at("extensions")) {
                FunctionEntry xe;
                xe.name   = qualify(x.at("name").get<std::string>());
                xe.header = header_dir + x.at("header").get<std::string>();
                xe.doc    = x.contains("doc") ? x.at("doc").get<std::string>() : std::string{};
                e.extensions.push_back(std::move(xe));
            }
        }
        m.classes.push_back(std::move(e));
    }

    // `functions` is optional: free (non-member) functions to bind. Each entry
    // gives the (optionally qualified) function name and its declaring header;
    // `doc` is an optional description (free functions carry no in-source
    // annotation, keeping the user's headers untouched).
    if (j.contains("functions")) {
        for (const auto &f : j.at("functions")) {
            FunctionEntry e;
            e.name   = qualify(f.at("name").get<std::string>());
            e.header = header_dir + f.at("header").get<std::string>();
            e.doc    = f.contains("doc") ? f.at("doc").get<std::string>() : std::string{};
            m.functions.push_back(std::move(e));
        }
    }

    // `sequences` is optional: foreign sequence containers, each a qualified
    // template name with one type parameter (see Manifest::sequences).
    if (j.contains("sequences")) {
        for (const auto &s : j.at("sequences")) {
            std::string tpl = s.get<std::string>();
            if (tpl.empty()) {
                throw std::runtime_error("\"sequences\" entries must not be empty");
            }
            m.sequences.push_back(std::move(tpl));
        }
    }

    // `plugins` is optional: extra .cpp sources (e.g. a custom Backend +
    // BackendRegistrar) compiled into the driver. Resolved to absolute paths.
    if (j.contains("plugins")) {
        for (const auto &p : j.at("plugins")) {
            m.plugins.push_back(
                fs::weakly_canonical(base / fs::path(p.get<std::string>())).string());
        }
    }

    // `user_sources` is optional: a list of user .cpp files compiled directly
    // into every generated binding target (use when the bound headers only
    // declare the API and you want the bodies built in rather than linked from a
    // pre-built user_lib). A single string is accepted as a one-element list.
    // Each entry is resolved relative to the manifest (or taken absolute) and may
    // be a shell glob ("src/algorithms/*.cpp"), expanded here at generation time.
    if (j.contains("user_sources")) {
        const auto &us  = j.at("user_sources");
        auto        add = [&](const std::string &p) {
            if (is_glob_pattern(p)) {
                std::vector<fs::path> matches = expand_glob(base, p);
                if (matches.empty()) {
                    std::fprintf(stderr,
                                 "rosetta_gen: warning: \"user_sources\" pattern matched "
                                 "no files: %s\n",
                                 p.c_str());
                }
                for (const auto &mt : matches) {
                    m.user_sources.push_back(mt.string());
                }
            } else {
                m.user_sources.push_back(fs::weakly_canonical(base / fs::path(p)).string());
            }
        };
        if (us.is_array()) {
            for (const auto &e : us) {
                add(e.get<std::string>());
            }
        } else {
            add(us.get<std::string>());
        }
        // Drop duplicates (a file named literally and also matched by a glob, or
        // overlapping globs) — keeping first-seen order — so a target never lists
        // the same source twice (which CMake rejects).
        std::vector<std::string> deduped;
        for (const auto &src : m.user_sources) {
            if (std::find(deduped.begin(), deduped.end(), src) == deduped.end()) {
                deduped.push_back(src);
            }
        }
        m.user_sources = std::move(deduped);
    }

    // `compile_definitions` is optional: preprocessor definitions ("NAME" or
    // "NAME=VALUE") applied to the driver and to every compiled binding target,
    // so they reach the bound headers and the user_sources alike (e.g. a
    // third-party lib's configuration switches: XXX_USE_BUILTIN_DEPS,
    // XXX_WITH_HLBFGS). A single string is accepted as a one-element list.
    if (j.contains("compile_definitions")) {
        const auto &cd  = j.at("compile_definitions");
        auto        add = [&](const std::string &d) {
            if (d.empty()) {
                throw std::runtime_error("\"compile_definitions\" entries must not be empty");
            }
            m.compile_definitions.push_back(d);
        };
        if (cd.is_array()) {
            for (const auto &e : cd) {
                add(e.get<std::string>());
            }
        } else {
            add(cd.get<std::string>());
        }
    }

    // `cpp26_root` is optional: the path to the C++26 / P2996 reflection
    // toolchain root (the clang-p2996 build dir, holding bin/clang++ and lib/).
    // Stored verbatim so a value like "$ENV{HOME}/..." or an absolute path is
    // baked straight into the generated CMakeLists. Only the reflection-driven
    // (thin) targets use it; the stock *-expanded targets ignore it.
    if (j.contains("cpp26_root")) {
        m.cpp26_root = j.at("cpp26_root").get<std::string>();
    }
    // Optional finer-grained overrides; each defaults from cpp26_root if unset.
    //   cpp26_cxx — C++ compiler (name or path)   cpp26_cc — C compiler
    //   cpp26_lib — fork stdlib dir (libc++/libc++abi) for -L / -rpath
    if (j.contains("cpp26_cxx")) {
        m.cpp26_cxx = j.at("cpp26_cxx").get<std::string>();
    }
    if (j.contains("cpp26_cc")) {
        m.cpp26_cc = j.at("cpp26_cc").get<std::string>();
    }
    if (j.contains("cpp26_lib")) {
        m.cpp26_lib = j.at("cpp26_lib").get<std::string>();
    }
    // Optional Qt 6 install prefix for the qt-expanded / qml-expanded backends.
    if (j.contains("qt_dir")) {
        m.qt_dir = j.at("qt_dir").get<std::string>();
    }

    // `build_type` is optional: the default CMAKE_BUILD_TYPE baked into every
    // compiled backend's generated CMakeLists. Case-insensitive on input,
    // stored in CMake's canonical spelling. Emitted inside
    // if(NOT CMAKE_BUILD_TYPE), so -DCMAKE_BUILD_TYPE=... at configure time
    // still wins.
    if (j.contains("build_type")) {
        std::string bt = j.at("build_type").get<std::string>();
        std::string lo = bt;
        std::transform(lo.begin(), lo.end(), lo.begin(),
                       [](unsigned char ch) { return std::tolower(ch); });
        static const std::pair<const char *, const char *> kBuildTypes[] = {
            {"debug", "Debug"},
            {"release", "Release"},
            {"relwithdebinfo", "RelWithDebInfo"},
            {"minsizerel", "MinSizeRel"}};
        for (const auto &[lower, canon] : kBuildTypes) {
            if (lo == lower) {
                m.build_type = canon;
                break;
            }
        }
        if (m.build_type.empty()) {
            throw std::runtime_error("build_type must be \"Debug\", \"Release\", "
                                     "\"RelWithDebInfo\" or \"MinSizeRel\" (got \"" +
                                     bt + "\")");
        }
    }
    // `optimization` is optional: an explicit optimization level applied to
    // every compiled backend after the build type's own flags — so this -O
    // wins over the level the build type implies. The leading "-" may be
    // omitted ("O2" ⇒ "-O2").
    if (j.contains("optimization")) {
        std::string o = j.at("optimization").get<std::string>();
        if (!o.empty() && o[0] != '-') {
            o = "-" + o;
        }
        static const char *kLevels[] = {"-O0", "-O1", "-O2", "-O3",
                                        "-Os", "-Oz", "-Og", "-Ofast"};
        if (std::find_if(std::begin(kLevels), std::end(kLevels),
                         [&](const char *l) { return o == l; }) == std::end(kLevels)) {
            throw std::runtime_error(
                "optimization must be one of -O0, -O1, -O2, -O3, -Os, -Oz, -Og, "
                "-Ofast (got \"" + j.at("optimization").get<std::string>() + "\")");
        }
        m.optimization = o;
    }

    // `user_lib` is optional: an external library the generated bindings link
    // against. Use it when the bound headers only *declare* the API and its
    // bodies are compiled into a separate shared/static library. `name` is the
    // library base name (-l<name>); `dir` is the directory holding it, resolved
    // to an absolute path (relative to the manifest) for -L / rpath.
    if (j.contains("user_lib")) {
        const auto &ul = j.at("user_lib");
        m.user_lib_name = ul.at("name").get<std::string>();
        m.user_lib_dir =
            fs::weakly_canonical(base / fs::path(ul.at("dir").get<std::string>())).string();
        // Optional "link": prefer linking the shared or the static form of the
        // library. "dynamic" is accepted as an alias for "shared". Default shared.
        // (WebAssembly always links static regardless — see the wasm backend.)
        if (ul.contains("link")) {
            std::string link = ul.at("link").get<std::string>();
            if (link == "dynamic") {
                link = "shared";
            }
            if (link != "shared" && link != "static") {
                throw std::runtime_error(
                    "user_lib.link must be \"shared\", \"dynamic\", or \"static\" (got \"" + link +
                    "\")");
            }
            m.user_lib_link = link;
        }
    }

    if (m.generator_name.empty()) {
        throw std::runtime_error(
            "cannot derive generator_name (set it explicitly in the manifest)");
    }
    if (m.targets.empty()) {
        throw std::runtime_error("manifest has no targets");
    }
    if (m.classes.empty()) {
        throw std::runtime_error("manifest has no class entries");
    }

    return m;
}
