// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Reflection-driven binding scaffolder. `rosetta::generate<T>(...)` reads
// the `rosetta::binding_info<T>` trait specialization and emits a
// per-backend project tree under <out_dir>:
//
//   <out_dir>/python/{auto_pybind.cpp, CMakeLists.txt, README.md}
//   <out_dir>/node/  {auto_napi.cpp,   CMakeLists.txt, package.json, README.md}
//   <out_dir>/rest/  {auto_rest.cpp,   CMakeLists.txt, README.md}
//   <out_dir>/web/   {auto_emscripten.cpp, CMakeLists.txt, README.md}
//
// The trait carries the per-class config (target list, lib name, header
// basename) so the class definition stays pristine. Example:
//
//   template <> struct rosetta::binding_info<Person> {
//       static constexpr std::array  targets{"python", "node"};
//       static constexpr const char *lib    = "reflected_person";
//       static constexpr const char *header = "person.h";
//   };
//
// The user-include and rosetta-include directory paths are not in the
// trait — they are build-context-dependent and come from the caller
// (typically CLI flags on the driver).

#pragma once

#include <any>
#include <cstdio>
#include <experimental/meta>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <rosetta/annotations.h>
#include <rosetta/walk.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rosetta {

    /**
     * @brief Per-class binding configuration. Specialize this trait for each
     * type you pass to `rosetta::generate<Ts...>`. The class itself stays
     * unmodified; the only per-class metadata is its header basename.
     *
     * Required static member:
     *   - `header` — `const char*` basename used in `#include "..."`
     *
     * The module / library name and the target backends are no longer
     * per-class — they live in `GenerateOptions` because one generator
     * call emits a single combined module per backend exposing every
     * class. Example:
     *   template <> struct rosetta::binding_info<Person> {
     *       static constexpr const char *header = "person.h";
     *   };
     */
    template <class T> struct binding_info; // primary — must be specialized

    /**
     * @brief One output target: a backend language and the module /
     * library name to bake into that backend's generated bindings.
     */
    struct TargetSpec {
        std::string lang; // "python", "node", "rest", "web"
        std::string name; // module / library name for this backend
    };

    /**
     * @brief A reflected type, reduced to a small language-neutral descriptor
     * so pure-data backends (TypeScript, JSON Schema, …) can render it without
     * reflection. `kind` is one of: "number", "boolean", "string", "void",
     * "vector", "object", "enum", "unknown". For "vector", `element` holds one
     * entry (the element type); for "object" and "enum", `object` is the
     * class / enumeration identifier.
     */
    /** @brief One enumerator: its name and its value as a signed integer. */
    struct GenEnumerator {
        std::string name;
        long long   value = 0;
    };

    struct GenType {
        std::string          kind    = "unknown";
        std::string          object;  // class / enum identifier ("object" / "enum")
        std::vector<GenType> element; // 0 or 1 entry, the element when "vector"
        bool                 integer = false; // kind == "number" and integral (vs floating)
        std::string          spelling; // prettified C++ type spelling (for human docs)
        std::vector<GenEnumerator> enumerators; // populated when kind == "enum"

        // True when the type is a raw pointer to a class (`T*`); `object` then
        // holds the pointee class identifier. `kind` is deliberately left
        // "unknown" so backends that don't opt in keep skipping raw pointers (no
        // regression); a backend that CAN marshal a pointer to a bound class
        // (e.g. embind via allow_raw_pointers) checks this flag explicitly.
        bool is_pointer = false;

        // True when the type is a std::function<R(A...)>. Like is_pointer, `kind`
        // stays "unknown" so backends that don't opt in keep skipping callbacks;
        // a backend that CAN marshal a JS function into a std::function (e.g.
        // embind via emscripten::val) checks this flag and consults `callback_sig`
        // to decide whether the whole signature is convertible. `callback_sig[0]`
        // is the return type (kind "void" when none); [1..] are the parameter
        // types, each cvref-stripped like any other GenType.
        bool                 is_callback = false;
        std::vector<GenType> callback_sig;
    };

    /** @brief A numeric range constraint (rosetta::range annotation). */
    struct GenRange {
        bool   has = false;
        double min = 0;
        double max = 0;
    };

    struct GenField {
        std::string name;
        GenType     type;
        bool        is_readonly = false;
        std::string doc;     // rosetta::doc annotation text, if any
        GenRange    range;   // rosetta::range, if any
        std::vector<std::string> choices;       // rosetta::combobox choices, if any
        std::string              default_value; // default member initializer, rendered (if capturable)

        // Every annotation on this member, type-erased. Backends query the ones
        // they care about via find_annotation<A>() — the core names none of them.
        std::vector<std::any> annotations;
    };

    struct GenParam {
        std::string name; // synthesized "argN" (parameter names aren't reflected)
        GenType     type;
    };

    struct GenMethod {
        std::string            name;
        bool                   is_static = false;
        GenType                ret;
        std::vector<GenParam>  params;
        std::string            doc; // rosetta::doc annotation text, if any

        // Virtual / trampoline metadata, captured from the rosetta::virtual_spec
        // that walk<T>() synthesizes plus direct reflection queries. Used by
        // backends that emit overridable bindings (e.g. pybind11 trampolines).
        // `ret_cpp` / `param_cpp` are the *exact* C++ spellings (cv- and
        // ref-qualifiers preserved), unlike GenType::spelling which is
        // cvref-stripped for human docs — a trampoline override must match the
        // base signature exactly to actually override it.
        bool                     is_virtual  = false;
        bool                     is_pure     = false;
        bool                     is_const    = false;
        bool                     is_noexcept = false;
        std::string              ret_cpp;   // exact return-type spelling
        std::vector<std::string> param_cpp; // exact parameter-type spellings, in order

        // False when the return type or a parameter is something pybind11 has no
        // type-caster for in this TU (a pointer/vector-of-pointer to an incomplete
        // type, a raw C array). Computed at reflection time so a backend can skip
        // emitting a trampoline override it could not compile. Defaults true.
        bool sig_bindable = true;

        // Every annotation on this method, type-erased (mirrors GenField). UI
        // backends query the ones they care about — e.g. rosetta::button /
        // rosetta::label — via find_annotation<A>(); the core names none of them.
        std::vector<std::any> annotations;
    };

    /**
     * @brief One free (non-member) function, erased to plain data. Declared in
     * the manifest (header + name + optional doc) rather than reflected from a
     * type, so the user's headers stay pristine. `qualified` is the C++ spelling
     * a backend emits for the function pointer (e.g. `api::add`); `name` is the
     * unqualified identifier used as the exposed binding name.
     */
    struct GenFunction {
        std::string           name;      // exposed (unqualified) identifier
        std::string           qualified; // fully-qualified C++ spelling for &fn
        std::string           header;    // basename for #include
        GenType               ret;
        std::vector<GenParam> params;
        std::string           doc; // from the manifest, if any
    };

    /**
     * @brief One class, erased to the plain data a backend needs. `generate`
     * fills this up front (the only place reflection runs), so backends are
     * pure text templating. Member type info (`fields` / `methods` / `ctors`)
     * is populated for pure-data backends; backends that emit C++ and defer to
     * a runtime visitor can ignore it and use just `name` / `header`.
     */
    struct GenClass {
        std::string name;       // reflected (unqualified) C++ identifier
        std::string name_space; // enclosing namespace ("" if global, "a::b" if nested)
        std::string header; // binding_info<T>::header — basename for #include

        // Qualified names of the direct *public* base classes (e.g.
        // "arch::BaseRemote"), in declaration order. A backend that registers an
        // inheritance relationship — e.g. pybind11's py::class_<T, Base> so a
        // derived instance is accepted where a base pointer/reference is expected
        // — consults these, filtering to the bases that are themselves bound.
        std::vector<std::string> bases;
        std::string doc;    // class_markdown(*this) — per-class Markdown fragment (README body)
        std::string annotations_json; // raw out-of-line annotation side-car (ann_json_source<T>), if any

        // Every class-level annotation, type-erased (see GenField::annotations).
        std::vector<std::any> annotations;

        std::vector<GenField>              fields;  // public data members
        std::vector<GenMethod>             methods; // instance + static methods
        std::vector<std::vector<GenParam>> ctors;   // one param list per constructor

        // Whether T is default-constructible. The implicitly-declared default
        // ctor is often *not* enumerated as a member, so `ctors` may be empty
        // even when `T()` is valid; backends that emit an explicit binding for
        // it (e.g. python-expanded's py::init<>()) consult this instead.
        bool is_default_constructible = false;

        // Whether T is abstract (has an unoverridden pure virtual). An abstract
        // class cannot be instantiated, so a backend must not emit any constructor
        // binding for it (embind's class_ constructor, for one, would try to
        // allocate the abstract type and fail to compile).
        bool is_abstract = false;

        // Exact C++ spellings of each constructor's parameter types, in the same
        // order as `ctors`. Parallel to `ctors` (which carries the neutral IR);
        // a backend that has to *spell* the constructor signature in emitted C++
        // (e.g. py::init<const std::vector<double>&, ...>()) uses these, since
        // GenType::spelling is cvref-stripped and may not round-trip.
        std::vector<std::vector<std::string>> ctor_param_cpp;
    };

    /**
     * @brief One enumeration, erased to plain data. Filled by `generate` (the
     * only place reflection runs) when a pack element is an enum type, so
     * backends render enums as pure text — no reflection.
     */
    struct GenEnum {
        std::string                name;       // reflected (unqualified) C++ identifier
        std::string                name_space; // enclosing namespace ("" if global, "a::b" if nested)
        std::string                header;     // binding_info<T>::header
        std::string                doc;        // markdown fragment for READMEs
        std::string                underlying; // underlying integer type spelling
        std::vector<GenEnumerator> values;     // enumerators in declaration order
    };

    struct GenerateOptions {
        std::filesystem::path    out_dir;         // root of the generated tree
        std::vector<std::filesystem::path> user_include; // dir(s) containing the class headers
        std::filesystem::path    rosetta_include; // path to rosetta's include/
        std::vector<TargetSpec>  targets;         // backends + per-backend module name
        std::vector<GenFunction> functions;       // free functions to expose

        // Optional pointers to the C++26 / P2996 reflection toolchain, baked into
        // the *thin* backends' generated CMakeLists so reflection-driven targets
        // find the right compiler and runtime without editing the output. The
        // stock *-expanded targets ignore all of these. Each is also overridable
        // at configure time (-DCLANG_P2996_ROOT=..., -DROSETTA_CXX_COMPILER=...,
        // -DROSETTA_C_COMPILER=..., -DROSETTA_STDLIB=...).
        //
        //   cpp26_root — toolchain root (clang-p2996 build dir). Empty ⇒ built-in
        //                default $ENV{HOME}/devs/c++/clang-p2996/build. The three
        //                below default to ${CLANG_P2996_ROOT}/{bin/clang++,
        //                bin/clang,lib} when empty, so usually only this is set.
        //   cpp26_cxx  — C++ compiler (name or full path).
        //   cpp26_cc   — C compiler (name or full path).
        //   cpp26_lib  — directory holding the fork's libc++ / libc++abi, used for
        //                -L and -rpath (the "lib" the binding links against).
        std::string cpp26_root;
        std::string cpp26_cxx;
        std::string cpp26_cc;
        std::string cpp26_lib;

        // Optional Qt 6 install prefix, baked as the default of the QT_DIR cache
        // variable in the qt-expanded / qml-expanded CMakeLists. Empty ⇒ built-in
        // default ($ENV{HOME}/Qt/6.8.3/macos). Overridable at configure time with
        // -DQT_DIR=...; backends other than qt/qml ignore it.
        std::string qt_dir;

        // Optional external user library to link the generated bindings against.
        // Use this when the bound headers only *declare* the API and the bodies
        // live in a separately-compiled (shared or static) library — the binding
        // TU then needs that library at link time. Both must be set to take
        // effect; the stock *-expanded backends emit a target_link_directories /
        // target_link_libraries (+ rpath) referencing them.
        //
        //   user_lib_name — the library's base name (e.g. "space" ⇒ -lspace,
        //                   libspace.dylib / .so / .a).
        //   user_lib_dir  — directory holding the built library (-L / rpath).
        //   user_lib_link — preferred link form: "shared" (default) or "static".
        //                   The generated CMake links that form by full path and
        //                   falls back to whichever is actually present on disk.
        //                   WebAssembly ignores it and always links static (a
        //                   native shared object cannot enter a wasm module).
        std::string user_lib_name;
        std::string user_lib_dir;
        std::string user_lib_link; // "shared" (default) | "static"; empty ⇒ shared

        // Optional user source files (.cpp) compiled directly into every generated
        // binding target. Use this — instead of (or alongside) user_lib — when the
        // bound headers only *declare* the API and the bodies live in source files
        // you want built into the binding rather than linked from a pre-built
        // library. Each compiled backend adds them to its binding target via
        // target_sources(); the text-only backends ignore them. Absolute paths.
        // Entries may be C sources (.c) — the generated CMakeLists then calls
        // enable_language(C) so they build (vendored zlib / rply / libMeshb…).
        std::vector<std::filesystem::path> user_sources;

        // Optional preprocessor definitions applied to every compiled binding
        // target (and picked up by user_sources), each "NAME" or "NAME=VALUE" —
        // e.g. {"GEOGRAM_USE_BUILTIN_DEPS", "GEOGRAM_WITH_HLBFGS"}. Emitted as
        // target_compile_definitions(... PRIVATE ...); text-only backends
        // ignore them.
        std::vector<std::string> compile_definitions;
    };

    /**
     * @brief Everything a backend needs to emit one target's project tree.
     */
    struct GenContext {
        std::filesystem::path    out_dir;         // root of the generated tree
        std::string              lib;             // this target's module / library name
        std::vector<GenClass>    classes;         // all classes to expose
        std::vector<GenEnum>     enums;           // all enumerations to expose
        std::vector<GenFunction> functions;       // all free functions to expose
        std::string              user_include;    // dir containing the class headers
        std::string              rosetta_include; // path to rosetta's include/
        std::string              cpp26_root;      // C++26 toolchain root (default of CLANG_P2996_ROOT)
        std::string              cpp26_cxx;       // C++ compiler   (default ${CLANG_P2996_ROOT}/bin/clang++)
        std::string              cpp26_cc;        // C compiler     (default ${CLANG_P2996_ROOT}/bin/clang)
        std::string              cpp26_lib;       // fork stdlib dir (default ${CLANG_P2996_ROOT}/lib)
        std::string              qt_dir;          // Qt 6 prefix (default of QT_DIR; qt/qml backends)
        std::string              user_lib_name;   // external lib to link bindings against (-l<name>); empty ⇒ none
        std::string              user_lib_dir;    // directory holding that lib (-L / rpath)
        std::string              user_lib_link;   // "shared" (default) | "static"; wasm always static
        std::vector<std::string> user_sources;    // user .cpp/.c files compiled into the binding target (abs paths)
        std::vector<std::string> compile_definitions; // "NAME"/"NAME=VALUE" defs for the binding target
    };

    /**
     * @brief Code-generation backend for one target language. Implement this
     * and register it (see `register_backend`) to teach `generate` a new
     * backend — no edit to `generate` itself is required.
     */
    struct Backend {
        virtual ~Backend()                          = default;
        // Write this target's project tree under c.out_dir.
        virtual void emit(const GenContext &) const = 0;
        // Render this backend's primary document to a string, for single-artifact
        // ("document") backends like markdown / html. Multi-file project backends
        // (python, node, rest, …) have no single string and leave this empty.
        virtual std::string render(const GenContext &) const { return {}; }
    };

    /**
     * @brief The lang → backend map consulted by `generate` at run time.
     * Seeded with the built-in "python", "node", "rest", "web" backends on
     * first use.
     */
    std::map<std::string, std::shared_ptr<Backend>> &backend_registry();

    /** @brief Register (or override) the backend handling `lang`. */
    void register_backend(std::string lang, std::shared_ptr<Backend> backend);

    /**
     * @brief Static-init helper: declare one at namespace scope in a plugin
     * translation unit linked into the generator to register a backend before
     * `main` runs. e.g.
     *   static rosetta::BackendRegistrar lua{"lua", std::make_shared<LuaBackend>()};
     */
    struct BackendRegistrar {
        BackendRegistrar(std::string lang, std::shared_ptr<Backend> backend) {
            register_backend(std::move(lang), std::move(backend));
        }
    };

    /**
     * @brief Scaffold the per-backend binding projects under opt.out_dir for
     * the whole set of classes `Ts...`. The pack is erased into a
     * `std::vector<GenClass>` and each target is dispatched through
     * `backend_registry()`; this function never changes when a backend is
     * added. Per-class headers come from the `rosetta::binding_info<T>` trait.
     */
    template <typename... Ts> void generate(const GenerateOptions &opt);

    /**
     * @brief Describe one free function (identified by its reflection `F`) as
     * plain data for `GenerateOptions::functions`. The generated driver calls
     * this with `^^name` for each function listed in the manifest; `qualified`
     * is the C++ spelling backends emit for the function pointer and `header`
     * its include basename. Free functions are declared in the manifest, never
     * by editing the user's headers.
     */
    template <std::meta::info F>
    GenFunction make_function(const char *qualified, const char *header, const char *doc);

} // namespace rosetta

#include "inline/generate.hxx"
