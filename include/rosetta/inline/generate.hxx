namespace rosetta {

    namespace gen_detail {

        inline void write_file(const std::filesystem::path &path, std::string_view content) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream(path) << content;
        }

        // write_file + the executable bit, for the shell scripts a backend emits
        // (the wheel builders). Permissions are added, not replaced, so the
        // user's umask still decides the read/write bits.
        inline void write_script(const std::filesystem::path &path, std::string_view content) {
            write_file(path, content);
            std::error_code ec; // best effort: a filesystem without exec bits is not fatal
            std::filesystem::permissions(path,
                                         std::filesystem::perms::owner_exec |
                                             std::filesystem::perms::group_exec |
                                             std::filesystem::perms::others_exec,
                                         std::filesystem::perm_options::add, ec);
        }

        // Substitute {{KEY}} placeholders. Plain string scan — no escapes.
        // (Named `subst`, not `render`, so it isn't shadowed by Backend::render
        // when called unqualified inside a backend's emit().)
        inline std::string
        subst(std::string_view                                                     tmpl,
              std::initializer_list<std::pair<std::string_view, std::string_view>> vars) {
            std::string out{tmpl};
            for (const auto &[k, v] : vars) {
                std::string needle = "{{" + std::string(k) + "}}";
                for (std::string::size_type pos = 0;
                     (pos = out.find(needle, pos)) != std::string::npos; pos += v.size()) {
                    out.replace(pos, needle.size(), v);
                }
            }
            return out;
        }

        // Look up a type-erased annotation by type in a GenClass/GenField's
        // `annotations`. Returns nullptr if absent. Lets a backend read its own
        // annotation kinds without the core ever naming them.
        template <class A> inline const A *find_annotation(const std::vector<std::any> &anns) {
            for (const auto &a : anns) {
                if (const A *p = std::any_cast<A>(&a)) {
                    return p;
                }
            }
            return nullptr;
        }

        // Reflected name of T as a runtime string. A plain class/enum yields its
        // bare identifier ("Point"); a template specialization
        // (Eigen::SparseMatrix<double>, pmp::Matrix<float, 3, 1>) has no
        // identifier, so fall back to its full display spelling — identifier_of
        // would otherwise be a hard constant-evaluation error.
        template <typename T> inline std::string class_name() {
            constexpr const char *n = std::define_static_string(
                std::meta::has_identifier(^^T) ? std::meta::identifier_of(^^T)
                                               : std::meta::display_string_of(^^T));
            return std::string(n);
        }

        // Enclosing-namespace spelling of T as a runtime string: "" for a global
        // type, "space" for space::Vector3, "a::b" for a::b::T. Walks parent
        // scopes via reflection, stopping at the (identifier-less) global
        // namespace. The *-expanded backends emit a `using namespace` for each
        // distinct namespace so they can keep spelling bound types by their
        // unqualified identifier (see using_namespaces_of()).
        template <typename T> inline std::string class_namespace() {
            constexpr const char *n = std::define_static_string([] consteval -> std::string {
                std::vector<std::string_view> parts;
                std::meta::info               scope = std::meta::parent_of(^^T);
                while (std::meta::is_namespace(scope) && std::meta::has_identifier(scope)) {
                    parts.push_back(std::meta::identifier_of(scope));
                    scope = std::meta::parent_of(scope);
                }
                std::string out;
                for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
                    if (!out.empty()) {
                        out += "::";
                    }
                    out += *it;
                }
                return out;
            }());
            return std::string(n);
        }

        // Namespace-qualified spelling of T ("arch::sinv::Data"; the bare
        // identifier for a global type). Distinct from qualified_class_name()
        // below, which also walks enclosing CLASSES but requires an identifier;
        // this one inherits class_name()'s display-string fallback, so template
        // specializations (std::array<double, 9>) stay safe.
        template <typename T> inline std::string ns_qualified_name() {
            const std::string ns = class_namespace<T>();
            return ns.empty() ? class_name<T>() : ns + "::" + class_name<T>();
        }

        // The qualified C++ spelling of a bound class, for emitted code. Always
        // unambiguous — unlike the unqualified `name`, which needs the
        // `using namespace` directives and breaks down as soon as two bound
        // namespaces declare the same identifier (the "expose" rename case).
        inline std::string qualified_of(const GenClass &k) {
            // Prefers GenClass::qualified, which also carries the enclosing
            // *classes* — a nested class has an empty `name_space`, so the
            // namespace-only spelling would emit the bare identifier and fail
            // to compile. Falls back to `name_space::name` for hand-built IR.
            if (!k.qualified.empty()) {
                return k.qualified;
            }
            return k.name_space.empty() ? k.name : k.name_space + "::" + k.name;
        }

        // The host-language-visible name of a bound class: the manifest
        // "expose" override when set, else the reflected identifier. Falls back
        // to `name` so hand-built GenClass values (tests, render callers) keep
        // working without filling `expose`.
        inline std::string exposed_of(const GenClass &k) {
            return k.expose.empty() ? k.name : k.expose;
        }

        // Same pair for an enumeration. Prefers GenEnum::qualified, which also
        // carries the enclosing *classes* — a nested enum has an empty
        // `name_space`, so the namespace-only spelling would emit the bare
        // identifier and fail to compile. Falls back to `name_space::name` for
        // hand-built IR that leaves `qualified` empty.
        inline std::string qualified_of(const GenEnum &e) {
            if (!e.qualified.empty()) {
                return e.qualified;
            }
            return e.name_space.empty() ? e.name : e.name_space + "::" + e.name;
        }

        inline std::string exposed_of(const GenEnum &e) {
            return e.expose.empty() ? e.name : e.expose;
        }

        // Does this IR type name that bound class / enumeration? Compares the
        // QUALIFIED spelling when the IR carries it — `object` alone cannot tell
        // two bound types apart once they share an unqualified identifier (the
        // "expose" rename case) — and falls back to the bare identifier so
        // hand-built IR (tests, render() callers) keeps resolving.
        template <typename K> inline bool names_type(const GenType &t, const K &k) {
            return t.object_qualified.empty() ? (k.name == t.object)
                                              : (qualified_of(k) == t.object_qualified);
        }

        // The host-language name of the class / enumeration an IR type names:
        // its "expose" override when it is bound, else the reflected identifier
        // the IR carries. Every backend that prints a bound type's name in
        // generated host-language text (a TypeScript class, a C# / Java type, a
        // doc cross-reference) should go through this, so a renamed type is
        // named consistently everywhere it appears.
        inline std::string exposed_object_of(const GenType &t, const GenContext &c) {
            for (const auto &k : c.classes) {
                if (names_type(t, k)) {
                    return exposed_of(k);
                }
            }
            for (const auto &e : c.enums) {
                if (names_type(t, e)) {
                    return exposed_of(e);
                }
            }
            return t.object;
        }

        // -------- shared CMake fragment --------

        // Defaults used when the manifest does not set a field. The cache vars
        // below stay overridable at configure time regardless. The compiler /
        // stdlib defaults are CMake expressions deriving from CLANG_P2996_ROOT, so
        // setting only cpp26_root moves all three together.
        constexpr std::string_view DEFAULT_CPP26_ROOT = "$ENV{HOME}/devs/c++/clang-p2996/build";
        constexpr std::string_view DEFAULT_CPP26_CXX  = "${CLANG_P2996_ROOT}/bin/clang++";
        constexpr std::string_view DEFAULT_CPP26_CC   = "${CLANG_P2996_ROOT}/bin/clang";
        constexpr std::string_view DEFAULT_CPP26_LIB  = "${CLANG_P2996_ROOT}/lib";
        // Default Qt 6 prefix for the qt-expanded / qml-expanded CMakeLists.
        constexpr std::string_view DEFAULT_QT_DIR = "$ENV{HOME}/Qt/6.8.3/macos";
        // Default distribution version for the packaging artifacts (the wheel
        // pyproject.toml) when the manifest sets no "version".
        constexpr std::string_view DEFAULT_DIST_VERSION = "0.1.0";

        // {{CPP26_*}} placeholders are substituted by render_meta(), which lists
        // HEADER_BLOCK before them so the placeholders introduced here resolve.
        // The thin backends' link options reference ${ROSETTA_STDLIB}.
        constexpr std::string_view CMAKE_HEADER =
            R"CMK(# Generated by rosetta::generate — do not edit by hand.
cmake_minimum_required(VERSION 3.28)

set(CLANG_P2996_ROOT "{{CPP26_ROOT}}"
    CACHE PATH "C++26 / P2996 reflection toolchain root (clang-p2996 build dir)")
set(ROSETTA_CXX_COMPILER "{{CPP26_CXX}}"
    CACHE FILEPATH "C++26 / P2996 C++ compiler")
set(ROSETTA_C_COMPILER "{{CPP26_CC}}"
    CACHE FILEPATH "C++26 / P2996 C compiler")
set(ROSETTA_STDLIB "{{CPP26_LIB}}"
    CACHE PATH "Directory holding the fork's libc++ / libc++abi (-L and -rpath)")
if(NOT CMAKE_CXX_COMPILER)
    set(CMAKE_C_COMPILER   "${ROSETTA_C_COMPILER}")
    set(CMAKE_CXX_COMPILER "${ROSETTA_CXX_COMPILER}")
endif()
)CMK";

        // Compiler / linker flag fragments shared by every reflection backend, so
        // the flag set lives in one place. Injected as {{REFLECTION_FLAGS}} /
        // {{STDLIB_LINK}} by render_meta() into a target_compile_options /
        // target_link_options call the template still owns (it names the target).
        //   REFLECTION_FLAGS — turn on the p2996 reflection + annotation front-end.
        //   STDLIB_LINK      — link the fork's libc++/libc++abi (-L/-rpath ROSETTA_STDLIB).
        constexpr std::string_view REFLECTION_FLAGS =
            "-freflection -freflection-latest -fexperimental-library -fannotation-attributes";
        constexpr std::string_view STDLIB_LINK =
            "-nostdlib++ -L${ROSETTA_STDLIB} -Wl,-rpath,${ROSETTA_STDLIB} -lc++ -lc++abi";

        // -------- shared render helpers (used by every backend) --------

        // Append `#include "h"` to `out`, skipping empties and any header already
        // present. The dedup primitive every backend's include-collection uses (a
        // class and enum may share a header; backends prepend their own framework
        // includes first). Backends keep a one-line `add` lambda over this.
        inline void append_include(std::string &out, const std::string &h) {
            if (h.empty()) {
                return;
            }
            const std::string line = "#include \"" + h + "\"\n";
            if (out.find(line) == std::string::npos) {
                out += line;
            }
        }

        // One `#include "..."` line per class / enum — shared by every backend.
        // Headers are de-duplicated (a class and enum may share a header).
        // rosetta's annotations are included first so user headers that carry
        // `[[ = rosetta::doc{...} ]]` annotations parse (they don't include
        // rosetta themselves).
        inline std::string using_namespaces_of(const GenContext &c); // defined below

        inline std::string includes_of(const GenContext &c) {
            std::string s   = "#include <rosetta/annotations.h>\n";
            auto        add = [&](const std::string &h) { append_include(s, h); };
            for (const auto &k : c.classes) {
                add(k.header);
            }
            for (const auto &e : c.enums) {
                add(e.header);
            }
            for (const auto &f : c.functions) {
                add(f.header);
            }
            // `using namespace` for namespaced user types, emitted right after the
            // class headers (so T is complete) and *before* everything below that
            // spells T unqualified — the annotation specializations here and the
            // backend's own bind_*<T> / trampoline code. Empty when all bound
            // types are global, so global-namespace projects are byte-identical.
            s += using_namespaces_of(c);
            // Out-of-line annotations: backends whose emitted .cpp re-walks the
            // type at its own compile time (python / node) need the
            // ann_json_source<T> specialization in their TU, or they would see
            // the empty primary and lose the side-car. Re-emit it here, after
            // the class headers (so T is complete). Backends that render purely
            // from GenClass ignore the unused specialization.
            std::string anns;
            for (const auto &k : c.classes) {
                if (k.annotations_json.empty()) {
                    continue;
                }
                std::string bytes;
                char        buf[16];
                for (unsigned char ch : k.annotations_json) {
                    std::snprintf(buf, sizeof buf, "char(0x%02x), ", ch);
                    bytes += buf;
                }
                const std::string kq = qualified_of(k); // unambiguous even when two
                                                        // bound classes share an identifier
                anns += "template <> inline constexpr auto rosetta::detail::ann_storage<" + kq +
                        "> =\n    std::to_array<char>({" + bytes + "'\\0'});\n" +
                        "template <> inline constexpr std::string_view rosetta::ann_json_source<" +
                        kq + "> =\n    std::string_view{rosetta::detail::ann_storage<" +
                        kq + ">.data(), rosetta::detail::ann_storage<" + kq +
                        ">.size() - 1};\n";
            }
            if (!anns.empty()) {
                s += "#include <rosetta/annotate.h>\n" + anns;
            }
            return s;
        }

        // The per-class reference docs, concatenated into the README body.
        inline std::string readme_body_of(const std::vector<GenClass> &classes) {
            std::string s;
            for (const auto &c : classes) {
                s += c.doc + "\n";
            }
            return s;
        }

        // `using namespace X;` lines for every distinct, non-global namespace the
        // bound classes/enums live in, in first-seen order. The *-expanded
        // backends spell bound types by their unqualified identifier; emitting
        // these directives after the includes lets a namespaced user library
        // (e.g. space::Vector3) bind without qualifying every spelling. Empty
        // when all bound types are global (matching the older examples).
        inline std::string using_namespaces_of(const GenContext &c) {
            std::vector<std::string> seen;
            auto                     add = [&](const std::string &ns) {
                if (!ns.empty() && std::find(seen.begin(), seen.end(), ns) == seen.end()) {
                    seen.push_back(ns);
                }
            };
            for (const auto &k : c.classes) {
                add(k.name_space);
            }
            for (const auto &e : c.enums) {
                add(e.name_space);
            }
            std::string s;
            for (const auto &ns : seen) {
                s += "using namespace " + ns + ";\n";
            }
            return s;
        }

        // CMake block linking the external user libraries (manifest "user_lib") into
        // a *native* binding target — one per entry, in the manifest's order, so a
        // library and the dependencies it needs link together. Honors each entry's
        // `link` ("shared" | "static"): it prefers that form but falls back to
        // whichever is actually present, and references the library by full path —
        // so a same-named project target is never linked by mistake, and the
        // static/shared choice is unambiguous (a bare -l<name> lets the linker, not
        // the manifest, decide). Resolution runs at CMake configure time; if neither
        // form exists yet (library not built), it falls back to a name-based link
        // resolved at build time. Returns "" when no user_lib is set.
        //
        // The block links whatever target ${ROSETTA_BINDING_TARGET} names; it
        // defaults to c.lib, so backends whose target IS c.lib just drop in
        // {{USER_LIB_BLOCK}}. Backends with a suffixed target (e.g. <lib>_qt,
        // <lib>_demo) set(ROSETTA_BINDING_TARGET <target>) right before it.
        //
        // WebAssembly does NOT use this (a native shared object cannot enter a wasm
        // module) — the wasm backends link the static archive directly.
        // True when a user source is a C translation unit (vendored zlib / rply /
        // libMeshb…), so the generated CMakeLists must enable_language(C).
        inline bool has_c_user_sources(const GenContext &c) {
            for (const auto &src : c.user_sources) {
                if (src.size() > 2 && src.compare(src.size() - 2, 2, ".c") == 0) {
                    return true;
                }
            }
            return false;
        }

        // set_source_files_properties block pinning the user sources to the
        // manifest's "cxx_standard". The generated binding TU keeps the
        // template's C++20 (the node runtime, e.g., is not C++17-clean); the
        // per-source -std lands after the target's standard flag on the
        // command line, so it wins for those files only. Empty when the
        // manifest is silent, asks for the stock 20, or there are no user
        // sources. C translation units (.c) are skipped — -std=c++NN is not
        // theirs.
        inline std::string user_sources_std_block(const GenContext &c) {
            if (c.cxx_standard.empty() || c.cxx_standard == "20" || c.user_sources.empty()) {
                return {};
            }
            std::string body;
            for (const auto &src : c.user_sources) {
                if (src.size() > 2 && src.compare(src.size() - 2, 2, ".c") == 0) {
                    continue;
                }
                body += "    " + src + "\n";
            }
            if (body.empty()) {
                return {};
            }
            std::string s;
            s += "# C++ standard of the user sources (manifest \"cxx_standard\") — the\n";
            s += "# generated binding TU stays C++20; this per-source -std wins over it.\n";
            s += "set_source_files_properties(\n" + body;
            s += "    PROPERTIES COMPILE_OPTIONS \"-std=c++" + c.cxx_standard + "\")";
            return s;
        }

        inline std::string user_lib_block(const GenContext &c) {
            if (c.user_libs.empty() && c.user_sources.empty() &&
                c.compile_definitions.empty() && c.link_options.empty()) {
                return {};
            }
            std::string s;
            // The user-sources, compile-definitions and user-lib parts all act on
            // ${ROSETTA_BINDING_TARGET}; establish it once (a suffixed-target
            // backend — <lib>_qt, <lib>_demo — sets it before this block, so only
            // default when unset).
            s += "\nif(NOT DEFINED ROSETTA_BINDING_TARGET)\n";
            s += "    set(ROSETTA_BINDING_TARGET " + c.lib + ")\n";
            s += "endif()\n";

            // User sources (manifest "user_sources"): the bound headers declare the
            // API; compile these translation units holding the bodies into the binding.
            if (!c.user_sources.empty()) {
                if (has_c_user_sources(c)) {
                    s += "# Some user sources are C (vendored zlib / rply / libMeshb…).\n";
                    s += "enable_language(C)\n";
                }
                s += "# User sources (manifest \"user_sources\") compiled into the binding.\n";
                s += "target_sources(${ROSETTA_BINDING_TARGET} PRIVATE\n";
                for (const auto &src : c.user_sources) {
                    s += "    " + src + "\n";
                }
                s += ")\n";
                const std::string src_std = user_sources_std_block(c);
                if (!src_std.empty()) {
                    s += src_std + "\n";
                }
            }

            // Preprocessor definitions (manifest "compile_definitions") applied to
            // the binding target — they reach the bound headers and user_sources
            // alike (e.g. GEOGRAM_USE_BUILTIN_DEPS, GEOGRAM_WITH_HLBFGS).
            if (!c.compile_definitions.empty()) {
                s += "# Definitions (manifest \"compile_definitions\") for the binding.\n";
                s += "target_compile_definitions(${ROSETTA_BINDING_TARGET} PRIVATE\n";
                for (const auto &def : c.compile_definitions) {
                    s += "    " + def + "\n";
                }
                s += ")\n";
            }

            // Extra linker flags for this target (manifest target "link_options") —
            // e.g. "-lnodefs.js" on a wasm target whose bound code mounts NODEFS.
            if (!c.link_options.empty()) {
                s += "# Linker flags (manifest target \"link_options\") for the binding.\n";
                s += "target_link_options(${ROSETTA_BINDING_TARGET} PRIVATE\n";
                for (const auto &opt : c.link_options) {
                    s += "    \"" + opt + "\"\n";
                }
                s += ")\n";
            }

            if (c.user_libs.empty()) {
                return s;
            }
            s += "\n# External user libraries (manifest \"user_lib\"): the bound headers only\n";
            s += "# declare the API; link the separately-compiled libraries holding the bodies\n";
            s += "# (yours, plus the ones it depends on — listed in link order). `link`\n";
            s += "# (\"shared\" | \"static\") picks the preferred form; we fall back to whichever\n";
            s += "# is present and reference it by full path.\n";
            s += "set(_rosetta_rpath \"\")\n";
            for (std::size_t i = 0; i < c.user_libs.size(); ++i) {
                const UserLib    &u  = c.user_libs[i];
                const std::string n  = std::to_string(i);
                const std::string lk = u.link.empty() ? "shared" : u.link;
                s += "set(ROSETTA_USER_LIB_" + n + " \"" + u.name + "\")\n";
                s += "set(ROSETTA_USER_LIB_DIR_" + n + " \"" + u.dir + "\")\n";
                s += "set(ROSETTA_USER_LIB_LINK_" + n + " \"" + lk + "\")\n";
                s += "set(_rosetta_shared \"${ROSETTA_USER_LIB_DIR_" + n +
                     "}/${CMAKE_SHARED_LIBRARY_PREFIX}"
                     "${ROSETTA_USER_LIB_" + n + "}${CMAKE_SHARED_LIBRARY_SUFFIX}\")\n";
                s += "set(_rosetta_static \"${ROSETTA_USER_LIB_DIR_" + n +
                     "}/${CMAKE_STATIC_LIBRARY_PREFIX}"
                     "${ROSETTA_USER_LIB_" + n + "}${CMAKE_STATIC_LIBRARY_SUFFIX}\")\n";
                s += "if(ROSETTA_USER_LIB_LINK_" + n + " STREQUAL \"static\")\n";
                s += "    set(_rosetta_order \"${_rosetta_static}\" \"${_rosetta_shared}\")\n";
                s += "else()\n";
                s += "    set(_rosetta_order \"${_rosetta_shared}\" \"${_rosetta_static}\")\n";
                s += "endif()\n";
                s += "set(_rosetta_lib \"\")\n";
                s += "foreach(_cand IN LISTS _rosetta_order)\n";
                s += "    if(EXISTS \"${_cand}\")\n";
                s += "        set(_rosetta_lib \"${_cand}\")\n";
                s += "        break()\n";
                s += "    endif()\n";
                s += "endforeach()\n";
                s += "if(NOT _rosetta_lib)\n";
                s += "    set(_rosetta_lib \"-l${ROSETTA_USER_LIB_" + n +
                     "}\") # not built yet; resolved at build time\n";
                s += "endif()\n";
                s += "message(STATUS \"rosetta: ${ROSETTA_BINDING_TARGET} links user library "
                     "${_rosetta_lib} (requested ${ROSETTA_USER_LIB_LINK_" + n + "})\")\n";
                s += "target_link_directories(${ROSETTA_BINDING_TARGET} PRIVATE "
                     "${ROSETTA_USER_LIB_DIR_" + n + "})\n";
                s += "target_link_libraries(${ROSETTA_BINDING_TARGET} PRIVATE \"${_rosetta_lib}\")\n";
                s += "list(APPEND _rosetta_rpath \"${ROSETTA_USER_LIB_DIR_" + n + "}\")\n";
            }
            // One rpath list covering every library directory (duplicates — several
            // libs from one dir — collapse).
            s += "list(REMOVE_DUPLICATES _rosetta_rpath)\n";
            s += "set_target_properties(${ROSETTA_BINDING_TARGET} PROPERTIES\n";
            s += "    BUILD_RPATH \"${_rosetta_rpath}\"\n";
            s += "    INSTALL_RPATH \"${_rosetta_rpath}\")\n";
            return s;
        }

        // CMake / package vars — backend source uses {{INCLUDES}}/{{BINDINGS}}
        // which are passed separately by render_source().
        inline std::string render_meta(std::string_view tmpl, const GenContext &c) {
            // HEADER_BLOCK must precede the CPP26_* keys: substituting CMAKE_HEADER
            // injects their placeholders, which the later passes resolve.
            const std::string root =
                c.cpp26_root.empty() ? std::string(DEFAULT_CPP26_ROOT) : c.cpp26_root;
            const std::string cxx =
                c.cpp26_cxx.empty() ? std::string(DEFAULT_CPP26_CXX) : c.cpp26_cxx;
            const std::string cc =
                c.cpp26_cc.empty() ? std::string(DEFAULT_CPP26_CC) : c.cpp26_cc;
            const std::string lib =
                c.cpp26_lib.empty() ? std::string(DEFAULT_CPP26_LIB) : c.cpp26_lib;
            const std::string qt =
                c.qt_dir.empty() ? std::string(DEFAULT_QT_DIR) : c.qt_dir;
            const std::string version =
                c.version.empty() ? std::string(DEFAULT_DIST_VERSION) : c.version;
            // {{USER_SOURCES}} — user .cpp files appended to a backend's source list
            // (used by the wasm templates, whose target name is fixed and so don't go
            // through {{USER_LIB_BLOCK}}/${ROSETTA_BINDING_TARGET}). Each on its own
            // indented line so it slots straight into an add_executable(...) call.
            std::string user_sources;
            for (const auto &src : c.user_sources) {
                user_sources += "\n    " + src;
            }
            // {{USER_LIB_WASM_BLOCK}} — the user-library link block for the wasm
            // templates, whose fixed-name target doesn't go through
            // {{USER_LIB_BLOCK}}/${ROSETTA_BINDING_TARGET}. WebAssembly cannot link
            // a native .dylib/.so and has no rpath, so each entry's `link` choice is
            // overridden here: the library is ALWAYS the wasm static archive
            // (lib<name>.a) compiled with the SAME emsdk. Referenced by full path so
            // a same-named project target (the wasm module is itself named after the
            // manifest target, e.g. `space`) is never linked by mistake. Empty when
            // the manifest declares no user_lib.
            std::string user_lib_wasm_block;
            for (std::size_t i = 0; i < c.user_libs.size(); ++i) {
                const UserLib    &u = c.user_libs[i];
                const std::string n = std::to_string(i);
                user_lib_wasm_block +=
                    "set(ROSETTA_USER_LIB_" + n + " \"" + u.name + "\")\n"
                    "set(ROSETTA_USER_LIB_DIR_" + n + " \"" + u.dir + "\")\n"
                    "target_link_directories(" + c.lib + " PRIVATE ${ROSETTA_USER_LIB_DIR_" + n + "})\n"
                    "set(_rosetta_static \"${ROSETTA_USER_LIB_DIR_" + n +
                    "}/${CMAKE_STATIC_LIBRARY_PREFIX}${ROSETTA_USER_LIB_" + n +
                    "}${CMAKE_STATIC_LIBRARY_SUFFIX}\")\n"
                    "if(EXISTS \"${_rosetta_static}\")\n"
                    "    target_link_libraries(" + c.lib + " PRIVATE \"${_rosetta_static}\")\n"
                    "else()\n"
                    "    # Archive not built yet at configure time — link by name (resolved\n"
                    "    # from the link directory above; emcc's linker takes lib<name>.a).\n"
                    "    target_link_libraries(" + c.lib + " PRIVATE \"-l${ROSETTA_USER_LIB_" + n + "}\")\n"
                    "endif()";
                if (i + 1 < c.user_libs.size()) {
                    user_lib_wasm_block += "\n";
                }
            }
            if (!user_lib_wasm_block.empty()) {
                user_lib_wasm_block =
                    "\n\n# External user libraries (manifest \"user_lib\") — always the wasm\n"
                    "# static archives, in link order.\n" + user_lib_wasm_block;
            }
            // {{USER_ENABLE_C}} — "enable_language(C)" line for the wasm templates
            // (placed right before their add_executable; the native templates get
            // it via {{USER_LIB_BLOCK}} instead). Empty when no .c user source.
            const std::string user_enable_c =
                has_c_user_sources(c) ? "enable_language(C)\n\n" : "";
            // {{USER_DEFS_BLOCK}} — target_compile_definitions (and per-target
            // target_link_options) for the wasm templates, whose fixed-name
            // target doesn't go through {{USER_LIB_BLOCK}}/
            // ${ROSETTA_BINDING_TARGET}. Empty when no defs and no link flags.
            std::string user_defs_block;
            if (!c.compile_definitions.empty()) {
                user_defs_block +=
                    "\n\n# Definitions (manifest \"compile_definitions\") for the binding.\n"
                    "target_compile_definitions(" + c.lib + " PRIVATE";
                for (const auto &def : c.compile_definitions) {
                    user_defs_block += "\n    " + def;
                }
                user_defs_block += ")";
            }
            if (!c.link_options.empty()) {
                user_defs_block +=
                    "\n\n# Linker flags (manifest target \"link_options\") for the binding.\n"
                    "target_link_options(" + c.lib + " PRIVATE";
                for (const auto &opt : c.link_options) {
                    user_defs_block += "\n    \"" + opt + "\"";
                }
                user_defs_block += ")";
            }
            // The wasm templates splice their user sources straight into
            // add_executable (no {{USER_LIB_BLOCK}}), so the per-source C++
            // standard block rides along here instead.
            {
                const std::string src_std = user_sources_std_block(c);
                if (!src_std.empty()) {
                    user_defs_block += "\n\n" + src_std;
                }
            }
            // {{BUILD_CONFIG}} — build type / optimization level (manifest
            // "build_type" / "optimization"), spliced right after each
            // template's set(CMAKE_CXX_STANDARD_REQUIRED ON) line so it
            // precedes every target. The build type is only a default (kept
            // inside if(NOT CMAKE_BUILD_TYPE), so -DCMAKE_BUILD_TYPE=... at
            // configure time still wins); the optimization flag goes through
            // add_compile_options / add_link_options, which land AFTER the
            // build type's per-config flags on the command line — so this -O
            // overrides the build type's own level (and reaches the wasm
            // link, where emscripten's -O matters). Empty when the manifest
            // sets neither, leaving the output unchanged.
            std::string build_config;
            if (!c.build_type.empty()) {
                build_config +=
                    "\n\n# Default build type (manifest \"build_type\") — override with\n"
                    "# -DCMAKE_BUILD_TYPE=... at configure time.\n"
                    "if(NOT CMAKE_BUILD_TYPE)\n"
                    "    set(CMAKE_BUILD_TYPE " + c.build_type + ")\n"
                    "endif()";
            }
            if (!c.optimization.empty()) {
                build_config +=
                    "\n\n# Optimization level (manifest \"optimization\") — added after the build\n"
                    "# type's own per-config flags, so this -O is the one that wins.\n"
                    "add_compile_options(" + c.optimization + ")\n"
                    "add_link_options(" + c.optimization + ")";
            }
            // {{OUT_DIR_BLOCK}} — copy the built artifact to the manifest's
            // per-target "out_dir" after every build, so the loadable module
            // lands where the project actually wants it (a Python package dir,
            // a web app's assets) instead of only next to its generated
            // sources. copy_if_different, so an unchanged build touches
            // nothing downstream; the directory is created if missing.
            // {{OUT_DIR_BLOCK_WASM}} is the same for the wasm templates, whose
            // artifact is a PAIR (.js loader + .wasm) and whose target name is
            // fixed — the sibling .wasm is not $<TARGET_FILE> and has to be
            // named on its own.
            std::string out_dir_block, out_dir_block_wasm;
            if (!c.artifact_dir.empty()) {
                const std::string d   = "\"" + c.artifact_dir + "\"";
                const std::string mk  = "    COMMAND ${CMAKE_COMMAND} -E make_directory " + d;
                const std::string hdr = "\n\n# Artifact output directory (manifest \"out_dir\").\n"
                                        "add_custom_command(TARGET " +
                                        c.lib + " POST_BUILD\n" + mk + "\n";
                out_dir_block = hdr +
                                "    COMMAND ${CMAKE_COMMAND} -E copy_if_different\n"
                                "        $<TARGET_FILE:" +
                                c.lib + "> " + d + ")";
                out_dir_block_wasm = hdr +
                                     "    COMMAND ${CMAKE_COMMAND} -E copy_if_different\n"
                                     "        $<TARGET_FILE:" +
                                     c.lib + "> " + d +
                                     "\n"
                                     "    COMMAND ${CMAKE_COMMAND} -E copy_if_different\n"
                                     "        $<TARGET_FILE_DIR:" +
                                     c.lib + ">/" + c.lib + ".wasm " + d + ")";
            }
            // Runtime pins (manifest "python" / "requires_python" /
            // "napi_version" / "node_engine"), each falling back to what the
            // templates hardcoded before they were configurable.
            //
            // {{PYTHON_CMD}} is the interpreter the emitted CMake PROBES: a
            // path goes in as written, a bare "3.11" becomes python3.11, and
            // the probe itself is kept either way — asking the interpreter for
            // its own sys.executable is what turns a PATH name into the
            // absolute path find_package needs.
            std::string python_cmd = "python3";
            if (!c.python.empty()) {
                const bool bare_version =
                    c.python.find_first_not_of("0123456789.") == std::string::npos;
                python_cmd = bare_version ? "python" + c.python : c.python;
            }
            // {{PYTHON_MIN}} — the find_package(Python …) floor, taken from the
            // SAME field as pyproject's requires-python so the two cannot drift:
            // ">=3.10" ⇒ 3.10. The version is the first digit run onward, minus
            // any trailing clause (">=3.10,<4" ⇒ 3.10).
            std::string python_min = "3.8";
            if (!c.requires_python.empty()) {
                const auto b = c.requires_python.find_first_of("0123456789");
                if (b != std::string::npos) {
                    const auto e = c.requires_python.find_first_not_of("0123456789.", b);
                    python_min   = c.requires_python.substr(b, e == std::string::npos ? e : e - b);
                }
            }
            const std::string requires_python =
                c.requires_python.empty() ? ">=3.8" : c.requires_python;
            const std::string napi_version = c.napi_version.empty() ? "8" : c.napi_version;
            // {{NODE_ENGINES}} — a package.json "engines" entry, or nothing.
            // Emitted with its leading comma so the surrounding JSON stays
            // valid when it is absent.
            const std::string node_engines =
                c.node_engine.empty()
                    ? std::string{}
                    : ",\n  \"engines\": {\n    \"node\": \"" + c.node_engine + "\"\n  }";
            return subst(tmpl, {{"LIB", c.lib},
                                {"PYTHON_CMD", python_cmd},
                                {"PYTHON_MIN", python_min},
                                {"REQUIRES_PYTHON", requires_python},
                                {"NAPI_VERSION", napi_version},
                                {"NODE_ENGINES", node_engines},
                                {"OUT_DIR_BLOCK", out_dir_block},
                                {"OUT_DIR_BLOCK_WASM", out_dir_block_wasm},
                                {"HEADER_BLOCK", CMAKE_HEADER},
                                {"BUILD_CONFIG", build_config},
                                {"CPP26_ROOT", root},
                                {"CPP26_CXX", cxx},
                                {"CPP26_CC", cc},
                                {"CPP26_LIB", lib},
                                {"QT_DIR", qt},
                                {"VERSION", version},
                                {"USER_INCLUDE", c.user_include},
                                {"ROSETTA_INCLUDE", c.rosetta_include},
                                {"USER_LIB_BLOCK", user_lib_block(c)},
                                {"USER_LIB_WASM_BLOCK", user_lib_wasm_block},
                                {"USER_SOURCES", user_sources},
                                {"USER_ENABLE_C", user_enable_c},
                                {"USER_DEFS_BLOCK", user_defs_block},
                                {"REFLECTION_FLAGS", std::string(REFLECTION_FLAGS)},
                                {"STDLIB_LINK", std::string(STDLIB_LINK)}});
        }

        inline std::string render_source(std::string_view tmpl, const GenContext &c,
                                         std::string_view binds) {
            // includes_of() already appends the `using namespace` directives a
            // namespaced user library needs (the reflection-based C++ TUs spell
            // bound types by their unqualified identifier).
            return subst(tmpl, {{"LIB", c.lib}, {"INCLUDES", includes_of(c)}, {"BINDINGS", binds}});
        }

        // Can a value of this type cross a JSON boundary (REST / OpenAPI)?
        // Scalars, bools, strings, enums (as their underlying int), and vectors
        // of those qualify; user object types and std::function do not. Shared
        // by the REST and OpenAPI backends so they describe the same surface.
        inline bool jsonable_type(const GenType &t) {
            if (t.kind == "number" || t.kind == "boolean" || t.kind == "string" ||
                t.kind == "enum") {
                return true;
            }
            if (t.kind == "vector") {
                return !t.element.empty() && jsonable_type(t.element.front());
            }
            return false; // object / void / unknown
        }

        // A method is exposable over JSON only if its return and every parameter
        // type can cross the boundary.
        inline bool jsonable_method(const GenMethod &m) {
            if (!(m.ret.kind == "void" || jsonable_type(m.ret))) {
                return false;
            }
            for (const auto &p : m.params) {
                if (!jsonable_type(p.type)) {
                    return false;
                }
            }
            return true;
        }

        // A free function is exposable over JSON under the same rule.
        inline bool jsonable_function(const GenFunction &f) {
            if (!(f.ret.kind == "void" || jsonable_type(f.ret))) {
                return false;
            }
            for (const auto &p : f.params) {
                if (!jsonable_type(p.type)) {
                    return false;
                }
            }
            return true;
        }

        // `build` (optional) is a ready-to-append markdown section ("## Build"
        // …) describing how to compile the emitted sources; each backend keeps
        // it next to its CMake template so the two can't drift apart.
        inline std::string readme(std::string_view backend, const GenContext &c,
                                  std::string_view build = {}) {
            std::string s = "# `" + c.lib + "` (";
            s += backend;
            s += ")\n\nAuto-generated bindings.\n\n";
            if (!build.empty()) {
                s += build;
                if (build.back() != '\n') {
                    s += '\n';
                }
                s += '\n';
            }
            s += readme_body_of(c.classes);
            for (const auto &e : c.enums) {
                s += e.doc + "\n";
            }
            if (!c.functions.empty()) {
                s += "# Functions\n\n";
                for (const auto &f : c.functions) {
                    s += "- `" + f.name + "`";
                    if (!f.doc.empty()) {
                        s += " — " + f.doc;
                    }
                    s += "\n";
                }
                s += "\n";
            }
            return s;
        }

        // -------- reflection IR (for pure-data backends) --------

        template <typename T> struct is_vec : std::false_type {};
        template <typename U, typename A> struct is_vec<std::vector<U, A>> : std::true_type {};

        template <typename T> struct is_shared : std::false_type {};
        template <typename U> struct is_shared<std::shared_ptr<U>> : std::true_type {};

        template <typename T> struct is_func : std::false_type {};
        template <typename R, typename... A>
        struct is_func<std::function<R(A...)>> : std::true_type {};

        // Prettify a canonical type spelling for human docs: display_string_of
        // yields e.g. basic_string<char, …>; show std::string instead.
        inline std::string prettify(std::string s) {
            struct Rule {
                const char *from;
                const char *to;
            };
            static constexpr Rule rules[] = {
                {"basic_string<char, char_traits<char>, allocator<char>>", "std::string"},
                {"basic_string_view<char, char_traits<char>>", "std::string_view"},
            };
            for (const auto &r : rules) {
                std::string::size_type pos  = 0;
                const std::string      from = r.from;
                const std::string      to   = r.to;
                while ((pos = s.find(from, pos)) != std::string::npos) {
                    s.replace(pos, from.size(), to);
                    pos += to.size();
                }
            }
            return s;
        }

        // Forward decl: decompose a std::function<R(A...)> into its return +
        // parameter GenTypes (defined after type_descriptor, which it calls).
        template <typename F> inline void fill_callback_sig(GenType &g);

        // --- Foreign-sequence spelling (rosetta::is_sequence) -----------------
        // A qualified, compilable spelling for the types a sequence adapter has
        // to name in emitted code. display_string_of prints template names and
        // user namespaces unqualified ("vector<double>" for GEO::vector<double>),
        // so the spelling is COMPOSED: enclosing namespaces + the template's own
        // identifier + the (recursively spelled) element. Only the kinds the
        // marshalling gates let through need to round-trip here.
        template <typename E> inline std::string marshal_spelling();

        template <typename U> inline std::string sequence_spelling() {
            // An exact spelling stated by the registration wins: a
            // specialization carrying more than its element (Eigen::VectorXd is
            // Matrix<double, -1, 1>) would compose to the uncompilable
            // `Eigen::Matrix<double>`. See rosetta::sequence_cpp_name.
            if constexpr (requires { std::string(rosetta::sequence_cpp_name<U>::value); }) {
                return std::string(rosetta::sequence_cpp_name<U>::value);
            } else {
                // class_namespace walks the specialization's parents — the same
                // chain encloses the template itself.
                const std::string     ns = class_namespace<U>();
                constexpr const char *id = std::define_static_string(std::meta::identifier_of(
                    std::meta::template_of(std::meta::dealias(^^U))));
                const std::string     head = ns.empty() ? std::string(id) : ns + "::" + id;
                return head + "<" + marshal_spelling<typename U::value_type>() + ">";
            }
        }

        // The same composition for a registered 2-D matrix (rosetta/matrix.h),
        // with the same stated-spelling escape hatch: Eigen::MatrixXd is
        // Matrix<double, -1, -1>, which composes to an uncompilable
        // `Eigen::Matrix<double>`.
        template <typename U> inline std::string matrix_spelling() {
            if constexpr (requires { std::string(rosetta::matrix_cpp_name<U>::value); }) {
                return std::string(rosetta::matrix_cpp_name<U>::value);
            } else {
                const std::string     ns = class_namespace<U>();
                constexpr const char *id = std::define_static_string(std::meta::identifier_of(
                    std::meta::template_of(std::meta::dealias(^^U))));
                const std::string     head = ns.empty() ? std::string(id) : ns + "::" + id;
                return head + "<" + marshal_spelling<typename U::value_type>() + ">";
            }
        }

        template <typename E> inline std::string marshal_spelling() {
            using V = std::remove_cvref_t<E>;
            if constexpr (std::is_same_v<V, std::string>) {
                return "std::string";
            } else if constexpr (std::is_same_v<V, bool>) {
                return "bool";
            } else if constexpr (std::is_arithmetic_v<V>) {
                constexpr const char *sp = std::define_static_string(
                    std::meta::display_string_of(std::meta::dealias(^^V)));
                return sp; // "double", "unsigned int", ...
            } else if constexpr (is_vec<V>::value) {
                return "std::vector<" + marshal_spelling<typename V::value_type>() + ">";
            } else if constexpr (rosetta::is_sequence<V>::value) {
                return sequence_spelling<V>();
            } else if constexpr (std::is_enum_v<V> || std::is_class_v<V>) {
                const std::string ns = class_namespace<V>();
                return ns.empty() ? class_name<V>() : ns + "::" + class_name<V>();
            } else {
                constexpr const char *sp = std::define_static_string(
                    std::meta::display_string_of(std::meta::dealias(^^V)));
                return sp;
            }
        }

        // Map a C++ type to the language-neutral GenType descriptor.
        template <typename T> inline GenType type_descriptor() {
            using U = std::remove_cvref_t<T>;
            GenType g;
            // dealias so we print the underlying type ("std::string", "int"),
            // not the local alias name "U".
            constexpr const char *sp =
                std::define_static_string(std::meta::display_string_of(std::meta::dealias(^^U)));
            g.spelling = prettify(sp);
            if constexpr (std::is_void_v<U>) {
                g.kind = "void";
            } else if constexpr (std::is_same_v<U, bool>) {
                g.kind = "boolean";
            } else if constexpr (std::is_same_v<U, std::string>) {
                g.kind = "string";
            } else if constexpr (std::is_arithmetic_v<U>) {
                g.kind    = "number";
                g.integer = std::is_integral_v<U>;
            } else if constexpr (is_func<U>::value) {
                g.kind        = "unknown";
                g.is_callback = true;
                fill_callback_sig<U>(g);
            } else if constexpr (is_vec<U>::value) {
                g.kind = "vector";
                g.element.push_back(type_descriptor<typename U::value_type>());
            } else if constexpr (rosetta::is_sequence<U>::value) {
                // Trait-registered foreign container: `kind` stays "unknown"
                // (backends that don't opt in keep skipping it — the
                // is_pointer / is_callback pattern); an opted-in backend
                // marshals it through std::vector<element> using seq_cpp to
                // construct the container (see rosetta/sequence.h).
                static_assert(requires(U s, std::size_t n) {
                                  typename U::value_type;
                                  s.resize(n);
                                  { s.size() } -> std::convertible_to<std::size_t>;
                                  s.begin();
                                  s.end();
                              } && std::is_default_constructible_v<U>,
                              "rosetta::is_sequence<T>: T must be default-constructible with "
                              "value_type, size(), resize(n) and begin()/end()");
                g.is_sequence = true;
                g.element.push_back(type_descriptor<typename U::value_type>());
                g.seq_cpp = sequence_spelling<U>();
                // A registered container may ALSO belong to an opted-in foreign
                // library — Eigen::VectorXd registered under "sequences" while
                // "interop": ["eigen"] is on, which is how the caster-less
                // backends (node / wasm / lua) get a flat array out of a type
                // pybind and nanobind would rather hand over as numpy. Record
                // BOTH marks and let each backend pick: a backend with the
                // caster drops is_sequence (interop_wins) and binds the type as
                // spelled, the others take the adapter. object /
                // object_qualified are what the former needs to spell it.
                if constexpr (std::is_class_v<U>) {
                    g.interop = interop_detail::owner_of<U>(class_namespace<U>());
                    if (!g.interop.empty()) {
                        g.object             = class_name<U>();
                        g.object_qualified   = ns_qualified_name<U>();
                        g.copy_constructible = std::is_copy_constructible_v<U>;
                        g.copy_assignable    = std::is_copy_assignable_v<U>;
                    }
                }
            } else if constexpr (rosetta::is_matrix<U>::value) {
                // Trait-registered foreign matrix: is_sequence one dimension
                // up. `kind` stays "unknown" for the same reason (a backend
                // that didn't opt in keeps skipping it); an opted-in backend
                // marshals it through std::vector<std::vector<element>> using
                // mat_cpp to construct the matrix (see rosetta/matrix.h).
                static_assert(requires(U m, std::size_t r, std::size_t c) {
                                  typename U::value_type;
                                  m.resize(r, c);
                                  { m.rows() } -> std::convertible_to<std::size_t>;
                                  { m.cols() } -> std::convertible_to<std::size_t>;
                                  m(r, c);
                              } && std::is_default_constructible_v<U>,
                              "rosetta::is_matrix<T>: T must be default-constructible with "
                              "value_type, rows(), cols(), resize(r, c) and operator()(i, j)");
                g.is_matrix = true;
                g.element.push_back(type_descriptor<typename U::value_type>());
                g.mat_cpp = matrix_spelling<U>();
                // Dual marking, exactly as for a registered sequence: a matrix
                // the interop opt-in also owns keeps its numpy binding on the
                // backends that have the caster (see interop_wins).
                if constexpr (std::is_class_v<U>) {
                    g.interop = interop_detail::owner_of<U>(class_namespace<U>());
                    if (!g.interop.empty()) {
                        g.object             = class_name<U>();
                        g.object_qualified   = ns_qualified_name<U>();
                        g.copy_constructible = std::is_copy_constructible_v<U>;
                        g.copy_assignable    = std::is_copy_assignable_v<U>;
                    }
                }
            } else if constexpr (std::is_enum_v<U>) {
                g.kind             = "enum";
                g.object           = class_name<U>();
                g.object_qualified = ns_qualified_name<U>();
                template for (constexpr auto e : std::define_static_array(
                                  std::meta::enumerators_of(std::meta::dealias(^^U)))) {
                    constexpr const char *en =
                        std::define_static_string(std::meta::identifier_of(e));
                    g.enumerators.push_back(GenEnumerator{en, static_cast<long long>([:e:])});
                }
            } else if constexpr (std::is_pointer_v<U> &&
                                 std::is_class_v<std::remove_cv_t<std::remove_pointer_t<U>>>) {
                // Raw pointer to a class: keep kind "unknown" (backends that can't
                // marshal a pointer keep skipping it), but record the pointee so an
                // opt-in backend can bind it (e.g. embind allow_raw_pointers).
                g.is_pointer       = true;
                g.object           = class_name<std::remove_cv_t<std::remove_pointer_t<U>>>();
                g.object_qualified = ns_qualified_name<std::remove_cv_t<std::remove_pointer_t<U>>>();
            } else if constexpr (is_shared<U>::value) {
                // std::shared_ptr<T>: described exactly as the class branch
                // below would describe it (kind "object", `object` still
                // "shared_ptr"), so no backend's marshalling gate changes —
                // only the flag and the pointee are added. pybind11 needs both
                // to declare the holder on T; see GenType::is_shared_ptr.
                g.kind             = "object";
                g.object           = class_name<U>();
                g.object_qualified = ns_qualified_name<U>();
                g.copy_constructible = std::is_copy_constructible_v<U>;
                g.copy_assignable    = std::is_copy_assignable_v<U>;
                g.is_shared_ptr      = true;
                g.element.push_back(type_descriptor<typename U::element_type>());
            } else if constexpr (std::is_class_v<U>) {
                g.object           = class_name<U>();
                g.object_qualified = ns_qualified_name<U>();
                // A type owned by an opted-in foreign library (rosetta/interop.h)
                // keeps kind "unknown" — the is_pointer / is_sequence pattern —
                // so a backend with no caster for it skips the member instead of
                // emitting a binding that throws at call time. Recognition is by
                // enclosing namespace, so every spelling the library owns
                // (VectorXd, MatrixXd, Map, Ref, …) is covered by one opt-in.
                g.interop = interop_detail::owner_of<U>(class_namespace<U>());
                g.kind    = g.interop.empty() ? "object" : "unknown";
                // Copyability, so emitters can skip/downgrade what would not
                // compile (see GenType). Guarded on completeness: traits on an
                // incomplete type are ill-formed, and an incomplete type can't
                // be copied anywhere either.
                if constexpr (requires { sizeof(U); }) {
                    g.copy_constructible = std::is_copy_constructible_v<U>;
                    g.copy_assignable    = std::is_copy_assignable_v<U>;
                } else {
                    g.copy_constructible = false;
                    g.copy_assignable    = false;
                }
            } else {
                g.kind = "unknown";
            }
            return g;
        }

        // Decompose std::function<R(A...)>: callback_sig[0] = return type (kind
        // "void" when R is void), followed by one entry per parameter. Types are
        // cvref-stripped like every other GenType so a backend can test each with
        // its normal marshalability predicate.
        template <typename R, typename... A>
        inline void fill_callback_sig_impl(GenType &g, std::type_identity<std::function<R(A...)>>) {
            g.callback_sig.push_back(type_descriptor<std::remove_cvref_t<R>>());
            (g.callback_sig.push_back(type_descriptor<std::remove_cvref_t<A>>()), ...);
        }
        template <typename F> inline void fill_callback_sig(GenType &g) {
            fill_callback_sig_impl(g, std::type_identity<F>{});
        }

        template <std::meta::info Fn, std::size_t... Is>
        inline std::vector<GenParam> params_impl(std::index_sequence<Is...>) {
            constexpr auto        ps = std::define_static_array(std::meta::parameters_of(Fn));
            std::vector<GenParam> out;
            (out.push_back(GenParam{
                 "arg" + std::to_string(Is),
                 type_descriptor<std::remove_cvref_t<typename[:std::meta::type_of(ps[Is]):]>>(),
                 std::is_lvalue_reference_v<typename[:std::meta::type_of(ps[Is]):]>,
                 std::is_lvalue_reference_v<typename[:std::meta::type_of(ps[Is]):]> &&
                     !std::is_const_v<std::remove_reference_t<
                         typename[:std::meta::type_of(ps[Is]):]>>}),
             ...);
            return out;
        }

        template <std::meta::info Fn> inline std::vector<GenParam> params_of() {
            constexpr auto n = std::define_static_array(std::meta::parameters_of(Fn)).size();
            return params_impl<Fn>(std::make_index_sequence<n>{});
        }

        // Fully qualified name of T: enclosing namespaces AND classes
        // ("stressinv::NodalPlane::Planes"). Unlike class_namespace(), which
        // stops at the first non-namespace scope, this keeps walking through
        // outer classes, so a nested type keeps its owner in the spelling.
        template <typename T> inline std::string qualified_class_name() {
            constexpr const char *n = std::define_static_string([] consteval -> std::string {
                // dealias: ^^T through an alias template (remove_cvref_t)
                // reflects the alias, whose parent is the alias's own scope.
                const std::meta::info ty = std::meta::dealias(^^T);
                // Same guard as class_name(): a class template SPECIALIZATION
                // has no plain identifier ("SpatialFieldND<2>"), and asking for
                // one makes this lambda non-constant. display_string_of yields
                // the unqualified template-id, which the scope walk below then
                // prefixes exactly like an ordinary identifier.
                std::string     out(std::meta::has_identifier(ty)
                                        ? std::meta::identifier_of(ty)
                                        : std::meta::display_string_of(ty));
                std::meta::info scope = std::meta::parent_of(ty);
                while (std::meta::has_identifier(scope) &&
                       (std::meta::is_namespace(scope) || std::meta::is_type(scope))) {
                    out   = std::string(std::meta::identifier_of(scope)) + "::" + out;
                    scope = std::meta::parent_of(scope);
                }
                return out;
            }());
            return std::string(n);
        }

        // Replace standalone occurrences of `token` in `s` by `repl` — same
        // word-boundary rules as the backends' qualify_std(): the match must
        // not be part of a longer identifier nor already qualified.
        inline std::string replace_type_token(std::string s, std::string_view token,
                                              const std::string &repl) {
            auto ident_char = [](char ch) {
                return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                       (ch >= '0' && ch <= '9') || ch == '_';
            };
            std::string::size_type pos = 0;
            while ((pos = s.find(token, pos)) != std::string::npos) {
                const bool left_ok = pos == 0 || (!ident_char(s[pos - 1]) && s[pos - 1] != ':');
                const std::string::size_type end = pos + token.size();
                const bool right_ok              = end >= s.size() || !ident_char(s[end]);
                if (left_ok && right_ok) {
                    s.replace(pos, token.size(), repl);
                    pos += repl.size();
                } else {
                    pos = end;
                }
            }
            return s;
        }

        // Re-qualify the user-class token of an IR type inside an EXACT spelling
        // (ret_cpp / param_cpp / ctor_param_cpp). display_string_of prints user
        // types unqualified ("Data &"); the emitted `using namespace` directives
        // resolve that — until two bound namespaces declare the same identifier
        // (the "expose" rename case), where the bare token is ambiguous. The IR
        // knows which class the type actually is, so swap the token for its
        // qualified form (recursing into vector elements and callback
        // signatures). Already-qualified occurrences are left alone.
        inline std::string qualify_objects(std::string s, const GenType &t) {
            if (!t.object.empty() && !t.object_qualified.empty() &&
                t.object != t.object_qualified) {
                s = replace_type_token(std::move(s), t.object, t.object_qualified);
            }
            for (const auto &e : t.element) {
                s = qualify_objects(std::move(s), e);
            }
            for (const auto &cb : t.callback_sig) {
                s = qualify_objects(std::move(s), cb);
            }
            return s;
        }

        // Exact C++ spelling of a reflected type (cv- and ref-qualifiers kept),
        // prettified only for the libc++ basic_string spelling. Unlike
        // type_descriptor (which strips cvref for the language-neutral kind),
        // this is what a trampoline override signature must reproduce verbatim.
        // display_string_of spells a class nested inside another class by its
        // bare identifier ("Planes" for stressinv::NodalPlane::Planes); the
        // emitted code opens namespaces (using_namespaces_of) but cannot open
        // classes, so the bare token would not resolve — re-qualify it with
        // its full parent chain (always legal, whatever scope it lands in).
        template <std::meta::info Ty> inline std::string exact_spelling() {
            constexpr const char *s = std::define_static_string(std::meta::display_string_of(Ty));
            std::string out         = prettify(s);
            using U                 = std::remove_cvref_t<typename[:Ty:]>;
            if constexpr (std::is_class_v<U> || std::is_enum_v<U>) {
                // dealias before parent_of: ^^U reflects the remove_cvref_t
                // alias, whose parent is the alias's scope, not U's owner.
                constexpr std::meta::info ur = std::meta::dealias(^^U);
                if constexpr (std::meta::has_identifier(ur) &&
                              std::meta::is_type(std::meta::parent_of(ur))) {
                    constexpr const char *id =
                        std::define_static_string(std::meta::identifier_of(ur));
                    out = replace_type_token(std::move(out), id, qualified_class_name<U>());
                }
            }
            return out;
        }

        template <std::meta::info Fn, std::size_t... Is>
        inline std::vector<std::string> param_cpp_impl(std::index_sequence<Is...>) {
            constexpr auto           ps = std::define_static_array(std::meta::parameters_of(Fn));
            std::vector<std::string> out;
            (out.push_back(exact_spelling<std::meta::type_of(ps[Is])>()), ...);
            return out;
        }

        template <std::meta::info Fn> inline std::vector<std::string> param_cpp_of() {
            constexpr auto n = std::define_static_array(std::meta::parameters_of(Fn)).size();
            return param_cpp_impl<Fn>(std::make_index_sequence<n>{});
        }

        // Is `ty` declared (transitively) inside namespace std? Walks the
        // parent chain and checks the outermost named scope.
        consteval bool in_std_namespace(std::meta::info ty) {
            std::meta::info scope = std::meta::parent_of(ty);
            std::meta::info root  = scope;
            while (std::meta::has_identifier(scope)) {
                root  = scope;
                scope = std::meta::parent_of(scope);
            }
            return std::meta::is_namespace(root) && std::meta::has_identifier(root) &&
                   std::meta::identifier_of(root) == std::string_view("std");
        }

        // Constructor-parameter spelling: like exact_spelling, except a
        // parameter whose decayed type lives in namespace std is spelled BY
        // VALUE. Those types cross the language boundary by conversion — the
        // argument the binding passes is a caster temporary — and the value
        // spelling routes construction to the move overload rather than a
        // const& one that may capture a dangling reference (e.g. a view ctor
        // like Serie(const std::vector<double>&, size_t): with the const&
        // spelling, first-match overload dispatch hands every script call to
        // the view ctor and the object ends up referencing freed memory).
        // Parameters of user class types keep their exact spelling: they
        // arrive as references to live registered objects, and a class may
        // deliberately store that reference (e.g. StressDomain's model).
        template <std::meta::info Ty> inline std::string ctor_param_spelling() {
            using U = std::remove_cvref_t<typename[:Ty:]>;
            if constexpr (std::is_class_v<U>) {
                if constexpr (!in_std_namespace(std::meta::dealias(^^U))) {
                    return exact_spelling<Ty>();
                } else {
                    return exact_spelling<std::meta::remove_cvref(Ty)>();
                }
            } else {
                return exact_spelling<std::meta::remove_cvref(Ty)>();
            }
        }

        template <std::meta::info Fn, std::size_t... Is>
        inline std::vector<std::string> ctor_param_cpp_impl(std::index_sequence<Is...>) {
            constexpr auto           ps = std::define_static_array(std::meta::parameters_of(Fn));
            std::vector<std::string> out;
            (out.push_back(ctor_param_spelling<std::meta::type_of(ps[Is])>()), ...);
            return out;
        }

        template <std::meta::info Fn> inline std::vector<std::string> ctor_param_cpp_of() {
            constexpr auto n = std::define_static_array(std::meta::parameters_of(Fn)).size();
            return ctor_param_cpp_impl<Fn>(std::make_index_sequence<n>{});
        }

        // --- Trampoline signature representability, evaluated at reflection time ---
        // Baked into GenMethod::sig_bindable so a backend (which only sees the
        // string IR) can skip emitting a *trampoline override* it could not compile.
        // This is the conservative common denominator across language backends:
        //   * no raw C arrays (no caster anywhere);
        //   * no pointers — marshalling a raw pointer back across the boundary for a
        //     host-language override is not something every backend supports (pybind
        //     handles a pointer to a *registered* type, but Node-API's to_napi has no
        //     conversion for a bare pointer at all), so an overridable signature must
        //     avoid them. Normal (non-overridable) methods are still bound through
        //     each backend's own per-type gate, which may accept pointers.
        //   * a std::vector recurses into its element (a vector<T*> is thus rejected).
        // Value/reference parameters of complete types are fine for every backend.
        template <class T> struct sig_vector_elem {
            static constexpr bool is = false;
            using type               = void;
        };
        template <class E, class A> struct sig_vector_elem<std::vector<E, A>> {
            static constexpr bool is = true;
            using type               = E;
        };

        template <class T> consteval bool sig_type_bindable() {
            using U = std::remove_cvref_t<T>;
            if constexpr (std::is_array_v<std::remove_reference_t<T>>) {
                return false;
            } else if constexpr (std::is_pointer_v<U>) {
                return false;
            } else if constexpr (rosetta::is_sequence<U>::value || rosetta::is_matrix<U>::value) {
                // A trait-registered foreign container in a VIRTUAL signature:
                // the trampoline override must spell the exact type, but the
                // exact spelling is unqualified (display_string_of) and
                // qualify_std would mis-qualify it to std::vector — the
                // override then would not override. The method itself still
                // binds callable through the sequence adapters; only the
                // script-side override is off the table.
                return false;
            } else if constexpr (sig_vector_elem<U>::is) {
                return sig_type_bindable<typename sig_vector_elem<U>::type>();
            } else {
                return std::is_arithmetic_v<U> || std::is_enum_v<U> || std::is_void_v<U> ||
                       requires { sizeof(U); };
            }
        }

        template <std::meta::info Fn, std::size_t... Is>
        consteval bool sig_params_bindable(std::index_sequence<Is...>) {
            constexpr auto params = std::define_static_array(std::meta::parameters_of(Fn));
            return (sig_type_bindable<typename[:std::meta::type_of(params[Is]):]>() && ...);
        }
        template <std::meta::info Fn> consteval bool sig_fn_bindable() {
            constexpr auto arity = std::meta::parameters_of(Fn).size();
            return sig_type_bindable<typename[:std::meta::return_type_of(Fn):]>() &&
                   sig_params_bindable<Fn>(std::make_index_sequence<arity>{});
        }

        // Whether the member function Fn is one of an overload set: more than
        // one exportable member function of its declaring class shares its
        // name. Baked into GenMethod::is_overloaded — the bare `&T::name` the
        // emitters spell is ambiguous for an overload set, so they skip it.
        template <std::meta::info Fn> consteval bool fn_is_overloaded() {
            constexpr auto parent = std::meta::parent_of(Fn);
            if constexpr (!std::meta::is_class_type(parent)) {
                return false; // free function — overloads never reach the IR
            } else {
                auto        ctx = std::meta::access_context::current();
                std::size_t n   = 0;
                for (auto m : std::meta::members_of(parent, ctx)) {
                    if (is_exportable_member_function(m) &&
                        std::meta::identifier_of(m) == std::meta::identifier_of(Fn)) {
                        ++n;
                    }
                }
                return n > 1;
            }
        }

        inline std::string num_str(double d); // defined below; used by default_value_str

        // Render a field's default member initializer (read from a default-built
        // instance) to a string, for the kinds that map to a scalar property.
        template <class F> inline std::string default_value_str(const F &v) {
            if constexpr (std::is_same_v<F, bool>) {
                return v ? "1" : "0";
            } else if constexpr (std::is_enum_v<F>) {
                return std::to_string(static_cast<long long>(v));
            } else if constexpr (std::is_integral_v<F>) {
                return std::to_string(v);
            } else if constexpr (std::is_floating_point_v<F>) {
                return num_str(static_cast<double>(v));
            } else if constexpr (std::is_same_v<F, std::string>) {
                return v;
            } else {
                return {}; // vectors / objects — no scalar default
            }
        }

        // Walk visitor that collects member type info into a GenClass. Templated
        // on T so it can read default member initializers from a default instance.
        template <class T> struct IRVisitor {
            GenClass &out;

            template <std::meta::info Fld, auto... Anns> void field(const char *name) {
                GenField gf;
                gf.name = name;
                gf.type =
                    type_descriptor<std::remove_cvref_t<typename[:std::meta::type_of(Fld):]>>();
                gf.is_readonly = ann::has<readonly>(Anns...);
                gf.doc         = ann::get_or<doc>(doc{""}, Anns...).text;
                if constexpr (ann::has<range>(Anns...)) {
                    constexpr auto r = ann::get_or<range>(range{0, 0}, Anns...);
                    gf.range         = GenRange{true, r.min, r.max};
                }
                if constexpr (ann::has<combobox>(Anns...)) {
                    constexpr auto cb = ann::get_or<combobox>(combobox{}, Anns...);
                    for (std::size_t i = 0; i < cb.count; ++i) {
                        gf.choices.push_back(cb.choices[i]);
                    }
                }
                // Default value: read the member from a default-built instance —
                // but only for field kinds default_value_str can render (scalars,
                // enums, strings). For anything else the instance would be built
                // for nothing, and `T tmp{}` odr-uses T's constructor: the driver
                // links no user library, so a header-declared ctor (GEO::Mesh)
                // would break the generator link.
                {
                    using F = std::remove_cvref_t<typename[:std::meta::type_of(Fld):]>;
                    if constexpr (std::is_default_constructible_v<T> &&
                                  (std::is_arithmetic_v<F> || std::is_enum_v<F> ||
                                   std::is_same_v<F, std::string>)) {
                        T tmp{};
                        gf.default_value = default_value_str<F>(tmp.[:Fld:]);
                    }
                }
                // Every annotation, type-erased — backends query what they want.
                (gf.annotations.emplace_back(Anns), ...);
                out.fields.push_back(std::move(gf));
            }

            template <std::meta::info Fn, auto... Anns> void method_instance(const char *name) {
                constexpr auto vs = ann::get_or<virtual_spec>(virtual_spec{}, Anns...);
                push_method<Fn>(name, false, ann::get_or<doc>(doc{""}, Anns...).text,
                                ann::has<virtual_spec>(Anns...), vs);
                (out.methods.back().annotations.emplace_back(Anns), ...);
            }

            template <std::meta::info Fn, auto... Anns> void method_static(const char *name) {
                // static members are never virtual; pass a default virtual_spec.
                push_method<Fn>(name, true, ann::get_or<doc>(doc{""}, Anns...).text, false,
                                virtual_spec{});
                (out.methods.back().annotations.emplace_back(Anns), ...);
            }

            template <std::meta::info Ctor, auto... /*Anns*/> void constructor() {
                // Parameter spellings a code-emitting backend can reproduce in
                // `py::init<...>()` without reflection on the target — std
                // types by value, see ctor_param_spelling(). A const&/&& pair
                // (Serie's view/owning vector ctors) collapses to one value
                // spelling: keep the first, the emitted init is identical.
                auto cpp = ctor_param_cpp_of<Ctor>();
                for (const auto &prev : out.ctor_param_cpp) {
                    if (prev == cpp) {
                        return;
                    }
                }
                out.ctors.push_back(params_of<Ctor>());
                out.ctor_param_cpp.push_back(std::move(cpp));
            }

        private:
            template <std::meta::info Fn>
            void push_method(const char *name, bool is_static, const char *docstr, bool is_virtual,
                             virtual_spec vs) {
                GenMethod m;
                m.name      = name;
                m.is_static = is_static;
                m.ret       = type_descriptor<
                    std::remove_cvref_t<typename[:std::meta::return_type_of(Fn):]>>();
                m.params      = params_of<Fn>();
                m.doc         = docstr;
                m.is_virtual  = is_virtual;
                m.is_pure     = vs.pure;
                m.is_const    = std::meta::is_const(Fn);
                m.is_noexcept = std::meta::is_noexcept(Fn);
                m.ret_cpp      = exact_spelling<std::meta::return_type_of(Fn)>();
                m.param_cpp    = param_cpp_of<Fn>();
                m.sig_bindable = sig_fn_bindable<Fn>();
                m.ret_is_ref =
                    std::is_lvalue_reference_v<typename[:std::meta::return_type_of(Fn):]>;
                m.is_overloaded = fn_is_overloaded<Fn>();
                out.methods.push_back(std::move(m));
            }
        };

        // A readable type name for human docs: vectors as `element[]`, everything
        // else its prettified C++ spelling (falling back to the neutral kind).
        inline std::string readable_type(const GenType &t) {
            if (t.kind == "vector") {
                return (t.element.empty() ? std::string("any") : readable_type(t.element.front())) +
                       "[]";
            }
            if (!t.spelling.empty()) {
                return t.spelling;
            }
            if (t.kind == "object" || t.kind == "enum") {
                return t.object.empty() ? "any" : t.object;
            }
            return t.kind;
        }

        // Format a range bound as a clean number (drop a trailing ".0…").
        // %g keeps tiny magnitudes exact ("1e-10") — std::to_string would
        // collapse a solver tolerance to "0.000000" and emit a wrong bound.
        inline std::string num_str(double d) {
            if (d == static_cast<long long>(d)) {
                return std::to_string(static_cast<long long>(d));
            }
            char buf[32];
            std::snprintf(buf, sizeof buf, "%g", d);
            return buf;
        }

        // The per-class Markdown fragment (heading + field table + methods),
        // rendered from the erased IR. Used as GenClass::doc (README bodies and
        // the markdown backend) and by rosetta::to_markdown<T>(). Mirrors what the
        // former <rosetta/docgen.h> produced, but from GenClass rather than a walk.
        inline std::string class_markdown(const GenClass &gc) {
            std::string out = "# " + exposed_of(gc) + "\n\n";
            if (!gc.fields.empty()) {
                out +=
                    "## Fields\n\n| Name | Type | Description |\n|------|------|-------------|\n";
                for (const auto &f : gc.fields) {
                    std::string desc = f.doc;
                    auto        add  = [&](const std::string &tag) {
                        if (!desc.empty()) {
                            desc += " ";
                        }
                        desc += tag;
                    };
                    if (f.range.has) {
                        add("(range: " + num_str(f.range.min) + ".." + num_str(f.range.max) + ")");
                    }
                    if (!f.choices.empty()) {
                        std::string c = "(choices: ";
                        for (std::size_t i = 0; i < f.choices.size(); ++i) {
                            c += (i ? ", " : "") + f.choices[i];
                        }
                        add(c + ")");
                    }
                    if (f.is_readonly) {
                        add("_(readonly)_");
                    }
                    out +=
                        "| `" + f.name + "` | `" + readable_type(f.type) + "` | " + desc + " |\n";
                }
            }
            if (!gc.methods.empty()) {
                out += (gc.fields.empty() ? "" : "\n");
                out += "## Methods\n\n";
                for (const auto &m : gc.methods) {
                    out += "### `";
                    out += (m.is_static ? "static " : "");
                    out += m.name + "(";
                    for (std::size_t i = 0; i < m.params.size(); ++i) {
                        out += (i ? ", " : "") + m.params[i].name + ": " +
                               readable_type(m.params[i].type);
                    }
                    out += ") → " + readable_type(m.ret) + "`\n\n";
                    if (!m.doc.empty()) {
                        out += m.doc + "\n\n";
                    }
                }
            }
            return out;
        }

        // Erase one class to plain data — the only place reflection runs.
        template <typename T> inline GenClass describe() {
            GenClass gc;
            gc.name       = class_name<T>();
            gc.name_space = class_namespace<T>();
            gc.qualified  = qualified_class_name<T>();
            // The binding name: the trait's `expose` override (manifest
            // "expose") when present, else the reflected identifier.
            gc.expose = gc.name;
            if constexpr (requires { binding_info<T>::expose; }) {
                gc.expose = binding_info<T>::expose;
            }
            // binding_info<T>::header is the #include basename — needed by code
            // backends, but not by render-to-string callers (to_markdown / to_html),
            // which may have no specialization. Use it only when present.
            if constexpr (requires { binding_info<T>::header; }) {
                gc.header = binding_info<T>::header;
            }
            // Carry the out-of-line annotation source so re-walking backends
            // (python / node) can re-emit it into their own TUs — see
            // includes_of(). Empty when the class has no side-car.
            gc.annotations_json = std::string(rosetta::ann_json_source<T>);
            // Every class-level annotation, type-erased — backends query what
            // they want via find_annotation<A>(); the core names none of them.
            template for (constexpr auto a :
                          std::define_static_array(std::meta::annotations_of(^^T))) {
                gc.annotations.emplace_back([:std::meta::constant_of(a):]);
            }
            gc.is_default_constructible = std::is_default_constructible_v<T>;
            gc.is_abstract              = std::is_abstract_v<T>;
            gc.copy_or_move_assignable =
                std::is_copy_assignable_v<T> || std::is_move_assignable_v<T>;
            gc.copy_or_move_constructible =
                std::is_copy_constructible_v<T> || std::is_move_constructible_v<T>;
            // Direct public bases as qualified names; the backend filters to the
            // ones actually bound before registering the relationship.
            {
                constexpr auto ctx = std::meta::access_context::current();
                template for (constexpr auto base : std::define_static_array(
                                  std::meta::bases_of(std::meta::dealias(^^T), ctx))) {
                    if constexpr (std::meta::is_public(base)) {
                        using B              = [:std::meta::type_of(base):];
                        const std::string ns = class_namespace<B>();
                        gc.bases.push_back(ns.empty() ? class_name<B>()
                                                      : ns + "::" + class_name<B>());
                    }
                }
            }
            IRVisitor<T> v{gc};
            walk<T>(v);
            // Render the doc fragment after the walk has filled fields/methods.
            gc.doc = class_markdown(gc);
            return gc;
        }

        // Render an enum's markdown fragment (heading + value table). Used both
        // as the GenEnum::doc (README body) and by the markdown backend.
        inline std::string render_enum_markdown(const GenEnum &ge) {
            std::string s = "# " + ge.name + "\n\n";
            s += "_enum";
            if (!ge.underlying.empty()) {
                s += " : " + ge.underlying;
            }
            s += "_\n\n";
            s += "| Name | Value |\n|------|-------|\n";
            for (const auto &v : ge.values) {
                s += "| `" + v.name + "` | " + std::to_string(v.value) + " |\n";
            }
            return s;
        }

        // Erase one enum to plain data — collects its enumerators via
        // reflection. The companion to describe() for enum pack elements.
        template <typename T> inline GenEnum describe_enum() {
            GenEnum ge;
            ge.name       = class_name<T>();
            ge.name_space = class_namespace<T>();
            // Namespaces *and* enclosing classes, so a nested enum keeps its
            // owner in every emitted C++ spelling (Modeler3D::SolverMode).
            ge.qualified  = qualified_class_name<T>();
            // The binding name: the trait's `expose` override (manifest
            // "expose") when present, else the reflected identifier.
            ge.expose     = ge.name;
            if constexpr (requires { binding_info<T>::expose; }) {
                ge.expose = binding_info<T>::expose;
            }
            ge.header     = binding_info<T>::header;
            ge.underlying = std::define_static_string(
                std::meta::display_string_of(std::meta::underlying_type(^^T)));
            template for (constexpr auto e :
                          std::define_static_array(std::meta::enumerators_of(^^T))) {
                constexpr const char *en = std::define_static_string(std::meta::identifier_of(e));
                ge.values.push_back(GenEnumerator{en, static_cast<long long>([:e:])});
            }
            ge.doc = render_enum_markdown(ge);
            return ge;
        }

        // --- Foreign-sequence adapter helpers (shared by the runtime backends) ---
        // A trait-registered sequence (GenType::is_sequence) crosses the boundary
        // by COPY through a std::vector<element>: the emitted adapter declares the
        // boundary vector as its parameter, builds the foreign container before
        // the call (seq_decl_stmts) and flattens a returned one after
        // (seq_from_expr). Because the adapter CALLS the method by name with
        // concrete arguments — instead of spelling the ambiguous `&T::name`
        // member pointer — an overload set whose surviving IR entry is the
        // sequence one binds too (overload resolution happens in the adapter).

        // Adapter-marshalable? Element must round-trip a script array cheaply.
        inline bool seq_ok(const GenType &t) {
            if (!t.is_sequence || t.element.empty() || t.seq_cpp.empty()) {
                return false;
            }
            const GenType &e = t.element.front();
            return e.kind == "number" || e.kind == "boolean" || e.kind == "string" ||
                   e.kind == "enum";
        }

        // The boundary element / vector spellings ("double" / "std::vector<double>").
        inline std::string seq_elem_cpp(const GenType &t) {
            const GenType &e = t.element.front();
            if (e.kind == "string") {
                return "std::string";
            }
            if (e.kind == "boolean") {
                return "bool";
            }
            if (e.kind == "enum") {
                return e.object;
            }
            return e.spelling.empty() ? "double" : e.spelling; // number
        }
        inline std::string seq_boundary_cpp(const GenType &t) {
            return "std::vector<" + seq_elem_cpp(t) + ">";
        }

        // Statements building the foreign container `sn` from boundary vector `an`.
        inline std::string seq_decl_stmts(const GenType &t, const std::string &an,
                                          const std::string &sn, const std::string &ind) {
            return ind + t.seq_cpp + " " + sn + ";\n" +
                   ind + sn + ".resize(" + an + ".size());\n" +
                   ind + "std::copy(" + an + ".begin(), " + an + ".end(), " + sn +
                   ".begin());\n";
        }

        // Expression flattening a foreign container value/reference `expr`.
        inline std::string seq_from_expr(const GenType &t, const std::string &expr) {
            return seq_boundary_cpp(t) + "(" + expr + ".begin(), " + expr + ".end())";
        }

        // --- Foreign matrices (rosetta::is_matrix) ------------------------------
        // The same adapter, one dimension up: the boundary is a
        // std::vector<std::vector<element>> — an array of rows — and the
        // conversions are loops over operator()(i, j), the only access the
        // registration promises. Every backend reaches these through the
        // adapt_* pair below, so a method mixing a sequence and a matrix
        // parameter needs no special case anywhere.

        // Adapter-marshalable? Numbers only: a grid of strings or enums has no
        // natural script shape, and no library asks for one.
        inline bool mat_ok(const GenType &t) {
            return t.is_matrix && !t.element.empty() && !t.mat_cpp.empty() &&
                   t.element.front().kind == "number";
        }

        inline std::string mat_elem_cpp(const GenType &t) {
            const GenType &e = t.element.front();
            return e.spelling.empty() ? "double" : e.spelling;
        }
        inline std::string mat_boundary_cpp(const GenType &t) {
            return "std::vector<std::vector<" + mat_elem_cpp(t) + ">>";
        }

        // Statements building the foreign matrix `mn` from boundary array `an`.
        // The row count is the outer size and the column count the FIRST row's,
        // so a ragged array is squared off rather than read out of bounds.
        inline std::string mat_decl_stmts(const GenType &t, const std::string &an,
                                          const std::string &mn, const std::string &ind) {
            return ind + "const std::size_t " + mn + "_r = " + an + ".size();\n" + ind +
                   "const std::size_t " + mn + "_c = " + mn + "_r ? " + an +
                   ".front().size() : 0;\n" + ind + t.mat_cpp + " " + mn + ";\n" + ind + mn +
                   ".resize(" + mn + "_r, " + mn + "_c);\n" + ind + "for (std::size_t i = 0; i < " +
                   mn + "_r; ++i) {\n" + ind + "    for (std::size_t j = 0; j < " + mn +
                   "_c && j < " + an + "[i].size(); ++j) {\n" + ind + "        " + mn +
                   "(i, j) = " + an + "[i][j];\n" + ind + "    }\n" + ind + "}\n";
        }

        // Expression flattening a foreign matrix value/reference `expr`. An
        // immediately-invoked lambda, so every backend keeps its plain
        // `return <expr>;` shape for a two-loop conversion.
        inline std::string mat_from_expr(const GenType &t, const std::string &expr) {
            const std::string v = mat_boundary_cpp(t);
            return "[&] { " + v + " o(static_cast<std::size_t>(" + expr +
                   ".rows())); for (std::size_t i = 0; i < o.size(); ++i) { "
                   "o[i].resize(static_cast<std::size_t>(" +
                   expr + ".cols())); for (std::size_t j = 0; j < o[i].size(); ++j) { o[i][j] = " +
                   expr + "(i, j); } } return o; }()";
        }

        // --- The two shapes, as one --------------------------------------------
        // Everything below this point in the backends asks "does this type go
        // through the copy adapter, and what does its boundary look like?"
        // rather than which of the two traits registered it.
        inline bool is_adapted(const GenType &t) { return t.is_sequence || t.is_matrix; }
        inline bool adapt_ok(const GenType &t) { return t.is_sequence ? seq_ok(t) : mat_ok(t); }
        inline std::string adapt_boundary_cpp(const GenType &t) {
            return t.is_sequence ? seq_boundary_cpp(t) : mat_boundary_cpp(t);
        }
        inline std::string adapt_decl_stmts(const GenType &t, const std::string &an,
                                            const std::string &sn, const std::string &ind) {
            return t.is_sequence ? seq_decl_stmts(t, an, sn, ind) : mat_decl_stmts(t, an, sn, ind);
        }
        inline std::string adapt_from_expr(const GenType &t, const std::string &expr) {
            return t.is_sequence ? seq_from_expr(t, expr) : mat_from_expr(t, expr);
        }
        // The adapter's local for parameter `j` — `seq0` / `mat0`, so the
        // generated code says which shape it is converting.
        inline std::string adapt_local(const GenType &t, std::size_t j) {
            return (t.is_sequence ? "seq" : "mat") + std::to_string(j);
        }

        // Does this signature touch an adapted container at all / only
        // marshalable ones? (touches && !adaptable ⇒ leave the method to the
        // backend's ordinary gates — for most that means skipping it.)
        inline bool seq_touches(const GenMethod &m) {
            if (is_adapted(m.ret)) {
                return true;
            }
            for (const auto &p : m.params) {
                if (is_adapted(p.type)) {
                    return true;
                }
            }
            return false;
        }
        inline bool seq_adaptable(const GenMethod &m) {
            if (!seq_touches(m)) {
                return false;
            }
            if (is_adapted(m.ret) && !adapt_ok(m.ret)) {
                return false;
            }
            for (const auto &p : m.params) {
                if (is_adapted(p.type) && !adapt_ok(p.type)) {
                    return false;
                }
            }
            return true;
        }

        // --- Foreign-library interop (rosetta/interop.h) ------------------------
        // The interops actually REACHED by the bound API, gathered from the
        // finished IR rather than from the traits directly. Two reasons: the
        // traits can only be queried from a template (see interop_detail),
        // and a caster header nothing in the signature needs is compile time
        // spent for nothing — an opt-in whose types never surface emits no
        // include at all.
        inline void collect_interop_type(const GenType &t, std::vector<std::string> &out) {
            if (!t.interop.empty() &&
                std::find(out.begin(), out.end(), t.interop) == out.end()) {
                out.push_back(t.interop);
            }
            for (const auto &e : t.element) {
                collect_interop_type(e, out);
            }
            for (const auto &cb : t.callback_sig) {
                collect_interop_type(cb, out);
            }
        }

        inline std::vector<std::string> collect_interop(const std::vector<GenClass>    &classes,
                                                        const std::vector<GenFunction> &functions) {
            std::vector<std::string> out;
            const auto               params = [&](const std::vector<GenParam> &ps) {
                for (const auto &p : ps) {
                    collect_interop_type(p.type, out);
                }
            };
            for (const auto &k : classes) {
                for (const auto &f : k.fields) {
                    collect_interop_type(f.type, out);
                }
                for (const auto &m : k.methods) {
                    collect_interop_type(m.ret, out);
                    params(m.params);
                }
                for (const auto &ct : k.ctors) {
                    params(ct);
                }
            }
            for (const auto &f : functions) {
                collect_interop_type(f.ret, out);
                params(f.params);
            }
            return out;
        }

        // --- Dual-marked types: the caster wins --------------------------------
        // A type can be BOTH a registered sequence and owned by an interop
        // library: Eigen::VectorXd listed under "sequences" while
        // "interop": ["eigen"] is on — the escape hatch that gets node / wasm /
        // lua a flat array out of a signature the Python family would rather
        // marshal as numpy. Where the framework HAS the caster, the caster wins:
        // a numpy view beats a copied list, in both directions.
        //
        // Rather than have every gate in those backends ask the question, the
        // backend drops the sequence marks from its OWN copy of the context,
        // once, at the top of its source funnel — after which the type reads
        // exactly like a plain interop-marked one and no other line changes.
        inline void interop_wins_type(GenType &t, bool (*has_caster)(const std::string &)) {
            if (is_adapted(t) && !t.interop.empty() && has_caster(t.interop)) {
                t.is_sequence = false;
                t.is_matrix   = false;
                t.seq_cpp.clear();
                t.mat_cpp.clear();
                t.element.clear(); // the container's element; a caster type carries none
            }
            for (auto &e : t.element) {
                interop_wins_type(e, has_caster);
            }
            for (auto &cb : t.callback_sig) {
                interop_wins_type(cb, has_caster);
            }
        }

        inline GenContext interop_wins(GenContext c, bool (*has_caster)(const std::string &)) {
            const auto params = [&](std::vector<GenParam> &ps) {
                for (auto &p : ps) {
                    interop_wins_type(p.type, has_caster);
                }
            };
            for (auto &k : c.classes) {
                for (auto &f : k.fields) {
                    interop_wins_type(f.type, has_caster);
                }
                for (auto &m : k.methods) {
                    interop_wins_type(m.ret, has_caster);
                    params(m.params);
                }
                for (auto &ct : k.ctors) {
                    params(ct);
                }
            }
            for (auto &f : c.functions) {
                interop_wins_type(f.ret, has_caster);
                params(f.params);
            }
            return c;
        }

        // Build a GenContext from a type pack (no files, no targets) — the same
        // class/enum split generate() does, for the render-to-string helpers.
        template <typename... Ts> inline GenContext make_context(std::string lib) {
            GenContext c;
            c.lib = std::move(lib);
            (
                [&] {
                    if constexpr (std::is_enum_v<Ts>) {
                        c.enums.push_back(describe_enum<Ts>());
                    } else {
                        c.classes.push_back(describe<Ts>());
                    }
                }(),
                ...);
            c.interop = collect_interop(c.classes, c.functions);
            return c;
        }

    } // namespace gen_detail

} // namespace rosetta

// -------- built-in backends (one file each) --------
// Each defines its templates + a gen_detail::*Backend, using the shared
// render helpers above. New backends register the same way (see
// docs/EXTENDING_BACKEND.md) without touching generate().
#include <rosetta/backends/csharp_backend.h>
#include <rosetta/backends/html_backend.h>
#include <rosetta/backends/java_backend.h>
#include <rosetta/backends/json_backend.h>
#include <rosetta/backends/julia_backend.h>
#include <rosetta/backends/markdown_backend.h>
#include <rosetta/backends/nanobind_backend.h>
#include <rosetta/backends/node_backend.h>
#include <rosetta/backends/openapi_backend.h>
#include <rosetta/backends/paraview_backend.h>
#include <rosetta/backends/python_backend.h>
#include <rosetta/backends/python_expanded_backend.h>
#include <rosetta/backends/csharp_expanded_backend.h>
#include <rosetta/backends/java_expanded_backend.h>
#include <rosetta/backends/julia_expanded_backend.h>
#include <rosetta/backends/lua_expanded_backend.h>
#include <rosetta/backends/nanobind_expanded_backend.h>
#include <rosetta/backends/node_expanded_backend.h>
#include <rosetta/backends/qt_expanded_backend.h>
#include <rosetta/backends/imgui_expanded_backend.h>
#include <rosetta/backends/qml_expanded_backend.h>
#include <rosetta/backends/rest_backend.h>
#include <rosetta/backends/typescript_backend.h>
#include <rosetta/backends/wasm_backend.h>
#include <rosetta/backends/wasm_expanded_backend.h>

namespace rosetta {

    // -------- backend registry --------

    inline std::map<std::string, std::shared_ptr<Backend>> &backend_registry() {
        static std::map<std::string, std::shared_ptr<Backend>> reg = [] {
            std::map<std::string, std::shared_ptr<Backend>> m;
            m["python"]          = std::make_shared<gen_detail::PythonBackend>();
            m["python-expanded"] = std::make_shared<gen_detail::PythonExpandedBackend>();
            m["nanobind"]          = std::make_shared<gen_detail::NanobindBackend>();
            m["nanobind-expanded"] = std::make_shared<gen_detail::NanobindExpandedBackend>();
            m["node"]            = std::make_shared<gen_detail::NodeBackend>();
            m["node-expanded"]   = std::make_shared<gen_detail::NodeExpandedBackend>();
            m["qt-expanded"]     = std::make_shared<gen_detail::QtExpandedBackend>();
            m["imgui-expanded"]  = std::make_shared<gen_detail::ImGuiExpandedBackend>();
            m["qml-expanded"]    = std::make_shared<gen_detail::QmlExpandedBackend>();
            m["julia"]           = std::make_shared<gen_detail::JuliaBackend>();
            m["julia-expanded"]  = std::make_shared<gen_detail::JuliaExpandedBackend>();
            m["lua-expanded"]    = std::make_shared<gen_detail::LuaExpandedBackend>();
            m["rest"]            = std::make_shared<gen_detail::RestBackend>();
            m["wasm"]            = std::make_shared<gen_detail::WasmBackend>();
            m["wasm-expanded"]   = std::make_shared<gen_detail::WasmExpandedBackend>();
            m["typescript"]      = std::make_shared<gen_detail::TypeScriptBackend>();
            m["markdown"]        = std::make_shared<gen_detail::MarkdownBackend>();
            m["html"]            = std::make_shared<gen_detail::HtmlBackend>();
            m["json"]            = std::make_shared<gen_detail::JsonBackend>();
            m["openapi"]         = std::make_shared<gen_detail::OpenApiBackend>();
            m["paraview"]        = std::make_shared<gen_detail::ParaViewBackend>();
            m["csharp"]          = std::make_shared<gen_detail::CSharpBackend>();
            m["csharp-expanded"] = std::make_shared<gen_detail::CSharpExpandedBackend>();
            m["java"]            = std::make_shared<gen_detail::JavaBackend>();
            m["java-expanded"]   = std::make_shared<gen_detail::JavaExpandedBackend>();
            return m;
        }();
        return reg;
    }

    inline void register_backend(std::string lang, std::shared_ptr<Backend> backend) {
        backend_registry()[std::move(lang)] = std::move(backend);
    }

    // Erase one free function (named by its reflection F) to plain data. Called
    // from the generated driver, which splices `^^name` for each manifest-listed
    // function. `qualified` is the C++ spelling backends emit for `&fn`; `doc`
    // comes from the manifest (free functions carry no in-source annotation, so
    // user headers stay untouched). `expose` (manifest "expose") replaces the
    // reflected identifier as the binding name — the C++ spelling stays
    // `qualified`, so only what scripts see changes.
    template <std::meta::info F>
    inline GenFunction make_function(const char *qualified, const char *header, const char *doc,
                                     const char *expose) {
        GenFunction gf;
        gf.name      = (expose && *expose) ? std::string(expose)
                                           : std::string(std::define_static_string(
                                                 std::meta::identifier_of(F)));
        gf.qualified = qualified;
        gf.header    = header;
        gf.ret       = gen_detail::type_descriptor<
            std::remove_cvref_t<typename[:std::meta::return_type_of(F):]>>();
        gf.params = gen_detail::params_of<F>();
        gf.doc    = doc;
        return gf;
    }

    template <typename... Ts> inline void generate(const GenerateOptions &opt) {
        // Erase the type pack into plain data once; backends do no reflection.
        // Enum pack elements go to `enums`, everything else to `classes`.
        std::vector<GenClass> classes;
        std::vector<GenEnum>  enums;
        (
            [&] {
                if constexpr (std::is_enum_v<Ts>)
                    enums.push_back(gen_detail::describe_enum<Ts>());
                else {
                    classes.push_back(gen_detail::describe<Ts>());
                }
            }(),
            ...);

        // Mark the manifest's "final" classes: no trampoline even with public
        // virtuals (see GenClass::is_final). Matched like extensions —
        // qualified or unqualified spelling.
        for (const std::string &fc : opt.final_classes) {
            bool found = false;
            for (auto &k : classes) {
                const std::string qualified =
                    k.name_space.empty() ? k.name : k.name_space + "::" + k.name;
                if (fc == qualified || fc == k.name) {
                    k.is_final = true;
                    found      = true;
                    break;
                }
            }
            if (!found) {
                std::fprintf(stderr,
                             "rosetta::generate: \"final\" names unbound class '%s' — "
                             "ignored\n",
                             fc.c_str());
            }
        }

        // Attach the extension methods (manifest class "extensions") to their
        // classes: a free function whose first parameter is `Cls&` becomes an
        // instance method of Cls in the IR, with the receiver dropped from the
        // parameter list. Backends emit it as a method backed by the free
        // function (ext_qualified) instead of a member pointer.
        for (const GenExtension &ext : opt.extensions) {
            GenClass *target = nullptr;
            for (auto &k : classes) {
                const std::string qualified =
                    k.name_space.empty() ? k.name : k.name_space + "::" + k.name;
                if (ext.cls == qualified || ext.cls == k.name) {
                    target = &k;
                    break;
                }
            }
            if (target == nullptr) {
                std::fprintf(stderr,
                             "rosetta::generate: extension '%s' names unbound class '%s' — "
                             "skipped\n",
                             ext.fn.qualified.c_str(), ext.cls.c_str());
                continue;
            }
            const bool self_ok = !ext.fn.params.empty() &&
                                 ext.fn.params.front().type.kind == "object" &&
                                 ext.fn.params.front().type.object == target->name &&
                                 ext.fn.params.front().is_ref;
            if (!self_ok) {
                std::fprintf(stderr,
                             "rosetta::generate: extension '%s' must take '%s&' as its first "
                             "parameter — skipped\n",
                             ext.fn.qualified.c_str(), ext.cls.c_str());
                continue;
            }
            GenMethod m;
            m.name          = ext.fn.name;
            m.doc           = ext.fn.doc;
            m.ret           = ext.fn.ret;
            m.params        = {ext.fn.params.begin() + 1, ext.fn.params.end()};
            m.is_extension  = true;
            m.ext_qualified = ext.fn.qualified;
            m.ext_header    = ext.fn.header;
            target->methods.push_back(std::move(m));
            // Refresh the class doc fragment so extension methods show up in
            // the markdown/html output like any other method.
            target->doc = gen_detail::class_markdown(*target);
        }

        // Join the (possibly several) user include dirs into one string that
        // drops straight into each backend's target_include_directories(... PRIVATE)
        // list: subsequent paths align under the first via the template's indent.
        std::string user_include;
        for (std::size_t i = 0; i < opt.user_include.size(); ++i) {
            if (i) {
                user_include += "\n    ";
            }
            user_include += opt.user_include[i].string();
        }

        // User .cpp sources as plain strings for the GenContext (backends compile
        // them into their binding target — see user_lib_block / {{USER_SOURCES}}).
        std::vector<std::string> user_sources;
        for (const auto &src : opt.user_sources) {
            user_sources.push_back(src.string());
        }

        // External user libraries. `user_libs` is the general form; the older
        // single-library shorthand (user_lib_name / _dir / _link) folds into one
        // entry when it is the only thing set, so hand-written drivers predating
        // the list keep working.
        std::vector<UserLib> user_libs = opt.user_libs;
        if (user_libs.empty() && !opt.user_lib_name.empty()) {
            user_libs.push_back({opt.user_lib_name, opt.user_lib_dir, opt.user_lib_link});
        }

        for (const TargetSpec &t : opt.targets) {
            auto &reg = backend_registry();
            auto  it  = reg.find(t.lang);
            if (it == reg.end() || !it->second) {
                std::fprintf(stderr,
                             "rosetta::generate: no backend registered for target '%s' — skipped\n",
                             t.lang.c_str());
                continue;
            }
            it->second->emit(GenContext{opt.out_dir, t.name, classes, enums, opt.functions,
                                        user_include, opt.rosetta_include.string(),
                                        opt.cpp26_root, opt.cpp26_cxx, opt.cpp26_cc,
                                        opt.cpp26_lib, opt.qt_dir, user_libs, user_sources,
                                        opt.compile_definitions, t.link_options,
                                        opt.build_type, opt.optimization,
                                        opt.cxx_standard, opt.version,
                                        gen_detail::collect_interop(classes, opt.functions),
                                        t.artifact_dir, t.python, t.requires_python,
                                        t.napi_version, t.node_engine});
        }
    }

} // namespace rosetta
