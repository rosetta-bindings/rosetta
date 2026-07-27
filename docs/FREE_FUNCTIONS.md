In C++26 (P2996), there's no direct "give me all free functions" call, but you can reach them by reflecting a namespace and filtering. The two gotchas: ^^foo is ill-formed when foo is overloaded, and function templates need substitute before you can call them.

## Basic pattern

```cpp
#include <experimental/meta>
namespace meta = std::meta;

namespace api {
    intadd(int a, int b){ return a + b; } 
    void greet(std::string_view s){ /* ... */ } 
} 

// Enumerate free functions in a namespace
constexpr auto fns = []{
    std::vector<meta::info> out;
    for (meta::info r : meta::members_of(^^api)) {
        if (meta::is_function(r)) out.push_back(r); 
        return out;
    }
}();
```

## What you can ask each reflection

```cpp
template <meta::info R> 
void describe() {
    constexpr auto name = meta::identifier_of(R); // "add"
    constexpr auto T= meta::type_of(R); // function type
    constexpr auto ret= meta::return_type_of(T);// ^^int
    constexpr auto ps = meta::parameters_of(R); // span<info> 
    // meta::display_string_of(R) gives a human-readable form 
}
```

## Calling a reflected function

Splice it back into an expression with `[: :]`:

```cpp
constexpr meta::info f = ^^api::add;
int x = [:f:](2, 3);// direct splice-call
int y = meta::reflect_invoke(f, {^^2, ^^3});// constexpr-only path
```

## Overloads

`^^api::add` fails if add is overloaded. Resolve by signature first:

```cpp
using sig = int(int,int);
constexpr meta::info f = ^^static_cast<sig*>(&api::add);
```

…or walk members_of and match on type_of.

## Function templates

```cpp
constexpr meta::info tpl= ^^my_template; // the template itself
constexpr meta::info inst = meta::substitute(tpl, {^^int});// my_template<int>
[:inst:](42);
```

## Typical use cases this unlocks

- Auto-generating RPC/CLI dispatch tables from a namespace's contents
- Building test runners that find every test_* function
- Producing serializers/bindings without macros

## How rosetta exposes free functions

Free functions are declared in the **manifest** — never by editing the user's headers, and with no third-party dependency beyond P2996 reflection:

```json
"functions": [
  { "name": "transform", "header": "common.h", "doc": "Scale a point" }
]
```

`name` may be qualified (`api::add`). For each entry `rosetta_gen` emits, into
the generated driver:

```cpp
opt.functions = {
    rosetta::make_function<^^transform>("transform", "common.h", "Scale a point", ""),
};
```

`make_function<^^fn>` reflects the function once (return type, parameters) into a language-neutral `GenFunction`; `name` becomes the exposed binding name and the qualified spelling is what each backend emits for the function pointer. Every backend then renders it:

| Backend    | Output                                            |
|------------|---------------------------------------------------|
| pybind     | `m.def("transform", &transform, "doc")`           |
| embind     | `emscripten::function("transform", &transform)`   |
| N-API      | `rosetta::bind_napi_function<^^transform>(env, …)` |
| REST       | `POST /transform` (JSON-array args → JSON result) |
| TypeScript | `export function transform(arg0: Point): Point;`  |
| Markdown   | a `## Functions` section                          |

### Renaming a function (`expose`)

A function binds under one module-level name — its reflected identifier, unless
the manifest entry overrides it:

```json
"functions": [
  { "name": "arch::solve",       "header": "slip.h" },
  { "name": "arch::sinv::solve", "header": "stress.h", "expose": "solve_stress" }
]
```

That fourth `make_function` argument carries it (`…, "Scale a point", "solve_stress"`),
so only `GenFunction::name` changes — every backend keeps emitting the *qualified*
spelling for the function pointer, and the rename therefore works in all of them.
Classes and free functions share the module namespace, so `rosetta_gen` rejects a
manifest where two of them resolve to the same name. The same field renames an
[extension method](MANIFEST.md#extension-methods-extensions) on its class.

Caveats inherited from the reflection model:
- **Overloads**: an overloaded `name` makes `^^name` ill-formed; bind a specific signature, or skip. N-API/REST dispatch is arity-only anyway.
- **Function templates** can't be bound without instantiation arguments.
- **REST** skips a function whose parameter/return types aren't JSON-(de)serializable (e.g. user class types), mirroring how it skips such methods.
- **doc** comes from the manifest, since the user's headers carry no annotations.
- 