# Out-of-line annotations (JSON side-car)

Keep your headers clean: instead of writing P3394 attributes inline, describe the same metadata (`doc`, `range`, `readonly`, `combobox`) in a JSON side-car that is baked into the program at **compile time**, parsed by a small `consteval` parser, and **merged** with whatever inline annotations the member already has.

- No side-car for a class → the class is simply un-annotated (no error).
- Inline annotations present → the JSON entries are **concatenated** with them.

Nothing here turns JSON into real P3394 annotations — that would need token injection (P3294), which clang-p2996 lacks. The parsed entries instead join the annotation pack at the single choke point in `rosetta::walk()`, so every existing backend (`docgen`, REST, OpenAPI, …) sees them with no change.

## Usage — declare it in the manifest

Add an `annotations` field to a class entry. `rosetta_gen` reads the side-car and bakes an `ann_json_source<T>` specialization straight into the generated `bindings.h` — so your header and source stay completely free of annotation wiring.

`manifest.json`:

```json
{
  "user_include": ".",
  "rosetta_include": "../../include",
  "targets": [ "markdown" ],
  "classes": [
    { "name": "Widget", "header": "widget.h", "annotations": "widget.ann.json" }
  ]
}
```

`widget.h` — a clean header that never mentions rosetta:

```cpp
#pragma once
#include <string>
struct Widget {
    std::string title;
    int         count = 0;
    int         id    = 0;
    std::string mode;
};
```

`widget.ann.json` — keys are member identifiers (fields **or methods**); each value object may carry any of `doc` (string), `range` (`[lo, hi]`, scientific notation ok), `readonly` (bool), `combobox` (string array), `label` (display-name override, string), `button` (call-button caption for a method, string), `widget` (UI hint: `"slider"`, `"spin"`, `"checkbox"`, `"textfield"`, `"color"`, `"multiline"`, `"radio"` or `"file"`) — the full inline annotation set:

```json
{
  "title": { "doc": "The widget title", "label": "Title" },
  "count": { "doc": "Visible items", "range": [0, 100], "widget": "slider" },
  "id":    { "readonly": true },
  "mode":  { "combobox": ["fast", "slow"] },
  "run":   { "doc": "Run the thing", "button": "Run" }
}
```

That is the whole surface: run `rosetta_gen manifest.json gen`, build, and every backend sees the annotations. The `annotations` path is resolved relative to the manifest.

## Or: attach the JSON directly to the type

The manifest's `annotations` field is just sugar for specializing the
`rosetta::ann_json_source<T>` customization point. You can do that yourself, with
the JSON inline as a string literal — attached to the type from **anywhere**
(another file, a TU, after the class), so the header still stays clean:

```cpp
#include "widget.h"            // the untouched class
#include <rosetta/annotate.h>  // for the customization point

template <>
constexpr std::string_view rosetta::ann_json_source<Widget> = R"({
  "title": { "doc": "The widget title" },
  "count": { "doc": "Visible items", "range": [0, 100] },
  "id":    { "readonly": true },
  "mode":  { "combobox": ["fast", "slow"] }
})";
```

Same schema, same merge with inline annotations, same effect on every backend.
The only rule is that this specialization must be visible (i.e. `#include`d)
before `rosetta::walk<Widget>()` runs in a translation unit — which is exactly
why the manifest route bakes it into `bindings.h`. This is also how the unit
test wires it up (see `tests/annotate_json.cpp`).

## How it works

`rosetta_gen` emits, into `bindings.h` (right after the class header is included, so `T` is complete):

```cpp
template <> inline constexpr auto rosetta::detail::ann_storage<Widget> =
    std::to_array<char>({ char(0x7b), /* …baked JSON bytes… */ '\0' });
template <> inline constexpr std::string_view rosetta::ann_json_source<Widget> =
    std::string_view{ rosetta::detail::ann_storage<Widget>.data(),
                      rosetta::detail::ann_storage<Widget>.size() - 1 };
static_assert(rosetta::detail::ann_keys_error<Widget>().empty(),
              rosetta::detail::ann_keys_error<Widget>());
```

The bytes are baked at generation time (no `#embed`, no compiler flags, works on any C++26 compiler). `bindings.h` is the single TU where `rosetta::walk<Widget>()` runs, so the specialization is visible before first use — exactly what a variable-template explicit specialization needs to avoid an ODR clash with the empty primary.

At `walk()` time, `rosetta::detail::merged_annotations<T>(member)` concatenates the member's inline P3394 annotations with the parsed JSON entries for that member name, and hands the combined pack to the visitor. Nothing downstream changes.

### Every backend, not just markdown

Backends that render from the reflected data at generation time — markdown, OpenAPI, JSON, TypeScript, REST — see the merged annotations directly (e.g. the OpenAPI schema gets `description`/`minimum`/`maximum`/`readOnly`/`enum`).

The **python** and **node** backends are different: they emit a small module that is *compiled separately* and re-reflects the type at its own compile time. That TU does not include `bindings.h`, so the generator re-emits the same `ann_json_source<T>` specialization into the module's source (see `includes_of()` in `inline/generate.hxx`) right after the class header. The result: the compiled pybind/N-API module carries the side-car metadata too — a `doc` becomes a docstring, `readonly` makes the property read-only, `range` validates assignments, `combobox` constrains values. In other words, the side-car behaves exactly like inline annotations for **all** backends.

The manifest is the single entry point: the `annotations` field is the only wiring, and a class with no `annotations` field uses the empty primary template (un-annotated). There is no separate per-class macro, include, or CMake call.

## Guarantees

- **No side-car** → the primary `ann_json_source<T>` (empty) is used, so the class is un-annotated; no special-casing needed.
- **Staleness guard**: a `static_assert` checks every JSON key names a real member of the type, so a renamed/typo'd field fails the build (naming the offending key) instead of silently dropping metadata.
- **Precedence**: inline annotations come first in the merged pack, JSON after — so `ann::get_or<A>` (which keeps the last match) lets a JSON entry override an inline one of the same kind, while cumulative readers (e.g. `doc`) see both.

## Why baking, not runtime

Loading the JSON at **runtime** (`ifstream`, `objcopy`/`.incbin` symbol, …) cannot work: annotations are **NTTPs** consumed at compile time in `walk()`, so the data must exist during constant evaluation. Runtime would lose the merge, the NTTP splice, and the staleness `static_assert`. Baking the bytes into `bindings.h` keeps everything in the one constant-evaluated pass.

## Which C++26 features carried this

P2996 reflection (the merge + member validation), P2741 constexpr `static_assert` messages (name the offending key), variable templates keyed by type (storage association), and `std::to_array` / `define_static_string` / `reflect_constant` (plumbing).

## Limitations (prototype)

- The parser takes string values literally (no escape unescaping) and parses numbers as plain decimals (no exponents) — enough for the annotation schema, not a general JSON library.
- Only the four core annotation kinds are mapped. Adding a kind = one `else if` in `json_annotations_for` (see `include/rosetta/annotate.h`).

A complete, runnable example is in [`examples/annotate-manifest`](../examples/annotate-manifest) — clean header, external `widget.ann.json`, one manifest field. See also `tests/annotate_json.cpp` for the customization point the manifest field feeds.
