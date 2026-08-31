# The steps, and why there are that many

A short note on why binding a library with rosetta is a *pipeline* rather than a command, what each step does, and where your own library enters it.

## The constraint everything follows from

C++26 reflection answers questions **at the compile time of a program that includes your headers**. It cannot be queried from a script, a config file or a build system — only from C++, being compiled, with your types in scope.

So to learn the shape of your types, something has to **compile**. To turn those answers into files on disk, that something has to **run**. Compile-then-run is not an implementation detail of rosetta; it is the only shape this can have. The steps below are that sequence, plus one step before it and one after.

## Step 0 — build `rosetta_gen`

**What it is.** A JSON-to-C++ text templater. It does no reflection at all and builds with an off-the-shelf C++17 compiler.

**Why it exists.** So that what you write is a *manifest*, not a driver program. Given `manifest.json` it emits three files — `<name>.cpp` (a `main()` that reflects), `bindings.h` (your types, plus any out-of-line annotations, baked in) and a `CMakeLists.txt` that knows where the reflection toolchain is.

**Your library:** untouched. `rosetta_gen` never opens your headers; it only copies their names into the driver it writes.

## Step 1 — compile and run the driver

Three commands, because "compile then run" is three things: 
1. **emit** the program
2. **build** it (**reflection** happens HERE)
3. **execute** it

```
rosetta_gen manifest.json gen     # emit the driver source
cmake -S gen -B gen/build         # COMPILE it — reflection happens HERE
cmake --build gen/build
./generator bindings              # RUN it — writes one project per target
```

**What actually happens.** During the *compile*, clang-p2996 evaluates the reflection operators in the driver: it walks your classes, enumerates fields and methods, reads annotations, and resolves each member against the target language's type gate. Every decision about what can and cannot be bound is made at this moment. During the *run*, the driver does nothing clever — it prints the conclusions it was compiled with.

**Why neither half can be skipped.** A compiler cannot write your binding files; a program cannot see types it was not compiled against. You need both.

**Your library:** read for the **first** time, as source, by the C++26 toolchain.

## Step 2 — what lands in `bindings/`

One self-contained project per target, plus `coverage.json` — a machine-readable account of what bound and what did not, available *before* anything is compiled.

The generated code is **fully expanded**: explicit pybind11 / N-API / sol2 / embind calls, or (for the `dynamic` target) aggregate-initialised tables. No splices, no `<experimental/meta>`, no trace that reflection was ever involved.

**Why it matters.** The expansion happened once, on a host with the fork. The machine that *builds* the binding never needs reflection — an off-the-shelf Clang, GCC, MSVC or emsdk is enough. That is what makes the generated tree shippable.

## Step 3 — compile a backend

Ordinary CMake (or npm, or emcmake) over an ordinary C++ project.

**Your library:** read a **second** time — the generated binding `#include`s your headers and calls your functions, which is where `user_sources` / `user_lib` are compiled or linked in.

## Step 4 — load it

`import`, `require`, `dlopen`. Nothing rosetta-specific left.

## Your library is read twice, for different reasons

| pass | by | to | needs C++26 |
|---|---|---|---|
| step 1 | the reflection driver | **describe** your types | yes |
| step 3 | the generated binding | **call** them | no |

The one consequence worth remembering: annotations written **inline** (`[[ = rosetta::doc{...} ]]`) pull `<experimental/meta>` into your header, so pass 2 needs the C++26 toolchain as well and the "no reflection on the target" promise is lost. Out-of-line annotations (a `.ann.json` side-car) keep your header plain C++ and pass 2 reflection-free. See [OUT_OF_LINE_ANNOTATIONS.md](OUT_OF_LINE_ANNOTATIONS.md).

## The `dynamic` backend — the IR as data

Every other backend spends the reflection result on *framework calls*: `py::class_<Mesh>().def("at", …)`, an N-API property descriptor, a sol2 usertype. The information is consumed at generation time and what survives is code for one specific runtime.

The `dynamic` backend spends it on **data instead**. It writes the IR back out as static tables, so the answer to "what does this class have" is still available at run time, to a caller that was never compiled against your headers.

### What it emits

```
bindings/dynamic/auto_dynamic.h     declares <lib>::register_all()   — idempotent
bindings/dynamic/auto_dynamic.cpp   the tables
bindings/dynamic/inspect.cpp        a registry walker, for free
bindings/dynamic/CMakeLists.txt     a static library, <lib>_dynamic
```

`auto_dynamic.cpp` is the only file that includes your header. It is ordinary C++20: the metadata is data, so nothing here needs reflection to compile.

### What the tables look like

One `TypeDesc` per distinct type, shared by address; one `MetaField` / `MetaMethod` / `MetaCtor` array per class; one `MetaClass` tying them together. Straight from the generated output:

```cpp
TypeDesc td_0{.kind = Kind::number, .spelling = "double", .integral = false};
TypeDesc td_6{.kind = Kind::vector, .spelling = "std::vector<double>", .element = &td_0};

const MetaField k_scene__Vec3_fields[] = {
    {.name = "x", .type = &td_0, .doc = "X component",
     .annotations = k_scene__Vec3_x_anns, .n_annotations = 2,
     .get = +[](const ObjectRef &self, const ArgList &) {
         return make_any(&td_0, static_cast<scene::Vec3 *>(self.ptr)->x);
     },
     .set = +[](const ObjectRef &self, const ArgList &a) {
         static_cast<scene::Vec3 *>(self.ptr)->x = value_cast<double>(a[0]);
         return Any::none();
     }},
```

Three properties of that shape are deliberate:

- **Aggregate initialisers of trivially-constructible data** — `const char *`, pointer-and-count spans, function pointers. No `std::string`, no `std::vector`, no dynamic initialisation. A 500-class library's metadata lands in `.rodata`, not in a static constructor that runs at load.
- **One captureless lambda per member**, decaying to a plain function pointer: `using Thunk = Any (*)(const ObjectRef &self, const ArgList &args)`. That is the *only* callable shape in the model — a field getter, a field setter, an instance method, a static method and a free function all reduce to it. `self` is the receiver, default-constructed when there isn't one.
- **`TypeDesc` is structural, not an identity.** `std::type_index` answers "are these the same type"; a UI and a marshaller need "what is *inside* it", so `element` recurses and `vector<vector<Foo>>` is describable.

### Why `register_all()` exists, and why it calls `link()`

Two classes can name each other's types, so `TypeDesc::cls` cannot be filled during static initialisation without an ordering you have no way to guarantee. The generated function adds every class, enum and function to the registry and then calls `link()` to resolve those back-pointers:

```cpp
void register_all(rosetta::dyn::Registry &r) {
    r.add_class(&k_scene__Vec3);
    r.add_class(&k_scene__Mesh);
    r.link();
}
```

It is idempotent, and it is the one line a consumer must run before querying anything.

### Calling by name

`Any` is the value type crossing the boundary, and its **canonical representation** is what makes a single generic wrapper possible: whatever the C++ said, the box holds exactly one of `bool`, `long long`, `double`, `std::string`, `std::vector<Any>` or an `ObjectRef`. So a caller reads a number with `as_number()` without caring whether the signature said `int`, `size_t` or `uint32_t` — and changing one for the other in your library is not a break for the wrapper.

`resolve()` scores every candidate of a name against the actual arguments on a deliberately small ladder — `exact` / `promote` / `convert` / `none` — rather than reproducing C++'s conversion lattice, because the question from a dynamically typed host is "did the user mean this overload", not "which standard conversion sequence is shorter". An equal best score is reported as *ambiguous* rather than silently picked, and a failure names every candidate and why it lost:

```
Mesh::at(std::string) matched no overload of 2:
  at(int) — argument 1 ("nope") is not a int
  at(int, int) — takes 2 argument(s), 1 given
```

Two more consequences worth knowing:

- **What cannot be marshalled is described, not deleted.** A member whose type no gate claimed keeps its entry with a null thunk and a stated reason (`.skip_reason = "callback: a std::function parameter needs a foreign-callable adapter"`), so a UI can grey it out instead of the member silently vanishing.
- **Ownership is explicit.** `ObjectRef::owner` is non-null when the reference owns the object. A getter returning `T&` can hand back a *borrowed* handle carrying the parent's owner — which pins the parent and closes the dangling-sub-object hole that a raw-pointer return has.

### What it costs

Every access is a linear name lookup plus a scoring pass. That is the right price for menus, property panels, commands and scripting glue; it is the wrong price for bulk data — pulling a mesh across boxes every coordinate into an `Any`. Cache the resolved `MetaMethod` (the pointer is stable for the process) and keep an expanded binding for hot paths.

## The bound reflection API — rosetta pointed at itself

The `dynamic` backend makes the metadata available **to C++**. Every consumer of it in this repo is C++: [`examples/dynamic/interp.h`](../examples/dynamic/interp.h), the Qt viewer, `runtime/imgui.h`, `visitors/qml_reflected_object.h` — hand-written walkers, one per toolkit.

The last step is to bind *that API* through rosetta's own backends, so the walker can be written in Python, Lua or JS. This is the Graphite/GOM move: expose the object model itself to the scripting layer, and the GUI becomes a script instead of a backend.

### Why a facade rather than binding `dynamic.h` directly

`rosetta::dyn` cannot be fed to the backends as-is. Three shapes block it:

| in `dynamic.h` | why it blocks | in `script.h` |
|---|---|---|
| `MetaField *fields; size_t n_fields` | pointer-and-count, so vector marshalling never fires | `std::vector<FieldInfo> fields()` |
| `const MetaClass *`, `const TypeDesc *` | raw pointer to an unbound type — the gate skips it | value-semantics handles |
| `using Thunk = Any (*)(…)` | function pointers marshal nowhere | ordinary `get` / `set` / `call` |

[`<rosetta/script.h>`](../include/rosetta/script.h) is that reshaping and nothing more. Each handle wraps **one `const` pointer** into the tables, so binding it copies no metadata — the `.rodata` guarantee survives. Nothing is named `Class` / `Type` / `Object` / `Method`, because those collide in the target languages (`java.lang.Class`, JS `Object`, Lua's `type`); hence `ClassInfo`, `TypeInfo`, `Instance`, `MethodInfo`.

Errors come back as `Outcome` (ok + text), never as an exception, for the reason `dyn::Result` states: unwinding through a foreign VM's frames is how you get a crash instead of a `TypeError`.

### The one hand-written piece

`Value` is type erasure — one C++ type standing for "whatever the host language just handed us". Reflection can *describe* it but cannot know that a Python `float`, a Lua number and a JS `Number` should all become the same box. That mapping is per-language knowledge, so it is written per language in [`runtime/script/{python,lua,node,wasm}.h`](../include/rosetta/runtime/script).

Only half of each caster is per-language, though. **Reading** a host value is irreducible: only Python knows what `PyFloat_Check` means. **Writing** one is the same shape everywhere, so it lives once in `rosetta::script::visit()`, and each caster supplies a sink of nine small methods (`on_none`, `on_bool`, `on_int`, `on_real`, `on_string`, `on_enum`, `on_list`, `on_object`, `on_unknown`). No two casters can drift apart on whether an enum crosses as its integer, or on what an unclaimed type renders as.

The casters reach the generated code through `module_init.headers`, which is the only hook that emits an `#include` **after** the framework header and **before** the bound classes — exactly the window a type caster needs. Because that list is global rather than per-target, each file guards its whole body on a macro only its own target defines.

One subtlety the type gate forces: `Value` must stay in the bound `classes` list. Bindability is decided at generation time and cannot know a caster will exist, so dropping `Value` would silently skip every member that mentions it — `Outcome::value`, `Instance::set`, `Instance::call`, all of it. The cost is one vestigial `Value` type per module that nothing ever produces.

### What a project has to write

Nothing, beyond where its own files are. The binding surface is fixed, so it ships as manifest data in [`include/rosetta/presets/scriptable.json`](../include/rosetta/presets/scriptable.json) and a manifest names it:

```json
{
    "preset": "scriptable",
    "rosetta_include": "../../include",
    "user_include": ["../lib", "../lib/bindings/dynamic"],
    "user_sources": ["../lib/bindings/dynamic/auto_dynamic.cpp"],
    "module_init": { "headers": ["auto_dynamic.h"], "statements": ["bank::register_all()"] },
    "targets": ["python", "lua", "node"]
}
```

Six keys, every one about *this* project. See [Presets](MANIFEST.md#presets-preset).

### What it buys

- **One wrapper per language instead of one per class.** Ship the module once; any library that has been through the `dynamic` backend becomes scriptable with no further codegen.
- **UI by query, not by codegen.** A property sheet is a walk over `fields()`: `doc` → tooltip, `range` → slider bounds, `combobox` / enumerators → drop-down, `readonly` → disabled row. The dispatch that `qt/propertypanel.h` spends C++ on is about ten lines of Python — editable without recompiling anything.
- **Overloads come back** for the name-keyed targets. Lua, Node, Wasm, C# and Java bind the first overload of a name and drop the siblings at generation time ([COVERAGE.md](COVERAGE.md)); through this module they do not, because `resolve()` scores the whole set at call time and can explain a failure.
- **Annotations are enforced once**, in the core, rather than re-implemented per backend — a `range` violation comes back as the same message in every language.

## Why some examples run the pipeline twice

[`examples/plain-binding`](../examples/plain-binding) runs it **once**: one library, one manifest, four language bindings. That is the base case, and it is what the rest of this document describes.


[`examples/scriptable-model`](../examples/scriptable-model) runs steps 1–2 once per *manifest*, and it has two:

1. **The user library → metadata.** The `dynamic` backend turns `scene.h` into `auto_dynamic.{h,cpp}`: the same information the other backends spend on framework calls, emitted as plain data instead.
2. **`<rosetta/script.h>` → bindings.** A second pipeline that binds rosetta's own reflection API — and takes stage 1's output as its `user_sources`.

Stage 1's *output* is stage 2's *input library*. That is the whole trick: by stage 2 your types are **data**, not C++, so stage 2's manifest never names one and the resulting module works for any library that has been through stage 1.
