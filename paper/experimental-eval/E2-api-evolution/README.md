# E2 — The cost of API evolution

**Question.** When a C++ library grows, how much of its binding has to change?

**Claim under test** (paper §7): because the scriptable projection binds
`rosetta::script` rather than the user's classes, a library can grow without the
binding changing at all.

## Running it

```sh
./run.sh              # v1=10, v2=30, v3=130 classes
./run.sh 5 10 20      # custom sizes
./run.sh clean
```

Requires only `clang-p2996` and `bin/rosetta_gen` — file counts, line counts and
diffs are all decidable at the generation stage. Full sweep: ~64 s.

## Method

One library, three cumulative versions (10 → 30 → 130 classes, 4 methods and 3
fields each). Versions are supersets: version *N* contains `Unit0..Unit{N-1}`, so
"what changed" is a real diff and not a reshuffle.

Three arms:

| arm | what it is |
|---|---|
| **static** | one manifest listing every class → pybind11 + N-API + sol2 bindings. Also the **manual** arm's artifact: the emitted pybind11 module is line-for-line what a developer would otherwise hand-write. |
| **dyn** (stage 1) | the `dynamic` backend → `auto_dynamic.{h,cpp}` metadata tables |
| **meta** (stage 2) | the `scriptable` preset → a binding of `rosetta::script` itself |

**One normalisation.** Generated `CMakeLists.txt` files embed the absolute path
of their configuration directory, which contains the version name. Left alone
that would make every file differ between versions for a reason having nothing
to do with the library, so paths are rewritten to a placeholder before hashing.
Without this the "0 changed files" result would be an artifact; with it, it is
about content.

## Results

### Generated binding source (LOC)

| arm | v1 (10) | v2 (30) | v3 (130) | fit |
|---|---:|---:|---:|---|
| static binding | 763 | 2 103 | 8 803 | `93 + 67·N` |
| scriptable stage 1 — tables | 1 106 | 3 006 | 12 506 | `156 + 95·N` |
| scriptable stage 2 — the binding | 1 208 | 1 208 | 1 208 | **constant** |

### Human-authored manifest lines

| arm | v1 | v2 | v3 | fit |
|---|---:|---:|---:|---|
| static | 53 | 133 | 533 | `13 + 4·N` |
| scriptable stage 2 | 25 | 25 | 25 | **constant** |

### Source files changed by the version bump

| arm | v1→v2 | v2→v3 |
|---|---|---|
| static binding | 4 changed | 4 changed |
| scriptable stage 1 — tables | 2 changed | 2 changed |
| **scriptable stage 2 — the binding** | **0** | **0** |

### Generation wall-clock (driver compile + generate, s)

| arm | v1 | v2 | v3 |
|---|---|---|---|
| static | 3 + 0 | 5 + 0 | 14 + 0 |
| stage 1 | 3 + 1 | 5 + 0 | 14 + 0 |
| stage 2 | 5 + 0 | 5 + 0 | **5 + 0** |

## What this shows — and the thing it does *not* show

The binding of `rosetta::script` is **byte-identical at 10 classes and at 130**,
and its manifest never mentions a library class. Growing the library by 120
classes changes **zero** of its source files, while the static binding changes
four every time and grows by 67 LOC per class. Stage-2 generation time is flat
at 5 s while the static arm's driver compile grows from 3 s to 14 s.

**But the scriptable arm produces more total code, not less.** At every size
measured:

| | v1 (10) | v2 (30) | v3 (130) |
|---|---:|---:|---:|
| static artifact | 763 | 2 103 | 8 803 |
| scriptable artifact (tables + binding) | 2 314 | 4 214 | 13 714 |

The metadata tables are bulkier per class (95 LOC) than the pybind11 binding
they replace (67 LOC), and stage 2 adds a fixed 1 208 LOC on top. Anyone
claiming the scriptable model "generates less code" would be wrong.

The win is not volume, it is **what has to change, and who writes it**:

1. **Zero binding files change** when the library grows — against four.
2. **The human-authored manifest stays at 25 lines** — against 533 at 130
   classes, one entry per class.
3. **The 1 208 LOC is not per-library.** It is a binding of `rosetta::script`,
   which is the same for every library that has been through the `dynamic`
   backend. It is paid once, ever — not once per project. The static arm's
   `67·N` is paid again by every library.
4. The `O(N)` part does not vanish: the metadata tables still regenerate on
   every version bump, and two files change each time. Only the *binding* is
   constant.

Taken as a per-library artifact-size question, the crossover is at **~17
classes** (`93 + 67N = 1208`). Taken as a maintenance question — the question
that actually motivated it — stage 2 is constant from the first class onward.

## Threats to validity

- The library is synthetic and uniform: every class has the same shape, so LOC
  per class is a clean constant in a way a real API's would not be. What is
  being tested is the *scaling relationship*, not the constants.
- The **manual** arm is measured as the size of the artifact a developer would
  have to write, not as the time to write it. It is an upper bound on what
  automation removes, not a developer-hours study — no such study is claimed.
- Only generation is measured. Compiling 12 506 lines of metadata tables is not
  free, and E3 covers build cost.

## Files

| file | role |
|---|---|
| `genlib.py` | emits one version of the library + all three arms' manifests |
| `run.sh` | generates every arm for every version, records timings |
| `analyse.py` | hashes/diffs the generated trees → `results.{csv,md}` |
| `work/` | per-version scratch (regenerated) |
