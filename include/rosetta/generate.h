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
#include <rosetta/sequence.h>
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

        // Extra linker flags for THIS target only (manifest target
        // "link_options"). Per-target — unlike compile_definitions — because
        // link flags are inherently toolchain-specific: e.g. "-lnodefs.js"
        // is only meaningful for the wasm backends and would break a native
        // link.
        std::vector<std::string> link_options;
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

        // True when the (cvref-stripped) type is a trait-registered foreign
        // sequence container (rosetta::is_sequence<T>, e.g. GEO::vector<double>
        // — see rosetta/sequence.h). Like is_pointer, `kind` stays "unknown" so
        // backends that don't opt in keep skipping it; an opted-in backend
        // marshals it by COPY through a std::vector<element> at the boundary.
        // `element` holds one entry (the element type, like kind == "vector")
        // and `seq_cpp` the qualified C++ spelling an emitted adapter can
        // construct ("GEO::vector<double>") — needed because display_string_of
        // prints template names unqualified.
        bool        is_sequence = false;
        std::string seq_cpp;

        // True when the type is a std::function<R(A...)>. Like is_pointer, `kind`
        // stays "unknown" so backends that don't opt in keep skipping callbacks;
        // a backend that CAN marshal a JS function into a std::function (e.g.
        // embind via emscripten::val) checks this flag and consults `callback_sig`
        // to decide whether the whole signature is convertible. `callback_sig[0]`
        // is the return type (kind "void" when none); [1..] are the parameter
        // types, each cvref-stripped like any other GenType.
        bool                 is_callback = false;
        std::vector<GenType> callback_sig;

        // Copyability of the (cvref-stripped) type, captured at reflection time.
        // Every runtime backend copies at some boundary — pybind's automatic
        // return policy copies an lvalue-ref return, embind's property getters
        // and N-API's to_napi copy by construction/assignment — so emitters
        // consult these to SKIP (or downgrade to read-only) what would otherwise
        // be a hard compile error in the generated code. A class whose data API
        // lives in non-copyable public members (e.g. GEO::Mesh::vertices holding
        // a Mesh& back-reference) then binds cleanly as an opaque handle instead
        // of breaking the build. True for non-class kinds; false when the type
        // is incomplete at reflection time.
        bool copy_constructible = true;
        bool copy_assignable    = true;
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

        // True when the parameter is declared as an lvalue reference (T& /
        // const T&). A by-reference class parameter never copies — every
        // runtime backend hands the wrapped object through — so it is bindable
        // even for a non-copyable class; a by-VALUE class parameter copies and
        // needs type.copy_constructible.
        bool is_ref = false;

        // True for a NON-const lvalue reference. For class kinds that's the
        // mutable-receiver / out-parameter feature; for everything else
        // (std::string&, index_t&, enum&) it's an out-parameter the node/wasm
        // runtimes cannot express — their converted argument is a temporary,
        // which cannot bind to a non-const reference — so those backends skip
        // the member.
        bool is_mutable_ref = false;
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

        // True when the method returns an lvalue reference. Combined with
        // ret.copy_constructible this lets an emitter skip methods it would
        // otherwise fail to compile (pybind's automatic policy COPIES an
        // lvalue-ref return, e.g. GEO::Mesh::get_subelements_by_index()
        // returning a non-copyable store&).
        bool ret_is_ref = false;

        // True when the declaring class has MORE THAN ONE member function with
        // this name. The walk dedups overloads by name (one IR entry survives),
        // but the emitters spell the bare member pointer `&T::name`, which is
        // ambiguous for an overload set — the generated line would not compile.
        // Emitters consult this to skip the whole set (conservative; overload
        // selection by explicit signature is the planned lift).
        bool is_overloaded = false;

        // Extension method (manifest class "extensions"): a free function whose
        // first parameter is `Cls&`, exposed as an instance method of Cls.
        // `params` holds the parameters AFTER the receiver; `ext_qualified` is
        // the fully-qualified C++ spelling for &fn; `ext_header` its #include.
        // Backends that can only emit member pointers skip these.
        bool        is_extension = false;
        std::string ext_qualified;
        std::string ext_header;

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
     * @brief An extension method (manifest class "extensions"): a free function
     * whose first parameter is `Cls&` (or `const Cls&`), exposed as an instance
     * method of the bound class `cls`. This is how a library whose own members
     * can't cross the boundary (raw-pointer accessors, attribute templates,
     * overloaded helpers) gets a scriptable surface WITHOUT a hand-written
     * wrapper class: the glue shrinks to stateless free functions and the
     * scripts keep holding the real C++ objects.
     */
    struct GenExtension {
        std::string cls; // the bound class, as spelled in the manifest ("GEO::Mesh")
        GenFunction fn;  // the free function (first param = receiver)
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

        // Whether T can be assigned to (copy OR move). The node runtime's
        // parameterized-constructor path assigns the freshly built object into
        // the Wrap's inner storage; for a non-assignable class (GEO::Mesh) only
        // the default constructor is emitted there.
        bool copy_or_move_assignable = true;

        // Whether T can be constructed from another T (copy OR move). The node
        // runtime's path for a NON-default-constructible class (e.g. a data
        // class whose only ctor is parameterized) builds the object straight
        // from the ctor_table entry into fresh storage, moving or copying the
        // returned value; the emitter registers no entries otherwise.
        bool copy_or_move_constructible = true;

        // Manifest "final": true — treat the class as non-overridable from the
        // host language: NO trampoline is generated even when it has public
        // virtual methods (they still bind as ordinary callable methods).
        // Beyond skipping useless shims, this is what lets the node runtime
        // hand the class out as an aliased member-object property — the alias
        // stores a T*, which requires the wrapped type to BE T, not Js_T
        // (GEO::MeshVertices, whose delete_elements/permute_elements virtuals
        // nobody script-overrides, is the motivating case).
        bool is_final = false;

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
        std::vector<GenExtension> extensions;     // free functions exposed as class methods

        // Class names (as spelled in the manifest, qualified or not) to mark
        // is_final — no trampoline, host-language overriding off. See
        // GenClass::is_final.
        std::vector<std::string> final_classes;

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

        // Optional build configuration baked into every compiled backend's
        // generated CMakeLists (text-only backends have none).
        //
        //   build_type   — default CMAKE_BUILD_TYPE ("Debug", "Release",
        //                  "RelWithDebInfo", "MinSizeRel"), emitted inside
        //                  if(NOT CMAKE_BUILD_TYPE) so -DCMAKE_BUILD_TYPE=...
        //                  at configure time still wins. Empty ⇒ not emitted
        //                  (CMake's usual no-build-type default applies).
        //   optimization — explicit optimization flag ("-O0".."-O3", "-Os",
        //                  "-Oz", "-Og", "-Ofast") added via
        //                  add_compile_options / add_link_options, which land
        //                  AFTER the build type's per-config flags on the
        //                  command line — so this -O overrides the build
        //                  type's own level. Empty ⇒ not emitted.
        std::string build_type;
        std::string optimization;
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
        std::vector<std::string> link_options;    // extra linker flags for THIS target (TargetSpec::link_options)
        std::string              build_type;      // default CMAKE_BUILD_TYPE ("" ⇒ not emitted)
        std::string              optimization;    // explicit -O flag overriding the build type's ("" ⇒ not emitted)
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
