# Auto-QML example

A Qt Quick inspector window driven by **one** `rosetta::walk` over the
annotated `Person` struct from `../bindings/person.h`. The C++ side
emits metadata for each field and method into a generic
`ReflectedObject`; the QML side renders the UI from that metadata —
there is no hand-written, per-`Person` markup in the generic
`Inspector.qml` (now shipped from `rosetta/visitors/qml/`).

```
┌──────────────────────────────────────────────────────┐
│ Person — generated from one C++26 reflection walk    │
├──────────────────────────────────────────────────────┤
│ FIELDS                                               │
│   name              [Alice           ]               │
│   age [0..150]      [42 ▲▼           ]               │
│   id [ro]           [s-123           ]               │
├──────────────────────────────────────────────────────┤
│ METHODS                                              │
│   greet(1)          [Hello   ]            Call       │
│   clear(0)                                Call       │
├──────────────────────────────────────────────────────┤
│  log...                                              │
└──────────────────────────────────────────────────────┘
```

Each editor commits to the bound struct on its natural signal — Enter or
focus loss for TextField, `valueModified` for SpinBox, `toggled` for
CheckBox, `activated` for ComboBox, release for Slider. There are no
GET / PUT buttons.

Annotation behaviour mirrors the python / rest backends:

| Annotation       | Effect on the UI                                        |
|------------------|---------------------------------------------------------|
| `doc{...}`       | tooltip on the field / method label                     |
| `readonly`       | editor disabled, `[ro]` suffix on the label             |
| `range{lo,hi}`   | shown next to label; invalid values logged in red       |
| `combobox{{...}}`| editor becomes a drop-down; off-list commits logged red |
| `widget::slider` | numeric editor becomes a Slider instead of a SpinBox    |
| `widget::checkbox`| renders the field as a CheckBox (useful for int 0/1)   |
| `widget::textfield`| renders the field as a TextField with range validator |
| `button{"label"}` | overrides the action button text on a method row        |

## Build

```bash
cmake -G Ninja -B build
cmake --build build
./build/auto_qml
```

The CMake script defaults to:

- `CLANG_P2996_ROOT = $HOME/devs/c++/clang-p2996/build`
- `QT_DIR          = $HOME/Qt/6.5.3/macos`

Override either with `-D<name>=...` on the cmake line.

## What's where

The generic backend now lives in the rosetta include tree, so a new QML
example only needs its own `main.cpp` + `CMakeLists.txt`:

| File                                          | Role                                                          |
|-----------------------------------------------|---------------------------------------------------------------|
| `rosetta/visitors/qml.h` (+ inline)   | `rosetta::QmlVisitor<T>` + `bind_qml<T>` entry point          |
| `rosetta/visitors/qml_reflected_object.h`     | The `Q_OBJECT` adapter exposed to QML (no template parameter) |
| `rosetta/visitors/qml/Inspector.qml`          | Generic inspector — one `Repeater` over fields / methods      |
| `main.cpp`                                    | Walks `Person`, registers the wrapper, launches QML           |
| `CMakeLists.txt`                              | Qt6 + clang-p2996 wiring                                      |

Two Qt-imposed wiring steps remain in each example's `CMakeLists.txt`
(they can't move into a header): list `qml_reflected_object.h` in
`qt_add_executable(...)` so AUTOMOC mocs it, and add `Inspector.qml` to
`qt_add_qml_module(QML_FILES ...)`. See this folder's `CMakeLists.txt`.

## Notes on the build

- Reflection flags (`-freflection -freflection-latest -fannotation-attributes`)
  are scoped to `main.cpp` via `set_source_files_properties`, so moc-generated
  code is compiled with plain C++26.
- We link against the libc++ that ships with clang-p2996 (`-nostdlib++ -lc++ -lc++abi`).
  If Qt was built against a different libstdc++/libc++, you may see ABI warnings on
  symbols that cross the boundary — none currently do here because every type passed
  to Qt is a Qt type (`QString`, `QVariant`, …).
