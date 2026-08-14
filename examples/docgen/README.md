# Auto-docgen example

Renders documentation for the annotated `Algo` struct from `../Algo.h` in
**Markdown or HTML**, using `rosetta::to_markdown<T>()` / `rosetta::to_html<T>()`
from `<rosetta/generate.h>` (the markdown/html backends' render-to-string entry
points). Unlike the other examples, this one has **no** external dependencies —
just libc++.

## Build & run

The format is chosen by an argument (default Markdown), and the single document
is written to stdout — so the usual `> file` redirect still works:

```bash
cmake -G Ninja -B build
cmake --build build

./build/auto_docgen                   # Markdown to stdout (default)
./build/auto_docgen > Algo.md         # capture Markdown
./build/auto_docgen html > Algo.html  # capture HTML
./build/auto_docgen md   > Algo.md    # explicit Markdown
```

## Sample output

```markdown
# Person

## Fields

| Name | Type | Description |
|------|------|-------------|
| `name` | `std::string` | the person's display name |
| `age` | `int` | age in whole years (range: 0..150) |
| `id` | `std::string` | server-assigned identifier _(readonly)_ |

## Methods

### `greet(salutation: const std::string &) → std::string`

Returns a greeting prefixed by the given salutation.

### `clear() → void`
```

## Annotation surface

| Annotation     | Effect on the output                                  |
|----------------|-------------------------------------------------------|
| `doc{"..."}`   | description column for fields / paragraph for methods |
| `readonly`     | `_(readonly)_` tag in the description column          |
| `range{lo,hi}` | `(range: lo..hi)` tag in the description column       |

## What's where

| File                | Role                                                  |
|---------------------|-------------------------------------------------------|
| `main.cpp`          | Renders `Algo` (md/html by arg), writes it to stdout  |
| `CMakeLists.txt`    | clang-p2996 wiring                                    |
| `../Algo.h`         | Demo type                                            |

`to_markdown<T>()` is the markdown backend's render-to-string entry point
(`include/rosetta/backends/markdown.h`); `to_html<T>()` is its HTML
counterpart. Both build on the same reflected IR every backend consumes.
