# E3 — Generation cost and scalability

**Question.** What does it cost to run the pipeline once, how does that cost
scale with the size of the library, and which stage dominates?

**Claim under test** (paper §10.3): the reflective compile of the generator
driver dominates the pipeline, it is linear in the number of reflected members,
and it is paid once per *library* rather than once per *target*.

The last clause is the one that matters architecturally. §5 claims one walk
feeding many visitors; if that is true, asking for nineteen targets instead of
one must not cost nineteen walks. E3 measures whether it does.

## Running it

```sh
./run.sh                    # every sweep, ~6 min
./run.sh classes targets    # only those sweeps
./run.sh clean
```

Requires `clang-p2996` and `bin/rosetta_gen`, plus a system C++ compiler for
one step (below). No emsdk, JDK, .NET, Julia or Lua.

## Method

`genlib.py` emits a synthetic library with four independent size knobs — `C`
classes, `M` method names per class, `F` fields per class, `R` overloads per
name — giving `C·M·R` methods, `C·F` fields, and `C·(F + M·R)` reflected
members. Every type is one that every backend can marshal (`int`, `double`,
`std::string`) and every class is default-constructible, publicly destructible
and copyable, so nothing is skipped: emitted volume is a function of the knobs
alone, with no coverage effect mixed in.

Five sweeps, each varying one knob from a common base point of
`C=16, M=4, F=3, R=1`, three targets:

| sweep | varies | values |
|---|---|---|
| A | classes | 1, 2, 4, 8, 16, 32, 64, 128 |
| B | methods per class | 1, 2, 4, 8, 16 |
| C | fields per class | 1, 2, 4, 8, 16 |
| D | overloads per name | 1, 2, 3, 4, 5 |
| E | targets emitted | 1, 3, 5, 10, 19 |

Sweeps A–D change the library; E holds it fixed and changes only what is asked
of it. Targets are taken as a prefix of a fixed ladder that begins with
`dynamic`, so every configuration — down to the one-target one — emits the
metadata tables whose compiled size is one of the measurements.

### What is timed

Four stages, separately, by `measure.py` (monotonic clock, and peak RSS from
`RUSAGE_CHILDREN`):

| stage | what it is |
|---|---|
| `rosetta_gen` | manifest → driver project. Text templating; no reflection. |
| `cmake configure` | toolchain probe. Reported so the total is accounted for, but it is a property of the machine, not of the library. |
| **`driver compile`** | compiling the emitted `generator.cpp` with `clang-p2996`. This is where `rosetta::generate<Classes...>` runs the `consteval` walk. The hypothesis says this dominates. |
| `generator run` | running the driver: IR → emitted binding sources. |

### The one thing that gets compiled

E1, E2 and E5 stay strictly inside the generation stage. E3 makes one exception:
`bindings/dynamic/auto_dynamic.cpp` is compiled at `-O2` and its section sizes
read with `size`, because the footprint of the metadata tables is a measurement
and not an estimate. That compile uses the **system** compiler — the tables are
stock C++20, which is the whole point of the dynamic backend — so it adds no
toolchain requirement.

The section split is kept rather than folded into one number: `data` is the
descriptor tables and their strings, `text` is the emitted per-member call
thunks. They scale differently and a single "metadata size" would hide which.

### Reading the fits

`analyse.py` fits ordinary least squares per sweep and reports R² alongside every
slope, so a superlinear cost cannot hide inside a linear-looking one. Two
conventions:

- **The intercept is a result, not a nuisance.** Every driver pays a fixed cost
  for `<experimental/meta>`, the rosetta headers, and all nineteen backends —
  which are linked into every driver regardless of which targets the manifest
  names, because target selection is a runtime registry lookup. Separating that
  constant is what makes the per-member slope quotable.
- **Repeats are not deduplicated.** The base configuration is generated
  independently by four sweeps, and sweep E generates five configurations over
  an identical library. Their spread is the only noise estimate this harness
  has, and it is reported so that small differences elsewhere can be discounted.

No fit is reported for sweep E: its x-axis counts targets, which are not
interchangeable units. It gets a table and a range instead.

## Threats to validity

- **The library is uniform.** LOC and compile time per member are cleaner
  constants than a real API would give. What is being tested is the scaling
  *relationship*; E5 is where a real API's constants come from.
- **One machine, one toolchain.** The absolute seconds are a property of this
  laptop and of one revision of `clang-p2996`, a research fork under active
  development and not tuned for compile speed. The shape of the curve is the
  portable claim; the constants are not.
- **Single-TU driver.** The generator is one translation unit by construction,
  so its compile does not parallelise. That is a real property of the design,
  not an artifact of the harness — but it means the driver compile is a
  serial cost on a machine that could otherwise use its cores.
- **Not measured: compiling the emitted bindings.** E3 stops after generation
  (plus the one metadata compile). Building 19 targets' worth of pybind11,
  N-API and embind output is a much larger cost than generating it, and it is
  a cost of those libraries rather than of rosetta.

## Results

See [results/results.md](results/results.md) — regenerated by `run.sh`.
`results/results.csv` holds one row per configuration for replotting.
