# Annotations — the rosetta metadata reference

<p align="center">
  <img src="/media/qt-qml-imgui.jpg" alt="Annotations" width="800">
</p>

Annotations are rosetta's **opt-in enrichment layer**: small, declarative tags attached to your fields and methods that backends turn into docstrings, validation, and UI. They are never required — reflection alone binds the whole class — and they never change the C++ behaviour of your types. You add them exactly where you want:

1. **Documentation** — `doc` becomes docstrings, reference pages, tooltips.
2. **Validation** — `range` rejects bad assignments at the binding boundary.
3. **UI** — `label`, `button`, `combobox` and the `widget::*` hints drive the
   generated Qt / QML / Dear ImGui inspectors.

Every annotation can be written **inline** (P2996/P3394 attributes in the header) or **out of line** (a JSON side-car; the header stays stock C++) — the two forms produce identical bindings and can be mixed.

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
| `combobox` | string fields | `= rosetta::combobox{{"a", "b"}}` | `"combobox": ["a", "b"]` | Allowed choices (max 16). UI renders a drop-down (or radio group, see `widget: radio`). |
| `label` | fields, methods | `= rosetta::label{"…"}` | `"label": "…"` | Display-name override in UI backends. The C++ identifier stays the lookup/binding key — only the visible text changes. |
| `button` | methods | `= rosetta::button{"Run"}` | `"button": "Run"` | Caption of the call button a UI backend renders for the method (default: the method name). |
| `widget::*` | fields | `= rosetta::widget::slider` | `"widget": "slider"` | UI editor hint — see the table below. |

\* class-level and free-function `doc` come from the **manifest** (`"doc"` on the class / function entry), since free functions carry no in-source annotations.

### Widget hints

Hints pick the editor when more than one would fit the field's type. They are consumed by the three UI inspector backends (**Qt Widgets** thin + expanded, **QML** thin + expanded, **Dear ImGui**) and ignored everywhere else.

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

---

## Who consumes what

- **Scripting backends** (python / nanobind / node / wasm / lua / julia, thin and expanded alike, plus C# / Java): `doc` → docstrings where the framework has them; `range` → validating setters; `readonly` → getter-only properties. `label`, `button` and `widget::*` are ignored — they are UI concepts.
- **UI inspectors** (qt / qml / imgui): everything — `doc` as tooltips, `range` as slider bounds / commit validation, `readonly` as disabled editors, `combobox` as drop-downs, plus `label` / `button` / `widget::*`.
- **Document backends** (markdown / html / openapi / typescript): `doc` and the constraints (`range`, `readonly`, `combobox`) are rendered into the reference text / schema.

An annotation a backend has no use for is simply ignored — never an error.

---

## Inline vs out of line

| | Inline `[[= …]]` | Side-car `.ann.json` |
|---|---|---|
| Header stays stock C++ | ✗ (needs the P2996 fork to *parse*) | ✅ |
| Works with `-expanded` targets on a stock compiler | ✗ (the generated binding `#include`s the header) | ✅ |
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

- **Inline annotations make the header C++26-only.** Every TU that includes it — including the generated `-expanded` bindings — must build with the fork and `-fannotation-attributes`. Use the side-car when you want stock builds (the [`examples/imgui`](../examples/imgui) / [`examples/geom-expanded`](../examples/geom-expanded) pattern).
- **Side-car edits need a regeneration**, not just a rebuild (the JSON is baked at `rosetta_gen` time).
- `widget::slider` without a `range` falls back to the default editor — a slider needs bounds.
- `widget::radio` does nothing without a `combobox` — it restyles the choices, it doesn't define them.
- `combobox` is capped at 16 choices; beyond that a drop-down is the wrong widget anyway.
- `label` changes only the visible text: scripting backends still bind the C++ identifier.

For annotation ideas that are *not* implemented yet (aliases, units, serialization shape, lifecycle…), see [OTHER_ANNOTATIONS](OTHER_ANNOTATIONS.md).
