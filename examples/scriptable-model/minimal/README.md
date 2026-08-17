# The smallest complete case

`..` binds the reflection API and drives it against `examples/dynamic`'s scene library. That example predates it, so it is easy to read the result as "this works because those two folders were built together".

This one starts from nothing: `lib/bank.h` is 25 lines of pure C++ that have never heard of rosetta — no annotations, no rosetta include, nothing to opt in.

```bash
./run.sh          # both stages, then the Python driver
./run.sh lua      # ...the Lua one
./run.sh node     # ...the Node one
./run.sh clean
```

## What a project actually supplies

**Three files.** Two of them are generated:

| file | where it comes from |
|---|---|
| `lib/bank.h` | yours — `auto_dynamic.cpp` `#include`s it |
| `lib/bindings/dynamic/auto_dynamic.h` | generated — declares `bank::register_all()` |
| `lib/bindings/dynamic/auto_dynamic.cpp` | generated — the tables |

Plus your `.cpp` / `.a` if the library is not header-only (`user_sources` or `user_lib`).

The two generated ones come from a six-line manifest with a single target ([`lib/manifest.json`](lib/manifest.json)):

```json
{
    "user_include": ".",
    "rosetta_include": "../../../../include",
    "generator_name": "bank_dynamic_gen",
    "module_name": "bank",
    "targets": [{ "lang": "dynamic", "name": "bank" }],
    "classes": [{ "name": "bank::Account", "header": "bank.h" }]
}
```

**Three manifest keys.** [`meta/manifest.json`](meta/manifest.json) is a complete manifest, and this is all of the project-specific part of it:

```json
"preset": "scriptable",
"user_include": ["../lib", "../lib/bindings/dynamic"],
"user_sources": ["../lib/bindings/dynamic/auto_dynamic.cpp"],
"module_init":  { "headers": ["auto_dynamic.h"], "statements": ["bank::register_all()"] }
```

`"preset": "scriptable"` supplies the binding surface — the twelve `rosetta::script` handle classes, the eight registry free functions, and the type casters — from [`include/rosetta/presets/scriptable.json`](../../../include/rosetta/presets/scriptable.json). It is library-independent, so it lives with the header it describes rather than being copied into every manifest. See [Presets](../../../docs/MANIFEST.md#presets-preset).

Nothing in `meta/` mentions `bank::Account` — not the manifest, not the generated code. The drivers name it once, as a string.

## What comes out

```
bank::Account
    owner    std::string
    balance  double
    frozen   bool
    deposit(double) -> void
    withdraw(double) -> bool
    describe() -> std::string
    open(std::string, double) -> bank::Account [static]

--- a live account ---
describe : ada: 100.000000
balance  : 150.0
withdraw : False (more than the balance)
frozen   : True -> deposit ignored, balance still 150.0

--- a form, built by query ---
    owner    text      = 'ada'
    balance  spinbox   = 150.0
    frozen   checkbox  = True
```

Both `python` and `lua` build; `deposit(50)` and `withdraw(500)` are real calls into real C++, with native values crossing in both directions thanks to [`rosetta/runtime/script_casters.h`](../../../include/rosetta/runtime/script_casters.h).

## Why bank.h has no annotations

Deliberate, and worth knowing before you try this on your own header. `auto_dynamic.cpp` `#include`s your header, so a header carrying **inline** annotations drags `<experimental/meta>` into it:

```
rosetta/annotations.h:16: fatal error: 'experimental/meta' file not found
```

The reflection-free promise covers the *generated* code, not a bound header that itself requires reflection. If you want annotations *and* a stock-compiler target build, put them **out of line** in a `.ann.json` side-car — which is exactly what `examples/dynamic/scene.h` does with `Mesh.ann.json` and `Vec3.ann.json`, and why `..` gets ranges, choices and tooltips while this example gets none.
