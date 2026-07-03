# Changelog

All notable changes to **rosetta** are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). This project has not yet cut a tagged release, so entries are grouped by date rather than by version number. Dates are `YYYY-MM-DD`.

## [Unreleased]

### Added
- **Node / WebAssembly: JS-function callbacks are marshalled into `std::function` parameters** — a method or constructor taking a `std::function<R(A...)>` (e.g. `UserRemote(fn, isStress)` for a per-point far-field stress, `Solver::onMessage` / `onEnd` progress callbacks) now accepts a host-language function, wrapped into the C++ `std::function` and invoked synchronously (valid for callbacks fired during a host-initiated call such as a `solver.run()`). The neutral IR gained `GenType::is_callback` plus a decomposed `callback_sig` (`[0]` = return type, kind `void` when none; `[1..]` = parameter types), populated in `type_descriptor`. A callback is bound only when its whole signature is convertible — scalars, `bool`, strings, enums, `std::array` (element-wise), vectors of such, and bound classes — so a callback naming a raw pointer, an unbound by-value type, or a nested callback (e.g. `Triangle*`, `Eigen::VectorXd&`, an unbound `StepResult&`) is still skipped rather than breaking the build. **Node** makes `napi_supported<std::function<…>>` true for a convertible signature and materializes the callback with `from_napi`, keeping the JS function alive in a shared `Napi::FunctionReference`; `std::array` gained an N-API array conversion. **WebAssembly (expanded)** emits an `emscripten::val` adapter — a factory-lambda constructor `+[](emscripten::val, …) -> T*` or a receiver-first method lambda — plus a `rosetta_wx` helper (`to_val` / `from_val` / `make_fn`) inlined into the generated TU (embind can't marshal a `std::function` parameter directly). (Python already handled this via `pybind11/functional.h`.)
- **Node / WebAssembly: raw pointers to bound classes are marshalled** — a method returning `T*` (e.g. `Model::addSurface() → Surface*`) or taking one (e.g. `Model::addRemote(BaseRemote*)`, `Postprocess::burgersFor(Surface*)`) is now bound, as a *non-owning* handle to the C++ object (the C++ side keeps ownership). The neutral IR gained `GenType::is_pointer` (with the pointee identifier). **WebAssembly (expanded)** emits `emscripten::allow_raw_pointers()` on any signature naming a raw pointer, gated so only a pointer to a *bound* pointee is accepted (a vector-of-pointer is still skipped — embind has no `register_vector<T*>`). **Node** wraps a returned `T*` by retargeting a fresh wrapper's `self_` at the external object; `Wrap` now holds its owned value in a `unique_ptr` (via a heap-constructing ctor table) so a non-default-constructible type (`SeidelSolver(Model&)`) or an abstract referenced-only base (`BaseRemote`) works, and all member access goes through a `self_`-based `obj()` so owned and referenced wrappers behave identically. A pointer to a forward-declared class is left unsupported (completeness is required).
- **Python / WebAssembly: C++ inheritance is registered** — a bound class now declares its bound base classes to the target framework, so a derived instance is accepted wherever a base pointer/reference is expected (e.g. passing a `UserRemote` to `Model.addRemote(BaseRemote*)`, or a `Coulomb` where a `Tic` is wanted) and the hierarchy is visible to the host language (`issubclass` / prototype chain). Direct public bases are reflected into `GenClass::bases` (via `std::meta::bases_of`) and each backend filters them to the bases that are themselves bound. **Python** threads them through `bind_pybind` into `py::class_<T, Bases..., Trampoline>` (multiple bases; trampolines compose). **WebAssembly (expanded)** emits `emscripten::class_<T, emscripten::base<Base>>` (embind supports single inheritance, so the first bound base is used). Bases must be registered before their derived — which follows from manifest order; a base that isn't bound is simply omitted. (Node is not yet covered: node-addon-api `ObjectWrap` does not compose C++-base inheritance across separately-wrapped types without a dedicated prototype-chain shim.)
- **`rosetta_gen --init` flag** — writes a fully-commented example `manifest.json` (default `./manifest.json`, or a path argument) so a new project can start from a working template instead of the docs. The example exercises the common fields: `cpp26_*` toolchain overrides, an array-form `user_include`, `rosetta_include`, `generator_name` / `module_name`, `user_sources`, a representative spread of `targets`, and one example class and one example function. It **refuses to overwrite an existing file** — warning and exiting non-zero instead — so a hand-written manifest is never clobbered.
- **`user_sources` manifest field** — a list of user `.cpp` files compiled directly into every generated binding target via `target_sources(...)`. Use it when the bound headers only *declare* the API and you want the bodies built into the binding rather than linked from a pre-built `user_lib`. Works alongside or instead of `user_lib`; ignored by the text-only backends. Entries may be **shell globs** (e.g. `src/algorithms/*.cpp`), expanded at generation time; results are sorted and de-duplicated.
- **Multiple include directories** — `user_include` now accepts an array of directories (a class `header` is searched across all of them, like a compiler's `-I` order), in addition to the existing single-string form.

### Fixed
- **WebAssembly (expanded): several build breakers for a real library** — the expanded embind backend now (a) sets `CMAKE_CXX_STANDARD 20` (arch3-style headers use concepts); (b) marshals a by-value class only when it is a *bound* `class_` — an unregistered/incomplete object (`std::array`, `std::initializer_list`, a forward-declared helper returned by value) is skipped instead of aborting the build; (c) emits no constructor for an abstract class (`GenClass::is_abstract`, so embind never tries to allocate one); (d) disambiguates the member-function pointer with an explicit `static_cast` to the exact signature, so an overloaded method no longer trips embind's "no matching function"; (e) emits at most one constructor per arity (embind dispatches constructors by parameter count and throws at registration on a collision — e.g. `Vector3(double)` vs `Vector3(const std::vector&)`). With these, a real library (arch3) builds, links and runs under embind.
- **Node: by-value class marshalling requires a round-trippable type** — the N-API conversion default-constructs a wrapper and copy-assigns, so a class returned/taken by value is now supported only when it is complete, default-constructible and copy-assignable (a non-copyable/incomplete helper like `Model::regions()`'s `ModelRegions` is skipped rather than breaking the build). Pass such types by pointer instead.
- **Python / Node: trampoline override signatures resolve unqualified `std` names** — a trampoline reproduces each virtual's *exact* parameter spelling via `display_string_of`, which drops the `std::` qualifier (e.g. `vector<double, allocator<double>>`), so the generated override failed to compile. The trampoline block now emits a `using namespace std;` scoped to its own `rosetta_py` / `rosetta_node` namespace — enough for the spellings to resolve, with no leak into the binding-registration code or user headers.
- **Python / Node: trampolines skip virtuals they can't marshal** — a virtual method whose signature has no caster (a raw C array, or a pointer the host language can't round-trip — Node-API has no conversion for a bare pointer at all) would make `PYBIND11_OVERRIDE` / `napi_call_override` fail to compile. A new reflection-time `GenMethod::sig_bindable` flag (rejects arrays, all pointers, and `std::vector`-of-pointer — the conservative common denominator across backends) lets the trampoline omit those overrides; a concrete bound class still supplies the body, so the trampoline stays instantiable.
- **Python / nanobind / WebAssembly / Qt / QML: unbindable members no longer break the module** — a method, static method, or field whose return type, a parameter type, or (for a field) its type can't be represented by the target framework now gets skipped instead of aborting the whole build. The two compile-breakers are **raw C arrays** (e.g. a method returning `double (&)[3][3]` — pybind11 / nanobind / embind have no caster, and the Qt / QML backends can't declare `R r` nor round-trip it through `QVariant`) and **types only forward-declared in the binding TU** (pybind needs `sizeof` / `typeid` / `is_polymorphic` on the complete type). A consteval `*_bindable_type` / `*_bindable_fn` guard drops exactly those, matching the support guards the Node, Julia, Java, C#, and REST backends already had. The guard now also covers **constructors** (`py::init<>` / `nb::init<>` / embind `constructor<>` over an unrepresentable parameter no longer aborts the build).
- **Python / nanobind / WebAssembly / Qt / QML: a pointer is judged by its pointee, and `std::vector` by its element** — the previous guard treated any pointer as bindable, but `sizeof(T*)` is always valid so a pointer to an *incomplete* type slipped through and then broke the build inside the framework's caster (it still needs the pointee's `typeid` / `sizeof` to register the conversion). The `*_bindable_type` concept now checks the **pointee** for completeness, and recurses into a `std::vector`'s **element** type (so a `std::vector<T*>` of an incomplete `T` is rejected just like a bare `T*`). A method / field / constructor that names such a type is skipped instead of failing to compile.
- **Python / nanobind / WebAssembly / Julia: the synthesized default constructor was registered behind a runtime `if`** — `bind_*` fell back to registering `T()` with `if (!saw_default_ctor && std::is_default_constructible_v<T>)`. A plain `if` still *instantiates* the discarded branch, so the framework's default-construct (`py::init<>()`, `nb::init<>()`, embind / jlcxx `constructor<>()`, and ultimately `new Trampoline{}`) was compiled even for a non-default-constructible type — a hard error for classes / trampolines whose only constructors take arguments (e.g. a `Surface(Model*, …)` or a pybind trampoline over such a base). Switched to `if constexpr` so the fallback is only instantiated when the type is actually default-constructible. (The Java and C# backends already used `if constexpr`; the Node backend has no such fallback.)
- **Node: reference parameters of bound types** — the N-API backend materialized every argument by value (`from_napi<remove_cvref_t<P>>`), so a non-const lvalue-reference parameter couldn't bind and any function taking a bound type by reference (e.g. an algorithm taking `SurfaceMesh&` and mutating it in place) failed to compile. A new `arg_from_napi<P>` binds reference parameters of wrapped user types directly to the wrapper's persistent `inner` object, so in-place mutations propagate back to the JS object (and `const T&` no longer copies). Applied to free functions, instance/static methods, and constructors.
- **Node: unbindable free functions are skipped, not fatal** — a free function whose signature can't cross the N-API boundary (a pointer out-parameter such as `std::vector<T>*`, a `std::function`, an unsupported return type, …) previously broke the whole module build. `bind_napi_function` now guards on a consteval `napi_free_supported<F>()` and binds a stub that throws on call instead, so the module still loads and every other function stays usable.
- **Template-specialization type names** — `class_name<T>()` no longer hard-errors when a bound free function takes or returns a template specialization (e.g. `pmp::Matrix<float, 3, 1>`, `Eigen::SparseMatrix<double>`); it falls back to the full display spelling when the type has no plain identifier.
- **Operator / conversion members** — the member walk skips functions without an identifier (`operator==`, `operator[]`, `operator T`), which can't be bound by name to a target language and previously broke generation for such classes.

### Docs
- Documented `user_sources`, multiple `user_include` directories, and added an initial `docs/MANIFEST.md` reference for the manifest file.
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
