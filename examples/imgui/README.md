# imgui — a Dear ImGui inspector from out-of-line annotations

Binds the `Algo` demo class to the **`imgui-expanded`** backend: a
self-contained desktop app (Dear ImGui + GLFW + OpenGL 3, both fetched
automatically by CMake) that builds with a **stock C++20 compiler**. The same
manifest also emits the **`qt-expanded`** and **`qml-expanded`** inspectors, so
the three UI backends can be compared on the identical annotated surface:

```sh
cmake -S bindings/qt-expanded  -B bindings/qt-expanded/build  && cmake --build bindings/qt-expanded/build  -j && ./bindings/qt-expanded/build/algo_qt
cmake -S bindings/qml-expanded -B bindings/qml-expanded/build && cmake --build bindings/qml-expanded/build -j && ./bindings/qml-expanded/build/algo_qml
```

> Qt note: Qt **6.8.3**'s CMake config references the `AGL` framework, which
> recent Xcode SDKs removed — the link fails with `ld: framework 'AGL' not
> found`. Configure with `-DQT_DIR=$HOME/Qt/6.5.3/macos` (or any Qt build made
> for the newer SDK).

[`Algo.h`](Algo.h) here is the stock-C++ variant of the inline-annotated
[`../Algo.h`](../Algo.h): the header never mentions rosetta — every `doc`,
`range` and `combobox` lives out of line in [`Algo.ann.json`](Algo.ann.json),
wired in by the manifest's `"annotations"` field:

| `Algo` member | Side-car annotations | Widget |
|---|---|---|
| `eps` | `doc`, `range [1e-10, 1e-6]`, `widget "textfield"`, `label "EPS"` | typed input box, clamped to the range |
| `maxIter` | `doc`, `range [0, 200]`, `widget "slider"`, `label "Max iter"` | clamping slider |
| `iterative` | `doc`, `label "Iterative solver"` | checkbox (from the `bool` type) |
| `solverName` | `doc`, `combobox [...]`, `label "Solver name"` | combo |
| `precond` | `doc`, `combobox [...]`, `widget "radio"` | radio-button group |
| `plotColor` | `doc`, `widget "color"` | color picker (hex `"#rrggbb"` string) |
| `meshFile` | `doc`, `widget "file"` | path field + "…" browse button (native dialog) |
| `notes` | `doc`, `widget "multiline"` | multi-line text area |
| `run()` / `reset()` | `doc`, `button "Run"` / `button "Reset"` | **Run** / **Reset** buttons, `run`'s convergence shown next to it |

Every `doc` becomes a "(?)" hover tooltip. Dear ImGui is immediate-mode, so
the generated `draw_Algo()` runs each frame and binds the widgets **directly
to the live object** — no copies, no sync layer.

## Run it

```sh
./run.sh                          # opens the inspector window
ROSETTA_IMGUI_FRAMES=5 ./run.sh   # smoke test: render 5 frames, then exit
```

Only stage 1 (running the generator) needs the clang-p2996 toolchain; the
inspector itself compiles with any stock C++20 compiler.

## Inline annotations instead?

The side-car carries the **full** annotation set — `doc`, `range`, `readonly`,
`combobox`, `label`, `button` and the `widget` hints — so this example's UI is
pixel-identical to what the inline-annotated original (`../Algo.h`) produces.
If you prefer inline annotations anyway, note that a header carrying them
can't be parsed by a stock compiler, since the generated inspector
`#include`s it; configure the generated project with
`-DROSETTA_IMGUI_CPP26=ON` to switch it to the clang-p2996 toolchain
(`-fannotation-attributes`). The generated binding code is identical either
way — only who can compile the included header changes.

Gotcha: the side-car is **baked into `bindings.h` when `rosetta_gen` runs** —
after editing `Algo.ann.json`, re-run stage 1 (`rosetta_gen` + generator),
not just the app build.
