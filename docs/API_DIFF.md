# API diff — what a release breaks

Every generation writes **`api.json`** next to `coverage.json`: the public API
of the bound library as reflection saw it — classes, fields, methods,
constructors, enumerations and free functions, with their exact signatures,
parameter names, defaults and annotations (`range`, `readonly`, `combobox`).
Keep the snapshot of each release, and

```bash
bin/rosetta_gen --api-diff v1.2/bindings bindings
```

tells you what the current API changes relative to 1.2 and which of those
changes **break** existing callers. It exits 1 when something does, so a CI
job can stop a release that breaks callers without a major version bump.

Most binding tools cannot do this: they never hold a description of the API,
only the generated glue. rosetta has the IR anyway, so the snapshot costs
nothing.

---

## Example

`v1` and `v2` of the same `Mesh` / `Mode`:

```cpp
// v1                                              // v2
enum class Mode { fast = 0, exact = 1, legacy = 2 }; enum class Mode { fast = 0, exact = 2, precise = 3 };

struct Mesh {                                      struct Mesh {
  [[= rosetta::range{0, 100}]] int quality = 50;     [[= rosetta::range{10, 100}]] int quality = 50;
  double scale = 1;                                  double scale = 1;
  int  count() const;                                int  count() const;
  void remesh(double edge);                          void remesh(double edge, int passes = 1);
  void smooth(int iterations);                       void smooth(int iterations, bool keep);
  double area() const;                               double surface() const;
                                                     void clear();
};                                                 };
```

```
$ rosetta_gen --api-diff v1/bindings v2/bindings
API diff 1.0 -> 2.0: 5 breaking, 0 warnings, 3 compatible

BREAKING
  x Mesh::quality: the field's range narrowed: values valid before are now rejected  [range_narrowed]
      was: int quality in [0, 100]
      now: int quality in [10, 100]
  x Mesh::smooth: the method's parameters changed  [signature_changed]
      was: void smooth(int iterations)
      now: void smooth(int iterations, bool keep)
  x Mesh::area: the method was renamed to 'surface' (same signature); callers spelling 'area' break  [method_renamed]
      was: double area() const
      now: double surface() const
  x Mode::exact: the enumerator's value changed from 1 to 2: stored or serialised values now mean something else  [enumerator_value_changed]
      was: 1
      now: 2
  x Mode::legacy: the enumerator was removed  [enumerator_removed]
      was: legacy = 2

COMPATIBLE
  + Mesh::remesh: the method gained trailing parameters with defaults  [params_appended]
      was: void remesh(double edge)
      now: void remesh(double edge, int passes = 1)
  + Mesh::clear: new method  [method_added]
      now: void clear()
  + Mode::precise: new enumerator  [enumerator_added]
      now: precise = 3
```

`count` and `scale` did not change, so the report doesn't mention them.

---

## Usage

```
rosetta_gen --api-diff <old> <new> [--json] [--breaking-only] [--strict]
```

| Argument | Meaning |
|---|---|
| `<old>`, `<new>` | an `api.json`, or a folder containing one (the bindings dir) |
| `--json` | machine-readable report on stdout (below) |
| `--breaking-only` | leave the compatible additions out of the text report |
| `--strict` | exit 1 on warnings too |

| Exit | Meaning |
|---|---|
| `0` | nothing breaks (warnings allowed unless `--strict`) |
| `1` | at least one breaking change (or a warning, with `--strict`) |
| `2` | bad arguments, unreadable file, not an `api.json` |

The `version` printed in the header (`1.0 -> 2.0`) is the manifest's
`version`, recorded in each snapshot.

### Keeping snapshots

The snapshot is written to the generator's output directory (`bindings/` under
`--build`). Two common workflows:

- **commit it** (e.g. as `api/api.json`) and let review show its diff; CI runs
  `rosetta_gen --api-diff api/api.json bindings` on every PR;
- **attach it to each release** and diff the release candidate against the
  previous release's file.

---

## What is reported

Three severities:

- **breaking**: code written against the old API stops working.
- **warning**: existing calls still bind but may behave differently.
- **compatible**: additions.

| Change | Severity | `kind` |
|---|---|---|
| class / enum / free function removed | breaking | `class_removed`, `enum_removed`, `function_removed` |
| class / enum renamed (same members) | breaking | `class_renamed`, `enum_renamed` |
| script-side name (`expose`) changed | breaking | `exposed_name_changed` |
| base class removed | breaking | `base_removed` |
| became abstract / final / not default-constructible | breaking | `became_abstract`, `became_final`, `lost_default_constructor` |
| constructor removed | breaking | `constructor_removed` |
| method removed / renamed (same signature) | breaking | `method_removed`, `method_renamed` |
| parameters changed (the one overload left on each side) | breaking | `signature_changed` |
| one overload of several removed | breaking | `overload_removed` |
| return type changed | breaking | `return_type_changed` |
| static ↔ instance | breaking | `static_changed` |
| no longer `const` | breaking | `const_dropped` |
| no longer `virtual` / became pure virtual | breaking | `virtual_dropped`, `became_pure` |
| parameter lost its default | breaking | `default_removed` |
| parameter became / stopped being an output parameter | breaking | `out_param_changed` |
| field removed / renamed (same type) / type changed | breaking | `field_removed`, `field_renamed`, `field_type_changed` |
| field became read-only | breaking | `became_readonly` |
| field `range` narrowed or added | breaking | `range_narrowed`, `range_added` |
| `combobox` choice removed | breaking | `choice_removed` |
| enumerator removed / renamed (same value) / value changed | breaking | `enumerator_removed`, `enumerator_renamed`, `enumerator_value_changed` |
| parameter renamed (keyword callers break) | warning | `param_renamed` |
| default value changed | warning | `default_changed` |
| enumeration's underlying type changed | warning | `underlying_changed` |
| anything added | compatible | `*_added` |
| trailing parameters added **with defaults** | compatible | `params_appended` |
| parameter gained a default, method became `const`, field became writable, range widened or removed | compatible | `default_added`, `const_added`, `became_writable`, `range_widened`, `range_removed` |

**How overloads are matched.** Overloads of one name are first paired by
identical parameter types. Any old overload left over is then paired with a
new one that only adds parameters with defaults. If exactly one overload is
still unpaired on each side, the pair is reported as one
`signature_changed`. Anything else left over is reported as removed or added.

**Renames are detected, never assumed.** A rename is reported only when the
match is unambiguous: one member disappeared and one appeared in the same
scope with the same signature, and no other member on either side has that
signature. Classes are matched by their member set (field names and types,
method names and parameter types), and enumerators by value. A rename is
still breaking, since callers use the old name. Its value is that the report
names the replacement.

**Default values.** A change in a default's *text* is reported only when both
snapshots captured it (from the header, see
[doc comments](MANIFEST.md#doc-comments-doc_comments)). Otherwise only the
presence of a default is compared.

---

## Scope

`api.json` is **target-neutral**: it is the C++ API every binding is built
from, before any backend has decided what it can marshal. It answers "did the
library's API change?". The question "did the Python binding stop exposing
`X`?" belongs to [`coverage.json`](COVERAGE.md), which records per target what
bound and what was skipped. To catch both, diff the two files.

Not covered: changes inside a function body, a changed meaning without a
changed signature, and members the reflection walk itself drops (operators,
member templates — listed in `coverage.json` under `reflection`).

---

## Formats

### `api.json` (schema 1)

```jsonc
{
  "rosetta_api": 1,
  "version": "2.0",
  "classes": [
    { "name": "Mesh", "expose": "Mesh", "bases": [], "abstract": false, "final": false,
      "default_constructible": true,
      "constructors": [ { "params": [ /* PARAM */ ] } ],
      "fields":  [ { "name": "quality", "type": "int", "readonly": false,
                     "range": { "min": 10, "max": 100 } } ],
      "methods": [ { "name": "remesh", "returns": "void",
                     "params": [ { "name": "edge", "type": "double", "out": false },
                                 { "name": "passes", "type": "int", "default": "1", "out": false } ],
                     "static": false, "const": false, "virtual": false, "pure": false,
                     "extension": false } ] }
  ],
  "enums":     [ { "name": "Mode", "expose": "Mode", "underlying": "int",
                   "values": [ { "name": "fast", "value": 0 } ] } ],
  "functions": [ { "name": "solve", "qualified": "ns::solve", "returns": "double",
                   "params": [ /* PARAM */ ] } ]
}
```

`name` is the qualified C++ name and is the identity key; `expose` is the
name scripts see.

### `--json` report

```jsonc
{
  "rosetta_api_diff": 1, "from": "1.0", "to": "2.0",
  "breaking": 5, "warning": 0, "compatible": 3,
  "changes": [
    { "severity": "breaking", "kind": "signature_changed",
      "scope": "Mesh", "member": "smooth",
      "detail": "the method's parameters changed",
      "before": "void smooth(int iterations)",
      "after":  "void smooth(int iterations, bool keep)" }
  ]
}
```

`kind` is a stable slug for tooling to match on. `detail` is meant for people
and may be reworded.

---

## From C++

The diff is the header-only [`<rosetta/api_diff.h>`](../include/rosetta/api_diff.h),
which needs only C++17 and nlohmann/json (no reflection):

```cpp
#include <rosetta/api_diff.h>

auto changes = rosetta::api::diff(old_json, new_json);   // std::vector<rosetta::api::Change>
if (rosetta::api::count(changes, rosetta::api::severity::breaking)) { ... }
std::cout << rosetta::api::report_text(changes, "1.0", "2.0");
```

Tests: `tests/api_diff.cpp`.
