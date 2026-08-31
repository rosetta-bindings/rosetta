# E4 — Runtime overhead

**Question.** E1 showed that late binding *recovers* API surface that
early-binding targets lose. What does it cost per call?

**Claim under test** (`docs/PIPELINE.md`): dynamic dispatch is "the right price
for menus, property panels, commands and scripting glue; the wrong price for
bulk data." E4 prices it.

## Running it

```sh
./run.sh              # both layers   (~25 s)
./run.sh --cpp-only   # layer A only, no Python needed
./run.sh clean
```

Unlike E1/E2 this experiment must **build and run**, so it needs a C++ compiler
for layer A and Python + pybind11 for layer B.

## Design: two layers, on purpose

| layer | arms | isolates |
|---|---|---|
| **A — C++** | `direct` / `dyn` (by name) / `dyn-cached` | late binding, no interpreter |
| **B — Python** | generated pybind11 / scriptable meta-object | late binding **+** the language boundary |

Running only layer B would confound two independent costs. The difference
between the layers is the interpreter's share.

`dyn-cached` resolves the `MetaMethod`/`MetaField` once and reuses it.
`dynamic.h` explicitly recommends this — *"the returned pointer is stable for
the process, so a wrapper should cache it per call site"* — so measuring only
the uncached path would price a usage the documentation tells you not to write.

Operations: trivial call, field get, field set, 3 scalar arguments, object
return, 1000-element `vector<double>` round trip, and a 3-candidate overload set
(so the resolution E1 celebrated can be priced).

## Results

### Layer A — C++ only

| operation | direct | dyn (by name) | dyn (cached) | dyn/direct | cached/direct |
|---|---:|---:|---:|---:|---:|
| trivial-call | 3.6 ns | 51.5 ns | 4.2 ns | 14.4× | **1.2×** |
| field-get | 3.6 ns | 10.8 ns | 4.2 ns | 3.0× | **1.2×** |
| field-set | 3.6 ns | 75.2 ns | 4.2 ns | 21.0× | **1.2×** |
| 3-arg-call | 3.6 ns | 159.2 ns | 14.6 ns | 44.4× | 4.1× |
| object-return | 3.4 ns | 187.7 ns | 107.4 ns | 54.8× | 31.3× |
| vector-1000 | 386.6 ns | 17.5 µs | 7.8 µs | 45.4× | 20.2× |
| overload-3way | 3.6 ns | 629.0 ns | 14.2 ns | **175.3×** | 4.0× |

### Layer B — from Python

| operation | generated pybind11 | scriptable meta-object | ratio |
|---|---:|---:|---:|
| trivial-call | 86.9 ns | 467.0 ns | 5.4× |
| field-get | 87.6 ns | 317.3 ns | 3.6× |
| field-set | 92.6 ns | 436.9 ns | 4.7× |
| 3-arg-call | 119.8 ns | 895.6 ns | 7.5× |
| object-return | 245.0 ns | 615.2 ns | 2.5× |
| vector-1000 | 17.1 µs | 42.9 µs | 2.5× |
| overload-3way | 108.4 ns | 1 216 ns | 11.2× |

### The same operation, two ratios

| operation | A: dyn/direct | B: script/static |
|---|---:|---:|
| trivial-call | 14.4× | 5.4× |
| field-set | 21.0× | 4.7× |
| 3-arg-call | 44.4× | 7.5× |
| object-return | 54.8× | 2.5× |
| vector-1000 | 45.4× | 2.5× |
| overload-3way | **175.3×** | 11.2× |

## What it shows

**1. Most of the cost is name lookup, and caching removes it.** For the three
cheapest operations, caching the resolved member brings late binding to **1.2×**
a direct C++ call — effectively free. The thunk is not the problem; finding it is.

**2. Overload resolution is the expensive lookup.** 629 ns uncached against
14 ns cached — 44× — and the worst ratio in the whole experiment at 175× direct.
This is precisely the mechanism E1 credits with recovering two thirds of the
API on name-keyed targets. E1 and E4 together give the honest trade: **late
binding buys back the overloads, and scoring them at every call is what it
costs.** Cache the resolution and you keep the expressiveness at 4.0×.

**3. Marshalling cannot be cached away.** Object return stays at 31× and the
1000-element vector at 20× even fully cached and pre-boxed, because the cost is
boxing each value into an `Any`, not finding the method. This is the hard floor
of the dynamic model.

**4. From a host language the penalty shrinks to 2.5–11.2×,** because crossing
the boundary already costs 87–245 ns before any of this. A 175× C++ ratio
becomes 11.2× seen from Python.

### The practical rule, in numbers

At ~317–467 ns per scriptable operation:

- a property panel with 100 fields: **~32 µs** — imperceptible;
- a menu, a dialog, a REST route, an inspector: far below any latency budget;
- a 1-million-element loop: **~0.47 s** — unacceptable.

Which is what `docs/PIPELINE.md` claims in prose, now with a number: the dynamic
model is right for control and wrong for bulk data, and the boundary is roughly
four to five orders of magnitude of call count, not a subtle judgement call.

## A build-configuration bug — found here, since fixed

The generated `CMakeLists.txt` used to carry a `CMAKE_BUILD_TYPE` block only when
the manifest asked for one, so the obvious invocation

```sh
cmake -S bindings/python -B build && cmake --build build
```

configured CMake with **no build type** — no `-O`, no `NDEBUG` — and produced an
unoptimised module with nothing in the output to say so. Measured that way the
pybind11 trivial call read **707 ns instead of 84 ns**, and every ratio in this
experiment would have been wrong by a factor of eight.

Fixed in `include/rosetta/inline/generate.hxx`: the block is now always emitted
and defaults to `Release`, guarded on `CMAKE_CONFIGURATION_TYPES` so multi-config
generators (Visual Studio, Xcode) still choose at build time. An explicit
`-DCMAKE_BUILD_TYPE=...` and the manifest's `build_type` both still win. Verified
across all nine compiled backends; the naive two-command build now compiles at
`-O3 -DNDEBUG`.

`run.sh` still passes `-DCMAKE_BUILD_TYPE=Release` explicitly, so the experiment
does not depend on the default being right.

It is worth noting *how* this surfaced: every functional test passed both before
and after. The artifact was never wrong, only slow — which is the class of defect
a performance experiment catches and a correctness suite structurally cannot.

## Threats to validity

- **One machine, one OS, one compiler.** Ratios are more portable than absolute
  numbers; treat the ns figures as this laptop's, not the universe's.
- **Best-of-5 for layer B**, to floor out scheduler noise. Repeatability across
  consecutive runs was ±1.5%. One early run taken immediately after heavy
  parallel builds read 1.7× slow across the board — measurements were repeated
  on an idle machine.
- **Microbenchmarks measure microbenchmarks.** Real UI code interleaves these
  calls with layout and I/O that dwarf both arms.
- The scriptable arm returns an `Outcome` object where the static arm returns a
  bare `float`; that allocation is part of the API, not a harness artifact, and
  is deliberately included.

## Files

| file | role |
|---|---|
| `genlib.py` | benchmark library + manifests for all three arms |
| `bench_cpp.cpp` | layer A: direct / dyn / dyn-cached |
| `bench_py.py` | layer B: pybind11 / scriptable |
| `analyse.py` | merges both layers → `results.{csv,md}` |
| `run.sh` | generate, build, run, analyse |
