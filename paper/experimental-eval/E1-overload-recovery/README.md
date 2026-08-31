# E1 — Overload recovery

**Question.** When a target's binding surface is name-keyed, C++ overload sets
cannot survive generation. How much of an API does that cost, and how much of it
comes back through the late-bound projection?

**Claim under test** (paper §8): the set of C++ members a target can express is
not a property of the target language — it is a property of *when* that target
resolves names.

The hypothesis is stated outright in the implementation, in
`include/rosetta/backends/inline/dynamic.hxx`:

> `// NOTE: no coverage::emit_overload() call. Every other name-keyed backend`
> `// passes overloads::first_only here and discards the siblings; the dynamic`
> `// model keeps them all, which is the point.`

E1 measures it.

## Running it

```sh
./run.sh              # full sweep, overloads per name = 1..5
./run.sh 1 3 5        # only those multiplicities
./run.sh clean
```

Requires **only** `clang-p2996` and `bin/rosetta_gen`. No emsdk, JDK, .NET,
Julia or Lua is needed: `coverage.json` is written by the generator *before* any
binding is compiled, so E1 never leaves the generation stage. The full sweep
takes well under a minute (driver compile 3–7 s per configuration).

## Method

`genlib.py` emits a synthetic library of `C` classes × `N` method names, where
each name carries `R` overloads, plus — per name — one uniquely-named *control*
method that is never part of an overload set.

Every parameter and return type is one that every backend can marshal (`int`,
`double`, `std::string`); every class is default-constructible, publicly
destructible and copyable. This is deliberate: the only thing that should cause
a member to be dropped is the overload policy under test. Anything else is a
confound, and `analyse.py` reports it in a separate `other drops` column rather
than folding it into the result.

Predictions, for `C`=8, `N`=6:

| quantity | formula | at R=5 |
|---|---|---:|
| total methods | `C·N·(R+1)` | 288 |
| a first-only target can bind | `C·N·2` | 96 |
| dropped to overload policy | `C·N·(R−1)` | 192 |

## Two measurement caveats, both found in the data

1. **`dynamic` records bound fields as well as methods** (`note_bound_field`),
   while the language backends here record only methods. Comparing the
   top-level `bound` counts would credit `dynamic` with the fields too, so
   `analyse.py` counts methods only, identified by a `(` in the signature.

2. **`typescript` records skips but never calls `note_bound`**, so its bound
   count is 0 no matter what it emitted. That is an instrumentation gap in the
   backend, not a measurement, and it is flagged (⚠) rather than averaged in.
   It is a real, if minor, bug: `typescript` is the one target whose
   `coverage.json` cannot be read as a coverage figure.

## Results

`C`=8 classes, `N`=6 names. Methods bound, by overloads per name:

| overloads per name | 1 | 2 | 3 | 4 | 5 |
|---|---:|---:|---:|---:|---:|
| total methods in the library | 96 | 144 | 192 | 240 | 288 |
| `python` / `nanobind` / `julia` (all-overloads) | 96 | 144 | 192 | 240 | 288 |
| `lua` / `node` / `wasm` / `csharp` / `java` (first-only) | 96 | **96** | **96** | **96** | **96** |
| `dynamic` (late-bound) | 96 | 144 | 192 | 240 | 288 |
| **recovered by late binding** | 0 | **48** | **96** | **144** | **192** |

The five first-only targets bind a *constant* 96 methods however many overloads
the library declares, while the late-bound projection tracks the library exactly.
Recovery is `C·N·(R−1)` — precisely the predicted drop, with no residue.

At R=5, **192 of 288 methods (66.7%) of the API are unreachable** through the
static binding on those five targets, and **all 192 are reachable** through the
late-bound one.

Two controls came out clean across every configuration:

- `other drops` = 0 everywhere — no member was lost for any reason except the
  overload policy, so the comparison is not contaminated.
- `reflection drops` = 0 everywhere — nothing was lost before the backends saw
  it, so every difference is attributable to a backend decision.

## What this does and does not show

It shows that the overloads are **present in the IR** and lost only in
projection: the same reflected description yields 96 or 288 reachable methods
depending on when the target resolves names. That is the paper's §8 claim, and
it is now measured rather than argued.

It does **not** show that late binding is free. Every recovered call pays a name
lookup and an overload-scoring pass; E4 measures that cost. The honest summary
is that late binding buys expressiveness with per-call time.

It also does not claim the synthetic library is representative — the overload
multiplicity is controlled precisely *because* it is the independent variable.
E5 measures coverage on a real third-party library, where the distribution of
overload set sizes is whatever it is.

## Files

| file | role |
|---|---|
| `genlib.py` | emits the synthetic library + manifest + `expected.json` ground truth |
| `run.sh` | sweeps overload multiplicity, runs the generation stage, calls the analysis |
| `analyse.py` | `coverage.json` → `results.{csv,md,tex}` |
| `results/results.csv` | one row per (configuration, target) |
| `results/results.md` | the tables above |
| `results/results.tex` | plot coordinates for the paper figure |
| `work/` | per-configuration scratch (regenerated; not worth committing) |
