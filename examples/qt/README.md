# Auto-Qt-Widgets example

A QtWidgets property / method inspector built by **one** `rosetta::walk`
over the annotated `Person` struct from `../bindings/person.h`. Unlike
the `../qml/` example, this version has no QML and no metadata
intermediate — the visitor constructs `QSpinBox`, `QLineEdit`,
`QCheckBox`, and `QPushButton` rows directly and wires lambdas into
`QPushButton::clicked`.

```
┌──────────────────────────────────────────────────────┐
│ Person — generated from one C++26 reflection walk    │
├─[ Fields ]───────────────────────────────────────────┤
│  name              [Alice                       ]    │
│  age [0..150]      [════════•═════════════════ 42]   │
│  id [ro]           [s-123                       ]    │
├─[ Methods ]──────────────────────────────────────────┤
│  greet(1)       [salutation       ]         Call     │
│                                                Reset │
├──────────────────────────────────────────────────────┤
│  log...                                              │
└──────────────────────────────────────────────────────┘
```

Each editor commits to the target struct on its natural signal — Enter or
focus loss for text, arrow click / Enter for spin boxes, toggle for
checkboxes, selection for combo boxes, release for sliders. There are no
GET / PUT buttons.

Annotation behaviour mirrors the python / rest / qml backends:

| Annotation        | Effect on the UI                                          |
|-------------------|-----------------------------------------------------------|
| `doc{...}`        | tooltip on the field / method label                       |
| `readonly`        | editor disabled, `[ro]` suffix on the label               |
| `range{lo,hi}`    | spinbox/slider bounds + range hint on the label + re-check|
| `combobox{{...}}` | editor becomes a `QComboBox` of the allowed choices       |
| `widget::slider`  | int + range renders as `QSlider` + value label            |
| `widget::checkbox`| arithmetic field renders as `QCheckBox` (0/1)             |
| `widget::textfield`| numeric field renders as `QLineEdit` (range validator)   |
| `button{"label"}` | overrides the action button text on a method row          |

## Build

```bash
cmake -G Ninja -B build
cmake --build build
./build/auto_qt
```

The CMake script defaults to:

- `CLANG_P2996_ROOT = $HOME/devs/c++/clang-p2996/build`
- `QT_DIR          = $HOME/Qt/6.5.3/macos`

Override either with `-D<name>=...` on the cmake line.

## What's where

| File                  | Role                                                          |
|-----------------------|---------------------------------------------------------------|
| `visitors/widget.h`    | `rosetta::WidgetVisitor<T>` + `build_inspector<T>` entry point|
| `main.cpp`            | Walks `Person`, shows the inspector                           |
| `CMakeLists.txt`      | Qt6::Widgets + clang-p2996 wiring                             |

## Notes on the build

- We define no `Q_OBJECT` class of our own — Qt's widget signals
  (`QPushButton::clicked`, etc.) are enough. AUTOMOC is still on because
  Qt's own widget headers contain `Q_OBJECT`.
- Reflection flags are scoped to `main.cpp` via
  `set_source_files_properties`, so moc-generated TUs stay on plain
  C++26.
