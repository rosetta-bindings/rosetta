# Annotations — the rosetta metadata reference

<p align="center">
  <img src="/media/qt-qml-imgui.jpg" alt="Annotations" width="800">
</p>

Annotations are rosetta's **opt-in enrichment layer**: small, declarative tags attached to your fields and methods that backends turn into docstrings, validation, and UI. They are never required — reflection alone binds the whole class — and they never change the C++ behaviour of your types. You add them exactly where you want:

1. **Documentation** — `doc` becomes docstrings, reference pages, tooltips.
2. **Validation** — `range` rejects bad assignments at the binding boundary.
3. **UI** — `label`, `button`, `combobox` and the `widget::*` hints drive the
   generated Qt / QML / Dear ImGui inspectors.

Every annotation can be written **inline** (P2996/P3394 attributes in the header) or **out of line** (a JSON side-car; the header stays plain C++) — the two forms produce identical bindings and can be mixed.

---

## Where it fits

```
your header            [[= rosetta::range{0, 200} ]] int maxIter;   (inline)
  — or —
Type.ann.json          { "maxIter": { "range": [0, 200] } }         (side-car)
        │
        ▼
reflection walk  ──►  neutral IR (GenField.range, .doc, …)  ──►  every backend
                                                                 python: validating setter
                                                                 imgui:  clamping slider
                                                                 openapi/markdown: documented bound
```

Annotations are read **once**, on the generation host, and merged (inline first, then JSON) into the same annotation list — backends cannot tell the two sources apart.

---

## Minimal example

```cpp
#include <rosetta/annotations.h>

class Algo {
public:
    [[= rosetta::doc{"Max solver iterations"}, = rosetta::range{0, 200},
       = rosetta::widget::slider, = rosetta::label{"Max iter"} ]]
    int maxIter{100};

    [[= rosetta::doc{"Run the solver"}, = rosetta::button{"Run"} ]]
    double run();
};
```

…or the same thing with a clean header and a side-car (see [OUT_OF_LINE_ANNOTATIONS](OUT_OF_LINE_ANNOTATIONS.md)):

```json
{
  "maxIter": { "doc": "Max solver iterations", "range": [0, 200],
               "widget": "slider", "label": "Max iter" },
  "run":     { "doc": "Run the solver", "button": "Run" }
}
```

---

## The annotation set

| Annotation | Applies to | Inline spelling | Side-car key | Meaning |
|---|---|---|---|---|
| `doc` | fields, methods, classes* | `= rosetta::doc{"…"}` | `"doc": "…"` | Description text. Docstrings (Python), reference pages (markdown/html), API descriptions (OpenAPI), "(?)" hover tooltips (Qt/QML/ImGui). |
| `range` | numeric fields | `= rosetta::range{lo, hi}` | `"range": [lo, hi]` | Value bounds. Compiled backends emit a **validating setter** (assignment outside the range throws); UI inspectors render a **clamping slider** (or validate on commit); doc backends print the bound. Scientific notation is fine (`[1e-10, 1e-6]`). |
| `readonly` | fields | `= rosetta::readonly` | `"readonly": true` | Getter-only property; writes are rejected per backend; UI editors grey out. |
| `combobox` | string fields | `= rosetta::combobox{{"a", "b"}}` | `"combobox": ["a", "b"]` | Allowed choices (max 16), **enforced**: any other value is rejected by the validating setters and the dynamic core. UI renders a drop-down (or radio group, see `widget: radio`). |
| `label` | fields, methods | `= rosetta::label{"…"}` | `"label": "…"` | Display-name override in UI backends. The C++ identifier stays the lookup/binding key — only the visible text changes. |
| `button` | methods | `= rosetta::button{"Run"}` | `"button": "Run"` | Caption of the call button a UI backend renders for the method (default: the method name). |
| `widget::*` | fields | `= rosetta::widget::slider` | `"widget": "slider"` | UI editor hint — see the table below. |

\* class-level and free-function `doc` come from the **manifest** (`"doc"` on the class / function entry), since free functions carry no in-source annotations.

### Semantics, validation, behaviour, lifecycle, layout

| Annotation | Applies to | Inline spelling | Side-car key | Meaning |
|---|---|---|---|---|
| `unit` | numeric fields, methods (their result) | `= rosetta::unit{"m"}` | `"unit": "m"` | Physical unit. A suffix in UI labels, a line in docstrings, `x-unit` in OpenAPI, `unit` in an MCP `describe_class`. |
| `default_value` | methods (names one parameter) | `= rosetta::default_value{"dt", 0.001}` | `"default": {"dt": 0.001}` | The parameter's default — C++ cannot reflect a default argument's value, so state it here. Every backend that renders defaults uses it (keyword defaults in Python, `x?` in TypeScript, OpenAPI `default`), and the dynamic core / MCP server let the caller omit it. Numbers, booleans and strings (`default_value{"mode", "fast"}`). Repeat for several parameters. |
| `example` | fields, methods | `= rosetta::example{"{\"seconds\": 2}"}` | `"example": {"seconds": 2}` | A sample value or call, as JSON. OpenAPI `examples`, TSDoc `@example`, docstrings, MCP. |
| `min` / `max` | numeric fields | `= rosetta::min{0}` | `"min": 0` | One-sided bounds (inclusive), **enforced** like `range`. |
| `length` | string / vector fields | `= rosetta::length{1, 64}` | `"length": [1, 64]` | Allowed size, **enforced** (vectors: by the dynamic core). OpenAPI `minLength`/`maxLength` (`minItems`/`maxItems`). |
| `regex` | string fields | `= rosetta::regex{"[a-z]+"}` | `"regex": "[a-z]+"` | ECMAScript pattern the whole value must match, **enforced**. OpenAPI `pattern`. |
| `requires_` | fields, methods | `= rosetta::requires_{"dt < period() / 20", "message"}` | `"requires": "expr"` or `["expr", "message"]` | A precondition, in the expression language below. On a field it is checked with the **new** value; on a method with the call's arguments. Enforced by the dynamic core (MCP server, scripts, dynamic UIs); rendered in every reference. Trailing `_` because `requires` is a C++ keyword. |
| `destructive` | methods | `= rosetta::destructive{}` | `"destructive": true` | Discards state. UIs ask before calling; an MCP client must pass `"confirm": true` (and is told to ask the user first). |
| `confirm` | methods | `= rosetta::confirm{"Discard?"}` | `"confirm": "Discard?"` | `destructive`, with the question to ask. |
| `expensive` | methods | `= rosetta::expensive{}` | `"expensive": true` | Takes noticeable time: UIs show a busy cursor; agents are told to batch such calls in a script. |
| `hidden` | fields, methods | `= rosetta::hidden{}` | `"hidden": true` | Not bound **anywhere** — public in C++ for ABI or friendship reasons, but not API. |
| `deprecated` | fields, methods, classes | `= rosetta::deprecated{"use foo()"}` | `"deprecated": "foo()"` or `true` | Still bound, flagged: "(deprecated)" in UI labels, `@deprecated` in TSDoc, `deprecated: true` in OpenAPI, a docstring line, MCP. |
| `since` | fields, methods, classes | `= rosetta::since{"1.2"}` | `"since": "1.2"` | The version that introduced the member. |
| `step` | numeric fields | `= rosetta::step{0.1}` | `"step": 0.1` | Spin-box increment / drag speed. |
| `decimals` | numeric fields | `= rosetta::decimals{3}` | `"decimals": 3` | Digits shown in UI editors. |
| `log_scale` | numeric fields with a positive `range` | `= rosetta::log_scale{}` | `"log_scale": true` | A slider moving along log10 of the value (a tolerance from `1e-10` to `1e-6`). |
| `group` | fields | `= rosetta::group{"Advanced"}` | `"group": "Advanced"` | Section of the property panel; a group's fields are kept together. |
| `order` | fields | `= rosetta::order{3}` | `"order": 3` | Position in the panel (lower first); unordered fields follow in declaration order. |
| `enabled_if` | fields | `= rosetta::enabled_if{"pinBoundary"}` | `"enabled_if": "pinBoundary"` | Editable only while the condition holds (expression language below). |
| `visible_if` | fields | `= rosetta::visible_if{"shading != Wireframe"}` | `"visible_if": "…"` | Shown only while the condition holds. |

### The expression language (`requires_`, `enabled_if`, `visible_if`)

Numbers, `"strings"`, `true` / `false`, identifiers, `name()` (a nullary method of the object), unary `-` and `!`, `* /`, `+ -`, `< <= > >= == !=`, `&&`, `||`, parentheses. An identifier is a parameter of the call (for a method's `requires_`), else a field of the object, else a bare word compared as a string — so an enum field reads `shading == Wireframe`. Enum values compare by enumerator name; a vector compares by its size.

The dynamic core evaluates them at run time (`rosetta::dyn::evaluate` / `condition`). The generated Qt and ImGui inspectors translate `enabled_if` / `visible_if` into C++ over the object at generation time (fields, nullary methods, enumerators); a condition they cannot translate is skipped — the field just stays enabled and visible. A condition that fails to evaluate counts as true, for the same reason: a broken hint must never lock a field for good.

### Widget hints

Hints pick the editor when more than one would fit the field's type. They are consumed by the three UI inspector backends (**Qt Widgets**, **QML**, **Dear ImGui**) and ignored everywhere else.

| Hint | Field type | Renders as |
|---|---|---|
| `spin` | number | spin box / typed input |
| `slider` | number + `range` | slider with the range as bounds (clamps by construction) |
| `textfield` | number | validated free-text input — the right editor for values a slider can't hit precisely (an eps of `1e-7`) |
| `checkbox` | number (0/1), bool | checkbox |
| `color` | string `"#rrggbb"` | color picker (ImGui `ColorEdit3`, Qt swatch + `QColorDialog`, QML `ColorDialog`); edits write back as hex |
| `multiline` | string | multi-line text area |
| `radio` | string + `combobox` | the choices as a radio-button group instead of a drop-down |
| `file` | string path | text entry + "…" browse button opening the platform file dialog (ImGui shells the native dialog: `osascript` on macOS, `zenity` on Linux) |
| `plot` | vector of numbers | a small line plot of the values, instead of a list |
| `progress` | number + `range` | a progress bar: a read-out (completion, residual), not an editor |

---

## Who consumes what

- **Every backend**: `hidden` (the member is never visited), and `default_value` wherever defaults are rendered.
- **Scripting backends** (python / nanobind / node / wasm / lua / julia, plus C# / Java): `doc` → docstrings where the framework has them; `range`, `min`, `max`, `combobox`, `length`, `regex` → **validating setters** in python, nanobind, lua, julia and wasm; `readonly` → getter-only properties. Python / nanobind docstrings also carry `unit`, the allowed values, `requires_`, `destructive`, `expensive`, `example`, `deprecated` and `since`. `label`, `button` and the layout / widget hints are ignored — they are UI concepts.
- **The dynamic core** (`dynamic`, `mcp`, and everything on top of them — the MCP server, `run_script` / `run_python`, dynamic UIs): enforces `range`, `readonly`, `combobox`, `min`, `max`, `length`, `regex` and `requires_`, fills defaults, and exposes every annotation as key/value metadata (`MetaField::annotation("unit")`). The MCP server reports them all in `describe_class` and refuses a `destructive` method without `"confirm": true`.
- **UI inspectors** (qt / qml / imgui, and the dynamic property panel of `examples/dynamic`): `doc` and the facts as tooltips, `unit` and `deprecated` in labels, `range` / `step` / `decimals` / `log_scale` in editors, `group` / `order` for layout, `enabled_if` / `visible_if`, `plot` / `progress`, a confirmation for `destructive`, a busy cursor for `expensive`, and an edit that breaks `min` / `max` / `length` / `regex` is refused. QML passes `unit` / `step` / `decimals` / `group` to its delegates and validates in its setters; conditions and layout are left to the QML side.
- **Document backends** (markdown / html / openapi / typescript): `doc` and every constraint and fact, as reference text, JSON Schema keywords (`minimum`, `maxLength`, `pattern`, `examples`, `deprecated`, `x-unit`) and TSDoc tags (`@example`, `@deprecated`, `@since`, `@remarks`).

An annotation a backend has no use for is simply ignored — never an error.

---

## Inline vs out of line

| | Inline `[[= …]]` | Side-car `.ann.json` |
|---|---|---|
| Header stays plain C++ | ✗ (needs the P2996 fork to *parse*) | ✅ |
| Generated binding builds on an off-the-shelf compiler | ✗ (the generated binding `#include`s the header) | ✅ |
| Third-party headers you can't edit | ✗ | ✅ |
| Expressiveness | full set | full set (parity) |

Wiring the side-car is one manifest field per class:

```json
"classes": [
  { "name": "Algo", "header": "Algo.h", "annotations": "Algo.ann.json" }
]
```

The side-car is **baked into `bindings.h` when `rosetta_gen` runs** — after editing the JSON, re-run `rosetta_gen` + the generator, not just the binding build. A JSON key that names no member of the class **fails the build** with a clear message (so a renamed field can't silently lose its metadata). Mechanics, the `ann_json_source<T>` customization point, and the merge rules are in [OUT_OF_LINE_ANNOTATIONS](OUT_OF_LINE_ANNOTATIONS.md).

---

## Internal annotations

`rosetta::virtual_spec` is **synthesized by the walk**, never written by users: it marks a method's surviving reflection as virtual / overriding so backends can emit overridable bindings (pybind11 / N-API trampolines). You only meet it when writing a custom backend.

---

## Common gotchas

- **Inline annotations make the header C++26-only.** Every TU that includes it — including the generated `-expanded` bindings — must build with the fork and `-fannotation-attributes`. Use the side-car when you want off-the-shelf builds (the [`examples/imgui`](../examples/imgui) / [`examples/geom-expanded`](../examples/geom-expanded) pattern).
- **Side-car edits need a regeneration**, not just a rebuild (the JSON is baked at `rosetta_gen` time).
- `widget::slider` without a `range` falls back to the default editor — a slider needs bounds.
- `widget::radio` does nothing without a `combobox` — it restyles the choices, it doesn't define them.
- `combobox` is capped at 16 choices; beyond that a drop-down is the wrong widget anyway.
- `default_value` names a parameter by its identifier: a parameter declared without a name (`void f(double)`) cannot be given one.
- `requires_` and the UI conditions are evaluated by the dynamic core; the compiled bindings (python, node, …) document a `requires_` but do not check it.
- `hidden` removes a member from every binding, including the coverage report — if it should merely be read-only, use `readonly`.
- `label` changes only the visible text: scripting backends still bind the C++ identifier.

For annotation ideas that are *not* implemented yet (aliases, serialization shape, REST routing, …), see [OTHER_ANNOTATIONS](OTHER_ANNOTATIONS.md).
