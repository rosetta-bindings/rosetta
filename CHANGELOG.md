# Changelog

All notable changes to **rosetta** are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). This project has not yet cut a tagged release, so entries are grouped by date rather than by version number. Dates are `YYYY-MM-DD`.

## [Unreleased]

### Changed
- **`std::vector<T>` crosses the wasm boundary as a plain JS `Array`** — the embind backend emitted `register_vector<T>`, which made it an *opaque bound class*: JS had to fill an `M.vector_double` with `push_back`, could not pass an array literal, and got a handle (not an `Array`) back, one it then had to `.delete()`. It was the one backend where a sequence was not the host language's own array type, contradicting what [docs/MANIFEST.md](docs/MANIFEST.md) already promised. The generated source now specializes embind's `BindingType<std::vector<T>>` onto the emval wire — `val::array` out (a typed-array memcpy for arithmetic `T`), `vecFromJSArray` in — and registers each element type with `register_type` instead. embind's own `const T` / `T&` forwarders carry it to `const std::vector<T>&` parameters and returns, so plain member-function pointers still bind and nested `vector<vector<T>>` falls out by recursion. **Breaking for hand-written JS**: `M.vector_double` and friends no longer exist; pass and expect arrays. `examples/plain-binding/drive.wasm.js` is now `drive.js` line for line, printing identical output.
- **The IR accessors are public** — `qualified_of` / `exposed_of` / `exposed_object_of` / `names_type` moved from `rosetta::gen_detail` to `rosetta::`, where the rest of the backend API already was, with the old spelling kept as `using`-declarations onto the same functions.
- **The header-only runtimes moved to `include/rosetta/runtime/`** — the five reflection-free headers the *generated* project compiles (`node`, `csharp`, `java`, `qt_widgets`, `imgui`) left `backends/` and `visitors/`, neither of which described them.
- **Backends dropped the `_backend` suffix and moved to `rosetta::backend`** — `backends/python_backend.h` → `backends/python.h` and `gen_detail::PythonBackend` → `backend::Python` across all 19, with `visitors/*_visitor.{h,hxx}` → `visitors/*.{h,hxx}` to match.
- **The last four `-expanded` names are gone** — `lua`, `qt`, `qml` and `imgui` dropped a suffix that had never distinguished them from a thin twin they never had, with the old spellings still resolving as deprecated aliases.

## 2026-08-15

### Removed
- **The seven *thin* backends are gone** — `python`, `nanobind`, `node`, `wasm`, `julia`, `csharp` and `java` now mean what `*-expanded` meant, deleting 14 backend files and the 14 reflection visitors only they used (~4800 lines).
- **Migration is a no-op for existing manifests** — the `*-expanded` spellings stay registered as aliases and `rosetta_gen` folds them to the short name, though output directories follow the short name, so a tree generated earlier has stale `*-expanded/` directories to remove.

### Fixed
- **nanobind bindings register their base class** — `nb::class_<T>` never declared the bases the IR had carried all along, so every derived class was an unrelated Python type and passing one where a base was expected failed.
- `collect_members` now enumerates through `access_context::unchecked()`
- **A class with a non-public destructor is skipped, not compiled** — every runtime backend destroys what it wraps, so listing such a class failed the build with an error pointing inside `<__memory/unique_ptr.h>`.
- **A `compile_definitions` value may contain quotes** — `GEOGRAM_VERSION="1.10.1"` was emitted inside a plain string literal that ended at the value's own first quote; it is a raw string literal now.
- **A class-typed return whose class is not bound no longer binds (python / node)** — pybind11 threw on the first call and node took the whole process down with a fatal N-API error, where both now skip the member and record it in `coverage.json`.
- **An overload whose exact signature has no spelling is skipped rather than emitted** — a disambiguating `static_cast` naming a lambda closure type produced a *compile* error, where every other gate in rosetta produces a skip.

### Added
- **Out-parameters — `"out_params": {"AttributesManager::get_doubles": [1, 2]}`** — a parameter the method returns through is filled by an emitted local and joins the return value, as a tuple in Python, multiple returns in Lua, an array in JS and a declared tuple in TypeScript.
- **`module_init` — the manifest can say what runs when the module LOADS** — statements spliced verbatim at the top of the module entry point, for the library start-up work that is not expressible as "bind this name".
- **`generated_headers` — a header the bound library's own build system would have produced** — a manifest entry names a template plus substitutions (or literal content), and the finished text is written into the binding tree, first on the include path.
- **`std::filesystem::path` marshals as a string, and `std::shared_ptr<T>` reaches the caster-less backends** — the two independent gaps that had kept a factory API like `std::shared_ptr<Mesh> compile_file(const std::filesystem::path&)` from binding on any target.
- **Overload selection on FREE functions — `"signature": "void(GEO::Mesh&, bool)"`** — a manifest entry may name the one overload it means, since `^^GEO::mesh_union` is ill-formed the moment the namespace declares the name twice.
- **Overload support — a class's whole overload set now reaches the binding** — the walk deduplicated member functions by identifier alone, silently dropping every overload but one.
- **`coverage.json` — a machine-readable account of what bound and what did not** — written on every run, recording reflection-stage drops, per-target skips with a reason, and what did bind, so a method that stops binding is a reviewable diff ([docs/COVERAGE.md](docs/COVERAGE.md)).

## 2026-08-09

### Fixed
- **A bound class template specialization was spelled with its template arguments stripped of every namespace** — `display_string_of` is a *display* rendering rather than a C++ spelling, so the template-id is now composed structurally from `template_of` + `template_arguments_of`.
- **csharp / java emitted a member pointer that could not compile for any overloaded method** — the bare `&T::f` names the whole overload set, so both now spell it through the shared cast to the exact signature.
- **nanobind: the abi3 wheel was *tagged* but not *built*** — nanobind discards a `STABLE_ABI` request without a word when `Development.SABIModule` is not among the requested components, so the wheel installed on every CPython ≥ 3.12 and imported on exactly one.
- **A class nested inside another class is spelled fully qualified (`GenClass::qualified`)** — `class_namespace<T>()` stops at the first non-namespace scope, so the `name_space::name` reconstruction collapsed to a bare identifier the emitted code cannot resolve.
- **`qualified_class_name<T>()` handles a class template specialization** — a specialization has no plain identifier, so the unguarded `identifier_of` stopped the enclosing `consteval` lambda from being a constant expression.

### Added
- In **rosetta_gen**, added option `--jobs N, -jN` for parallel build jobs
- **Pinning the runtime — per-target `"python"` / `"requires_python"` / `"napi_version"` / `"node_engine"`** — a generated project used to take whatever `PATH` handed it, and the obvious `-DPython_EXECUTABLE` escape hatch was silently overwritten by the template's own probe.
- **Where the built artifact lands — per-target `"out_dir"`** — a target may name the directory its artifact is copied to after every build, instead of every project bolting on a copy step that has to know each backend's build layout.
- **`"wheel"` / `"wheel_dir"` in the manifest** — packaging was command-line-only, so both are now manifest fields acting as defaults that the flags can still override, but only ever toward packaging more.
- **The caster-less backends reach interop types too — `"sequences": [{ "type": "Eigen::VectorXd" }]`** — a concrete type may state its own C++ spelling and cross node / wasm / lua through the flat-array adapter, while the Python family keeps its native caster.
- **Foreign 2-D matrices — `"matrices": [{ "type": "Eigen::MatrixXd" }]`** — `is_sequence` one dimension up, marshalled as an array of rows through a `std::vector<std::vector<element>>` boundary on every expanded backend.
- **Foreign-library interop — `"interop": ["eigen"]` binds Eigen types natively (python / nanobind)** — recognition is by enclosing namespace rather than a list of type names, so one line covers every Eigen spelling including the ones a hand-maintained list would have missed.
- **Python wheels — `python` / `nanobind` emit `pyproject.toml` + `make_wheel.py`** — the backends produced a buildable CMake project but nothing installable, and `rosetta_gen --build` gained `--wheel` / `--wheel-dir` to drive the packaging.
- **`std::shared_ptr<T>` crosses the boundary — holders declared automatically (python)** — pybind11 refuses to hand out a `shared_ptr` whose pointee was not registered with a matching holder, so the holder set is computed and propagated along bound base links.

## 2026-07-31

### Fixed
- **A non-const `T&` return no longer binds as a COPY (python / nanobind)** — a fluent API handed Python a copy on every call, measured at 3.66 s for a 50k-point loop against 5.5 ms once `reference_internal` was attached.
- **Enumerations nested inside a class are spelled fully qualified (`GenEnum::qualified`)** — the same shape as the nested-class fix, one level down and in the IR rather than in a backend.
- **nanobind: the STL type casters are actually included** — nanobind splits its casters one per header where pybind11 ships them together, so every standard type outside `string` / `vector` / `function` compiled and then threw at call time.
- **pybind11 trampolines: a return type containing a comma no longer mis-expands** — `PYBIND11_OVERRIDE` is a macro, so `vector<T, allocator<T>>` was split across two macro arguments; such a return type now hides behind a local alias.
- **nanobind: no constructor is emitted for an abstract class** — `nb::init<>` instantiates the alias type, so `new T{}` on a pure-virtual interface is ill-formed.

`tests/shared_ptr.cpp` covers all four with 14 gtests over the generated sources, 9 of which fail against the pre-fix backends.

### Added
- **`"expose"` now reaches every backend — and works on enums** — the class rename was consumed by only 7 of 26 backends, and a rename on an enum entry was parsed, emitted into `bindings.h`, and then silently ignored by all of them.
- **`"expose"` on free functions and extension methods** — two free functions sharing an unqualified name had no fix and degraded differently per backend, pybind making an overload set where node simply kept the last.
- **Per-class `"expose"` — bind a class under a different name** — and emitted C++ now spells every bound class qualified, since the unqualified spelling became ambiguous the moment two bound namespaces declared the same identifier.
- **Manifest `user_lib` accepts an array** — a bound library plus the pre-built libraries it depends on, linked in array order with one de-duplicated rpath list.
- **`user_sources` globstar — `**` matches zero or more directories** — so one entry replaces a `*.cxx` line per subdirectory.
- **Manifest grouped entries — scoped defaults inside `classes` / `functions`** — an element may be a group carrying `entries` plus local `namespace` / `header_dir` / `header` defaults, nestable and resolved entirely in `load()`.
- **Manifest `namespace` / `header_dir` — shared defaults for class & function entries** — two optional top-level fields factor the per-entry repetition out of `classes`, `functions` and `extensions`.
- **`rosetta_gen --init <src_dir>` — pre-fill the manifest from a source scan** — a comment- and preprocessor-aware token walk rather than a real C++ parse, skipping what it cannot bind with a note where useful.

### Changed
- **`rosetta_gen` split into one file per concern** — ~2400 lines of `rosetta_gen.cpp` became `manifest` / `emit` / `init` / `build` / `clean` / `util`, verified to produce byte-identical output.
- **Plain `rosetta_gen` emits incrementally and guides the next step** — unchanged files are no longer rewritten, and both footguns of the manual three-step workflow now print a note.

## 2026-07-20

### Added
- **Manifest `build_type` / `optimization` — build configuration for every generated CMakeLists** — emitted as a default inside `if(NOT CMAKE_BUILD_TYPE)` and as `add_compile_options` / `add_link_options` respectively, so a configure-time `-D` still wins.

### Fixed
- **wasm templates: manifest `link_options` land after the template's own link flags** — emcc honors the *last* occurrence of a repeated `-s` setting, so an override could never take effect before.
- **Expanded backends: nested class types are spelled fully qualified** — the emitted code opens namespaces with `using namespace` but cannot open classes, so a bare nested identifier in a constructor or trampoline signature failed to compile.
- **Constructor parameters of std-namespace types are spelled by value — dangling-view fix** — bindings pass caster temporaries, so a constructor that *stored* a `const std::vector<double>&` captured freed memory.
- **The walk skips `std::initializer_list` constructors** — no target language can produce one, and such a constructor always shadows an equivalent `std::vector` overload.
- **Node: a non-default-constructible class is constructible from script** — `Wrap` default-constructed and then assigned, so a class whose only constructors take arguments threw "this class cannot be constructed directly".

### Changed
- **Expanded backends emit `CMAKE_CXX_STANDARD 20`** — the templates still pinned C++17, so a bound library using `std::span` or concepts in its headers failed to compile.

All four fixes verified end to end on the stressinv manifest (20 classes, 14 free functions); suite 56/56.

## 2026-07-16

### Added
- **`rosetta_gen --build` — the whole manifest pipeline in one command** — emits, builds and runs the generator, then compiles every declared backend with its own build shape, skipping the ones whose toolchain is absent; the companion `--clean` removes only what the pipeline generated.
- **`rosetta_gen` builds on Windows** — the `user_sources` glob expansion dropped POSIX `<glob.h>` for a `std::filesystem` walk with the same semantics.
- **Per-class `"final": true` manifest flag — suppress the trampoline** — a class marked final generates no trampoline, which also makes a virtual-bearing class eligible as a node member-object property.
- **Foreign sequence containers (`rosetta::is_sequence` trait + manifest `sequences` field)** — a library's own vector type crosses the boundary by copy through a `std::vector<element>` adapter, like a `std::vector` of its element.
- **Member-object properties on wasm** — the non-copyable member object of a bound class binds as a borrowed-handle getter method, since embind properties copy.
- **Member-object properties — a class's non-copyable member objects become reference properties** — bound read-only against the real member, with the parent kept alive for as long as any child handle exists.

### Fixed
- **Overload sets no longer break the expanded builds** — the bare `&T::name` member pointer is ambiguous for an overload set, so binding any class with a const/non-const accessor pair failed to compile.

### Changed
- `node_runtime.h` now holds only the documented declarations, with the conversion helpers, the override plumbing and all of `Wrap`'s member definitions moved to its inline half (the `generate.h` / `inline/generate.hxx` layout convention).

## 2026-07-08

### Fixed
- **Out-of-line side-car: scientific notation in numbers** — the consteval JSON parser had no exponent support, so a `"range": [1e-10, 1e-6]` parsed as `[1, 0]` and silently derailed every key after it.
- **Emitted range bounds lose tiny magnitudes** — `std::to_string` collapses `1e-10` to `0.000000`, so bounds are formatted with `%g` now.

### Added
- **Four new widget hints: `color`, `multiline`, `radio`, `file`** — implemented across all three UI inspector backends, inline and out of line alike.
- **Out-of-line side-cars reach annotation parity** — `.ann.json` now also carries `label`, `button` and `widget`, so a stock-C++ header plus a side-car drives exactly the same UI as inline annotations.
- **Dear ImGui backend (`imgui`)** — the immediate-mode counterpart of the Qt inspector: a self-contained GLFW + OpenGL 3 app whose generated `draw_<Class>()` functions bind widgets directly to the live members, no copies and no signal plumbing.
- **Julia backend, expanded variant (CxxWrap / jlcxx)** — building against the *stock* libc++ lets `<jlcxx/stl.hpp>` compile, so `std::vector` is fully bound where the thin target had to skip it entirely.
- **Lua backend (sol2)** — a `require`-able C module for Lua 5.1–5.4 / LuaJIT built with a stock C++17 compiler, with full runtime constructor overload resolution and multiple inheritance via `sol::bases`.
- **Extension methods (`extensions` manifest class field)** — a free function whose first parameter is `Cls&` is exposed as an ordinary instance method, the escape hatch for a library whose own members cannot cross the boundary.
- **Copyability captured in the IR, and emitters gate on it** — every runtime backend copies at some boundary, so a class whose copy is deleted used to make the *generated code* fail to compile.
- **Per-target `link_options` manifest field** — extra linker flags for that target only, since link flags are toolchain-specific in a way `compile_definitions` is not.
- **`compile_definitions` manifest field** — preprocessor definitions applied to the driver and every compiled binding target, so the walk, the bound headers and the `user_sources` all see the same preprocessor state.
- **C sources in `user_sources`** — `.c` entries make the generated CMakeLists call `enable_language(C)` so they compile alongside the C++ binding.
- **Node / WebAssembly: JS-function callbacks are marshalled into `std::function` parameters** — bound only when the whole signature is convertible, so an unconvertible one is skipped rather than breaking the build.
- **Node / WebAssembly: raw pointers to bound classes are marshalled** — as a non-owning handle, with the C++ side keeping ownership.
- **Python / WebAssembly: C++ inheritance is registered** — a derived instance is now accepted wherever a base pointer or reference is expected, and the hierarchy is visible to the host language.
- **`rosetta_gen --init` flag** — writes a fully-commented example `manifest.json`, refusing to overwrite an existing one.
- **`user_sources` manifest field** — user `.cpp` files compiled directly into every generated binding target, with shell globs expanded at generation time.
- **Multiple include directories** — `user_include` accepts an array of directories, searched like a compiler's `-I` order.

### Fixed
- **Generator link no longer requires the bound class's constructor** — the IR visitor default-constructed a `T tmp{}` to read every field's default, odr-using a constructor the driver links no library for.
- **Node: the `Wrap` constructor compiles for non-assignable classes** — its parameterized path is now `if constexpr`-guarded on assignability, matching the emitter's own gate.
- **Node: wrapped class arguments are handed to C++ by reference, not by copy** — so a function taking `T&` mutates the caller-visible JS object, and a pImpl facade no longer dangles its duplicated impl pointer.
- **WebAssembly: several build breakers for a real library** — C++20, by-value marshalling restricted to bound classes, no constructor for an abstract class, `static_cast` disambiguation of member pointers, and at most one constructor per arity.
- **Node: by-value class marshalling requires a round-trippable type** — complete, default-constructible and copy-assignable, or the member is skipped rather than breaking the build.
- **Python / Node: trampoline override signatures resolve unqualified `std` names** — the trampoline block emits a `using namespace std;` scoped to its own namespace, with no leak into the registration code or user headers.
- **Python / Node: trampolines skip virtuals they can't marshal** — a new `GenMethod::sig_bindable` flag omits the overrides whose signature has no caster, keeping the trampoline instantiable.
- **Python / nanobind / WebAssembly / Qt / QML: unbindable members no longer break the module** — raw C arrays and types only forward-declared in the binding TU are skipped instead of aborting the whole build.
- **Python / nanobind / WebAssembly / Qt / QML: a pointer is judged by its pointee, and `std::vector` by its element** — `sizeof(T*)` is always valid, so a pointer to an incomplete type slipped through the old guard and broke the build inside the framework's caster.
- **Python / nanobind / WebAssembly / Julia: the synthesized default constructor was registered behind a runtime `if`** — a plain `if` still instantiates the discarded branch, a hard error for a type whose only constructors take arguments.
- **Node: reference parameters of bound types** — a new `arg_from_napi<P>` binds them to the wrapper's persistent object, so in-place mutations propagate back to the JS object.
- **Node: unbindable free functions are skipped, not fatal** — the module still loads and every other function stays usable.
- **Template-specialization type names** — `class_name<T>()` falls back to the full display spelling when the type has no plain identifier, instead of hard-erroring.
- **Operator / conversion members** — the member walk skips functions without an identifier, which cannot be bound by name to a target language.

### Changed
- `annotate.h` now holds only the customization points, the parsed-representation types and documented declarations, with the consteval JSON parser and the walk-time merge implementations moved to `inline/annotate.hxx`.

### Docs
- Added `docs/ANNOTATIONS.md` — the companion reference to `MANIFEST.md` for the annotation layer: the full set, inline vs out-of-line spelling side by side, which backend consumes what, and the gotchas.
- Documented `user_sources`, multiple `user_include` directories, and added an initial `docs/MANIFEST.md` reference for the manifest file.
- Documented per-target `link_options`, class `extensions` and the skip-instead-of-fail semantics for unmarshalable members in `docs/MANIFEST.md`.
- Added this `CHANGELOG.md`.

## 2026-06-27

### Added
- External third-party library linking: full example using both dynamic and static linkage, with handling for libraries that live in their own namespace.

### Changed
- Reworked dynamic-vs-static user-library linkage; namespaced third-party libraries now bind without qualifying every spelling.

## 2026-06-26

### Added
- **Java backend and visitor** — C-ABI shared library plus handle-backed FFM wrappers.
- More backend examples wired up for the `geom-lib` example.

### Docs
- README updates, including the backend capability table.

## 2026-06-25

### Added
- **C# backend** — C-ABI shared library plus P/Invoke wrappers, buildable without a C++26 toolchain (expanded path).

## 2026-06-20 – 2026-06-21

### Added
- **nanobind** visitor/backend support, plus a `nanobind-expanded` variant.
- **Expanded backends** — reflection runs once on a C++26 host and the generated sources build with a stock compiler, for toolchains that don't yet support C++26 / P2996 reflection.
- Browser example for the WebAssembly target.

### Changed
- "Transparent rosetta" pass over the generation flow.

## 2026-06-16 – 2026-06-19

### Added
- **Inheritance introspection** — base-class flattening, `virtual_spec` carried through the walk, and trampolines for pybind11 and Node so virtual/overriding methods are distinguished from plain ones.
- **ParaView** Server Manager plugin XML generation.

### Changed
- Refactored doc generation.

### Docs
- README updates.

## 2026-06-12 – 2026-06-14

### Added
- **Out-of-line (external file) annotations** — annotate bound types from a JSON side-car so the headers stay clean.
- GoogleTest integration and `signal::scoped_connect`.

### Changed
- Reorganized sources; simplified the mini-MOC signal handling and refactored the mini-MOC.
- Moved the reflection walk into an inline `walk.hxx`.

### Removed
- Obsolete files.

## 2026-06-09 – 2026-06-11

### Added
- **Julia** language binding/backend (CxxWrap.jl / jlcxx).

### Changed
- Moved the Qt/QML inspectors under `include/rosetta`; general reorganization and a linter pass.

### Docs
- README and Julia example updates.

## 2026-06-02 – 2026-06-08

### Added
- **Enum** support.
- **Free (non-member) function** binding.
- **REST** backend (cpp-httplib JSON server + browser client) and a JSON (de)serializer.
- **OpenAPI 3.1** spec backend.
- **TypeScript** (`.d.ts`) and **Markdown** documentation backends.

### Changed
- Split backends into separate files; renamed the `web` target to `wasm`.
- Inlined the doc generator.

## 2026-05-31 – 2026-06-01

### Added
- The `rosetta_gen` manifest-driven project generator.
- Constructor binding support.

## 2026-05-29 – 2026-05-30

### Added
- CLI tools for generating skeletons.
- Richer annotations and an initial Qt/QML example.

### Docs
- Early documentation.

## 2026-05-28

### Added
- Initial commit: the rosetta framework and introductory slides.

[Unreleased]: https://github.com/Xaliphostes/rosetta/compare/df8960d...HEAD
