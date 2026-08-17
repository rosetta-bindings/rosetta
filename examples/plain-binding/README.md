# Plain binding — one library, one pipeline, four languages

The base case. A stock-C++ class goes in; a Python module, a Julia module, a Node addon and a WebAssembly module come out. No `dynamic` target, no reflection API, no preset — this is what binding a class with rosetta normally looks like.

Read it before [`../scriptable-model`](../scriptable-model), which chains *two* pipelines to avoid generating a binding per class at all. This one is the thing that does.

```
Serie.h            declarations — plain C++20, no rosetta include
Serie.hxx          the bodies, included from Serie.h
Serie.ann.json     the docs, out of line
manifest.json      one class, one free function, four targets
drive.{py,jl,js}   the same script in three languages
drive.wasm.js      the fourth, unverified (no emsdk here)
```

```bash
./run.sh          # generate + build, then the Python driver
./run.sh node
./run.sh julia
./run.sh wasm     # needs an activated emsdk
./run.sh clean
```

## The library

`serie::Serie` is a flat buffer of scalars grouped into items of `itemSize`
consecutive values — `itemSize` 1 is a serie of scalars, 3 a serie of 3D
vectors, 6 a serie of symmetric tensors. `count()` is the number of items,
`size()` the raw number of scalars. Plus `forEach` / `map` / `reduce`, which take
a `std::function` rather than being templates — for a reason worth knowing.

It is deliberately more awkward than a demo struct: a nested iterator type,
`std::span`, callbacks, a method returning `*this` by reference, and a free
function taking a `std::vector` of the class itself.

The bodies live in `Serie.hxx`, included at the bottom of `Serie.h` — the layout
`<rosetta/dynamic.h>` itself uses. It is also a small test of the pipeline:
rosetta reads only `Serie.h`, so every member it binds is one whose body it never
saw. Reflection reads **declarations**, and the split proves it. That is the point —
those are the shapes that decide what a real binding looks like.

## The whole manifest

```json
{
    "user_include": ".",
    "rosetta_include": "../../include",
    "generator_name": "serie_gen",
    "module_name": "serie",
    "targets": ["python", "julia", "node", "wasm"],
    "classes": [
        { "name": "serie::Serie", "header": "Serie.h", "annotations": "Serie.ann.json" }
    ],
    "functions": [
        { "name": "serie::weightedSum", "header": "Serie.h" }
    ]
}
```

Bare-string targets, so all four modules are called `serie` — the `module_name`
default. Give a target an object (`{ "lang": "python", "name": "…" }`) to
override one.

## What binds, and what doesn't

`bindings/coverage.json` after a build:

```
python   bound 13  skipped 2
node     bound 13  skipped 2
wasm     bound 13  skipped 2
julia    bound 10  skipped 5
```

The split is the callback methods — see [below](#getting-the-templates-back).

| | |
|---|---|
| **bound** | `itemSize` `count` `size` `empty` `item` `scalar` `scalars` `raw` `append` `describe`, plus the free `weightedSum` — and `forEach` / `map` / `reduce` everywhere but julia |
| **skipped** | `begin` / `end` — they return `Serie::ItemIterator`, an unbound nested type. Correct, and reported with the reason rather than silently dropped. |
| **invisible** | A member **template** — the natural spelling of `map(F&&)` — has nothing to reflect on until something names an instance, so it never reaches the coverage report at all: not "skipped", never a candidate. See [Getting the templates back](#getting-the-templates-back). |

Three consequences worth internalising before binding your own class:

- **`std::vector<T>` marshals; `std::span<T>` does not.** `item()` returns a
  `std::vector<double>` copy on purpose. The `span` is still there for C++
  callers, on the iterator, where no binding can reach it.
- **A reference return works.** `append()` returns `Serie&`, so `s.append(t).scalars()`
  chains in every language.
- **A free function can take a `std::vector` of the bound class.**
  `weightedSum([a, b], [2.0, 1.0])` passes two Python objects into C++ and gets
  a new one back.

## Getting the templates back

A member template is invisible because there is nothing to reflect on until
something names an instance. The fix is to stop using a template *at the
boundary* — `std::function` is a concrete type:

```cpp
void   forEach(const std::function<void(double)> &f) const;
Serie  map(const std::function<double(double)> &f) const;
double reduce(const std::function<double(double, double)> &f, double init) const;
```

Whether that binds comes down to whether the target can turn a host callable
into a `std::function`:

| target | callbacks | how |
|---|:--:|---|
| python | ✅ verified | pybind11's `<pybind11/functional.h>` |
| node | ✅ verified | `rosetta::napi_make_fn` — added for this example; see below |
| wasm | ✅ implemented | `rosetta_wx::make_fn`, emitted into the binding: the parameter arrives as `emscripten::val`, wrapped in a closure |
| julia | ❌ | jlcxx has no conversion for a `std::function` — all three are skipped, *with the reason* |

```python                                # drive.py
s.forEach(seen.append)                   # -> [1.0, 2.0, 4.0, 9.0]
s.map(lambda x: x * x).scalars()         # -> [1.0, 4.0, 16.0, 81.0]
s.reduce(lambda acc, x: acc + x, 0.0)    # -> 16.0
```
```javascript                            // drive.js
s.forEach((x) => seen.push(x));
s.map((x) => x * x).scalars();
s.reduce((acc, x) => acc + x, 0.0);
```

So coverage stops being uniform, and that is the honest picture:

```
python   bound 13  skipped 2      (begin, end)
node     bound 13  skipped 2      (begin, end)
wasm     bound 13  skipped 2      (begin, end)
julia    bound 10  skipped 5      (begin, end, forEach, map, reduce)
```

> **The node half was written for this example.** `rosetta::napi_make_fn`
> (`runtime/node.h`) persists the incoming `Napi::Function` in a
> `shared_ptr<FunctionReference>` — so a class that *stores* the callback and
> fires it later still works, and the last copy of the closure releases the JS
> function. It is still only callable on the JS thread. The emitter gate
> (`nx_callback_convertible`) opens for a callback whose whole signature is
> convertible: scalars, bool, string, enum and vectors of those.
>
> A JS callback that throws mid-iteration comes back out intact:
> `s.map(x => { throw new Error("boom") })` → `boom`, process alive. Passing a
> non-function gives `Error - Invalid argument`, not a crash.

### Don't overload a template with a non-template

Tempting, and it does not compile:

```cpp
template <typename F> Serie map(F &&f) const;                    // invisible to the IR
Serie map(const std::function<double(double)> &f) const;         // the bindable one
```

The template is invisible to reflection but perfectly visible to C++ overload
resolution, so the emitted `&Serie::map` is ambiguous and every backend fails to
compile. Either give the bindable overload its own name (`mapScalars`) or, as
`Serie.h` does, let the `std::function` version be the only one.

For julia today: expose the operation without a callback (`sum()`, `scaled(k)` —
usually a better API anyway) or do the loop in Julia over `scalars()`.

## What each language sees

Fields would be properties; this class has none — everything is a method. The
enum-free, all-const surface makes the *spelling* differences stand out:

```python                                   # drive.py
v = serie.Serie([0,0,0, 1,0,0, 0,1,0], 3)
v.item(1)                                   # -> [1.0, 0.0, 0.0]   a Python list
s.append(serie.Serie([9.0], 1)).scalars()   # -> [1.0, 2.0, 4.0, 9.0]
serie.weightedSum([a, b], [2.0, 1.0])       # a list of bound objects, in
```
```javascript                               // drive.js
const v = new serie.Serie([0,0,0, 1,0,0, 0,1,0], 3);
v.item(1);                                  // -> Array
serie.weightedSum([a, b], [2.0, 1.0]);
```
```julia                                    # drive.jl
v = serie.Serie([0.0,0,0, 1,0,0, 0,1,0], 3)
serie.item(v, 1)                            # CxxWrap: free functions, not methods
serie.scalars(serie.append(s, t))           # -> Vector{Float64}
```

Julia's `f(obj, args…)` spelling is a **CxxWrap convention** — rosetta follows
each runtime rather than imposing one shape on all of them. Note also that
indices stay **0-based** in Julia: they are C++ indices, not Julia ones.

## Annotations, and why they are out of line

`Serie.h` has no `[[ = rosetta::doc{...} ]]` in it. Everything lives in
`Serie.ann.json`:

```json
"item": { "doc": "The i-th item, as a copy of itemSize consecutive scalars" }
```

…and becomes a real docstring:

```python
>>> serie.Serie.item.__doc__
item(self: serie.Serie, arg0: ...) -> list[float]

The i-th item, as a copy of itemSize consecutive scalars
```

The reason to keep them out of line is mechanical, not stylistic. The generated
bindings `#include Serie.h` and are compiled by a **stock** toolchain, so an
inline annotation would drag `<experimental/meta>` into every target and the
C++26 fork would be needed twice instead of once. See
[OUT_OF_LINE_ANNOTATIONS.md](../../docs/OUT_OF_LINE_ANNOTATIONS.md) and
[PIPELINE.md](../../docs/PIPELINE.md#your-library-is-read-twice-for-different-reasons).

## Exceptions cross

`Serie` enforces its own contract by throwing, which is the natural way for a
bound class to reject bad input. It arrives as a catchable, correctly-typed
error in every runtime:

```
python : ValueError - Serie::scalars: requires itemSize == 1
julia  : Serie::scalars: requires itemSize == 1
node   : TypeError - Serie::scalars: requires itemSize == 1
```

`std::out_of_range` becomes a `RangeError`, `std::invalid_argument` and
`std::domain_error` become `TypeError`s, anything else deriving from
`std::exception` becomes an `Error` carrying `what()`.

> **This example is why that works.** node-addon-api's own boundary wrapper
> catches `Napi::Error` and nothing else, so a plain `throw
> std::invalid_argument(...)` used to escape into the C++ runtime and call
> `std::terminate` — the whole node process died where pybind11 and CxxWrap both
> handed the script a catchable error. `rosetta::guard()` in
> `runtime/inline/node.hxx` now wraps every entry point (constructor, field
> accessors, methods, statics, free functions). Found here, fixed in the backend.

## Two traps that are not rosetta's fault

- **Enumerator names collide with host keywords.** This class has no enum, but
  when yours does: `SomeEnum.None` is a Python *syntax error*, leaving callers
  with `getattr(SomeEnum, "None")`. `True`, `class`, `end`, `function` all bite
  somewhere. Pick names that survive the trip, or rename with `expose`.
- **wasm objects need releasing.** embind hands out handles the caller must
  `delete()`. The other three runtimes collect for you.

## Status

`python`, `julia` and `node` are built and run above. **`wasm` is unverified** —
there is no emsdk on the machine this was written on, so the target was skipped
and `drive.wasm.js` has never executed. The generated `bindings/wasm/` project
is complete; `emcmake cmake -S bindings/wasm -B bindings/wasm/build && cmake
--build bindings/wasm/build` is all it should need.
