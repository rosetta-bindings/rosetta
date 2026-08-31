# Rosetta — paper plan (arXiv preprint, English)

Status: structural plan. `DRAFT.md` holds the prose skeleton.
Everything marked **[TODO-N]** needs a number that does not exist in the repo yet.

---

## 0. The thesis, in one sentence

> C++26 static reflection can be used as a *language-independent interface
> description mechanism*: one reflective pass over an unmodified C++ library
> yields an intermediate representation from which both reflection-free static
> bindings and a persistent runtime meta-object model can be projected, and the
> two projections differ only in when name resolution happens.

Everything in the paper is in service of that sentence. The two halves —
*static projection* and *dynamic projection* — must be presented as **one
mechanism seen from two sides**, not as two features.

## 0.1 The load-bearing evidence

The single most convincing fact in the repository, and the one the paper should
be built around:

**Lua, Node, WASM, C# and Java drop C++ overload sets at generation time**
(`docs/COVERAGE.md` per-target policy table — their binding surfaces are
name-keyed maps, so a second `c["at"] = …` overwrites the first). **Through the
scriptable meta-object model, those same five targets get their overloads
back**, because `resolve()` scores the whole overload set at call time.

This is not an anecdote. It is a *proof that the IR is genuinely
target-independent*: the same reflected overload set survives into both
projections, and the expressiveness difference is attributable entirely to
resolution time, not to information loss. Reviewers who suspect "multi-target"
is marketing will be answered by this one result.

Corollary claim, which the paper should state explicitly:

> The set of C++ members a target can express is not a property of the target
> language. It is a property of *when* the target resolves names.

---

## 1. Contribution claims — what to claim, and what NOT to

### Claim (defensible)

| # | Claim | Evidence in repo | Gap |
|---|---|---|---|
| C1 | A single consteval walk over `T` produces a target-neutral IR; backends never touch `std::meta::info` | `include/rosetta/walk.h` (4-member visitor concept), `generate.h` (`GenClass`/`GenMethod`/`GenType`/`GenEnum`) | — |
| C2 | 19 heterogeneous targets are projections of that one IR | `include/rosetta/backends/` (19 headers), coverage.json across examples | needs one table with real per-target numbers |
| C3 | Reflection is confined to generation; emitted artifacts build with an off-the-shelf compiler | `docs/PIPELINE.md` "read twice" table | must state the inline-annotation caveat honestly (§7) |
| C4 | Binding coverage is an explicit, machine-readable, diffable property | `include/rosetta/coverage.h`, 17 `note_skip` sites across 9 backends | needs a real-library coverage study |
| C5 | The same IR projects to a runtime meta-object model, making a library scriptable without per-class codegen | `dynamic.h` (`MetaClass`/`MetaField`/`MetaMethod`/`MetaCtor`/`TypeDesc`), `script.h` | needs the O(N) vs O(1) evolution experiment |
| C6 | Late binding *recovers* expressiveness that early binding loses | overload policy table vs `resolve()` | **[TODO-1]** measure it: N overloads recovered per target |

### Do NOT claim

- **"First C++ binding generator using reflection."** Unverifiable and irrelevant.
- **"Novel meta-object protocol."** Qt's meta-object system, Graphite/GOM, Unreal's
  UObject reflection and Objective-C all predate it. The novelty is the *derivation*
  (automatic, from the language's own reflection, on unmodified headers), not the model.
- **"O(1) binding maintenance."** Only the *scriptable wrapper* is O(1) in the number
  of classes. The `dynamic` metadata tables still regenerate. Say exactly that.
- **"Better than SWIG/pybind11."** Different problem. Rosetta does not aim to beat
  pybind11 at Python; it aims to not need a pybind11-shaped effort *per target*.
- **"19 backends" as a contribution.** It is evidence of generality, not a result.

---

## 2. Related work — the section that decides acceptance

The reviewer's opening move is: *"This is Qt's meta-object system with C++26
reflection instead of moc. What is new?"* The answer must be in the abstract,
not on page 14.

### The axis matrix (build this table; it is the paper's spine)

| System | Type discovery | Header intrusion | Targets from one description | Runtime meta-model | Coverage reported |
|---|---|---|---|---|---|
| SWIG | own C++ parser | `.i` interface file | many | no | no |
| pybind11 / nanobind | hand-written | none (but hand-written per class) | 1 (Python) | no | n/a |
| Qt moc | own parser | `Q_OBJECT`, `Q_PROPERTY` | 1 (QML/JS) | **yes** | no |
| Graphite / GOM (Lévy) | manual registration | registration macros | Lua + GUI | **yes** | no |
| Unreal Header Tool | own parser | `UCLASS`/`UPROPERTY` | Blueprint | **yes** | no |
| cppyy / Cling | Clang JIT | none | 1 (Python) | yes (JIT) | no |
| Clang-AST generators (binder, litgen, …) | Clang AST | none | 1–2 | no | partial |
| **Rosetta** | **language-native (P2996)** | **none** (out-of-line annotations) | **19** | **yes** | **yes** |

Two columns carry the novelty: *language-native discovery with no intrusion*,
and *both projections from one description*. Every prior system has at most one.

`docs/PIPELINE.md` already names Graphite/GOM as the ancestor of the scriptable
model — **keep that citation, prominently**. Acknowledging the closest prior art
first is what makes the delta credible.

### Why C++26 reflection rather than a Clang AST pass — the argument to make

Not "it is more modern". The real arguments:

1. **The compiler's own answer.** Overload resolution, name hiding, access
   control, template instantiation and implicit special members are *decided by
   the compiler*. `hidden_by_derived` (`walk.h:73`) is the example: a
   `using`-less derived declaration hides base overloads, and Rosetta binds
   exactly what a C++ caller sees. A parser reimplements that rule and gets it
   subtly wrong.
2. **No second front-end to keep in sync.** SWIG and moc each carry a C++ parser
   that lags the language.
3. **Splicing, not text.** The result of reflection is spliced back into the same
   translation unit as real code, so the generated call is type-checked against
   the real declaration.

### Honest cost

Depends on `clang-p2996`, a research fork. State where it bites (step 1 only)
and where it does not (steps 3–4). Name the mitigation: the generated artifacts
outlive the fork.

---

## 3. Section plan

| § | Title | Length | Readiness |
|---|---|---|---|
| 1 | Introduction | 1.5 p | draft written |
| 2 | Motivation: the per-target cost of exposing a C++ API | 2 p | ✅ written |
| 3 | Background: C++26 static reflection and what it forces | 1 p | draft written |
| 4 | Architecture: the two-stage pipeline | 1.5 p | draft written |
| 5 | The reflection-derived IR | 2 p | draft written |
| 6 | Static projection: 19 backends | 2 p | needs coverage table |
| 7 | Dynamic projection: the runtime meta-object model | 2.5 p | draft written |
| 8 | Resolution time as the axis: what late binding recovers | 1.5 p | **the key section** — needs [TODO-1] |
| 9 | Coverage as a first-class artifact | 1.5 p | needs real-library study |
| 10 | Evaluation | 3 p | **[TODO-1..5]** |
| 11 | Case study: a geophysical library across Python / Julia / Qt / Web | 2 p | needs building |
| 12 | Limitations | 1 p | draft written |
| 13 | Related work | 2 p | matrix above |
| 14 | Conclusion | 0.5 p | — |

Target: 22–26 pages arXiv single-column, or ~14 double-column.

---

## 4. Figures and tables

| ID | What | Status |
|---|---|---|
| F1 | The two-stage pipeline (manifest → driver → P2996 → IR → targets) | adapt from `docs/slides-pipeline.md` |
| F2 | `walk<T>` fan-out: one reflective pass, N visitors | new |
| F3 | The IR as the pivot: static projection left, dynamic projection right | new — **this is the paper's key figure** |
| F4 | The `dynamic.h` → `script.h` reshaping (3 blocking shapes → 3 fixes) | table already in `docs/PIPELINE.md` |
| T1 | Per-target overload policy | exists verbatim in `docs/COVERAGE.md` |
| T2 | Coverage across examples per target | partly real (below) |
| T3 | Related-work axis matrix | §2 above |
| T4 | Binding effort: LOC per target, manual vs Rosetta | **[TODO-2]** |
| T5 | Call overhead: direct C++ / static binding / dynamic | **[TODO-3]** |

### T2 — numbers that already exist (from checked-in `coverage.json`)

| Example | target | bound | skipped |
|---|---|---|---|
| plain-binding | python | 13 | 0 |
| plain-binding | node | 13 | 0 |
| plain-binding | wasm | 13 | 0 |
| plain-binding | julia | 10 | 3 |
| geom-expanded | python / nanobind / wasm / lua | 8 | 0 |
| geom-expanded | node | 7 | 1 |
| geom-expanded | julia | 7 | 1 |
| geom-expanded | csharp / java | 2 | 6 |
| dynamic | dynamic | 31 | 1 |
| scriptable-model | python / lua / node / wasm | 108 | 1 |

The `csharp`/`java` 2-of-8 row is the most valuable line in the table: it is a
target honestly reporting that it can express only a quarter of the surface.
**Do not hide it — lead with it.** A generator that reports its own weakness is
the paper's C4 claim in one row.

---

## 5. Experiments to build

Put them in `benchmarks/`, each self-contained and scripted.

### E1 — Overload recovery — ✅ **DONE**
See `experimental-eval/E1-overload-recovery/`. Paper §10.1, Figure 2, Table 7.

Result: with `C`=8 classes × `N`=6 names and `R` overloads per name swept 1→5,
the five first-only targets (lua, node, wasm, csharp, java) bind a **constant 96
methods** while the late-bound projection tracks the library to 288. Recovery is
exactly `C·N·(R−1)` — the predicted drop, no residue. At `R`=5, **66.7% of the
API is statically unreachable and fully recoverable late-bound.**

Both controls clean: zero non-overload drops, zero reflection-stage drops.

Byproduct finding: the `typescript` backend records skips but never calls
`note_bound`, so its coverage figure is unreadable. Worth fixing in the repo.

### E2 — API evolution cost — ✅ **DONE**
See `experimental-eval/E2-api-evolution/`. Paper §10.2, Table 8.

Result at 10 → 30 → 130 classes. Static binding `93 + 67N` LOC, 4 files changed
per bump, manifest `13 + 4N` lines. Scriptable **stage 2 is constant**: 1208 LOC,
25 manifest lines, 5 s generation, **0 files changed** at every bump.

**Do NOT claim it generates less code — it generates more.** Tables (95 LOC/class)
+ fixed 1208 beat the static binding's 67 LOC/class only in *what changes*, never
in total bytes; artifact-size crossover is ~17 classes and it never catches up.
The 1208 is amortised across all libraries (it binds `rosetta::script`), which is
the real argument. Stage 1 still regenerates — say so.

### E3 — Generation cost / scalability
- Sweep classes × methods × fields × overloads.
- Measure driver compile time (the P2996 cost), generator run time, emitted LOC,
  metadata table size in `.rodata`.
- Purpose: bound the practicality of the approach. The P2996 compile is expected
  to dominate; say so.

### E4 — Runtime overhead — ✅ **DONE**
See `experimental-eval/E4-runtime-overhead/`. Paper §10.4, Table 9.

Two layers, deliberately: layer A (C++ only) isolates late binding; layer B
(Python) adds the language boundary. The difference is the interpreter's share.

- **Caching removes most of it**: 1.2× direct for trivial call / field get / set.
- **Overload resolution is the expensive lookup**: 631 ns → 14 ns cached; 174×
  direct uncached. This is E1's recovery, priced — use the two together.
- **Marshalling can't be cached away**: object return 23×, vector-1000 21× even
  cached and pre-boxed. That's the floor.
- **From Python it's only 2.3–10.8×** — the boundary already dominates.
- Rule: ~400 ns/op → 100-field panel 32 µs (fine), 1M-element loop 0.45 s (not).

Byproduct finding: generated python CMakeLists sets no `CMAKE_BUILD_TYPE`, so
the documented build produces an unoptimised module (707 ns vs 84 ns). Fix in
the emitter.

### E5 — Coverage on a real library
- A real third-party scientific C++ library, unmodified.
- Report bound/skipped per target with reason histograms.
- Answers: *what fraction of a realistic C++ API can be exposed automatically,
  and why does the remainder fail?*

### E6 — Case study (§11)
`Mesh` / `Field` / `Solver`, driven from Python (analysis), Julia (numerics),
Qt (interactive UI, generated by query over `fields()`), WASM (browser demo).
Then evolve it and show the UI picking up new properties with no binding change.

---

## 6. Title candidates

Ranked. The title must foreground *reflection → IR → two projections*.

1. **Reflection as an Interface Description Language: Static and Dynamic
   Multi-Language Projections of C++ APIs**
2. **From C++26 Reflection to a Language-Independent Meta-Object Model**
3. **One Reflective Pass, Nineteen Interfaces: A Reflection-Derived IR for
   Multi-Language C++ Binding**
4. Rosetta: Reflection-Derived Multi-Language Interfaces for C++ Scientific Software

(1) is the most accurate to what the system actually is. (4) is the safest if the
paper later goes to a scientific-software venue.

---

## 7. Limitations to state before a reviewer finds them

1. **`clang-p2996` dependency.** A research fork, step 1 only. State the blast radius.
2. **Inline annotations break the "no reflection on the target" promise.** `[[= rosetta::doc{…}]]`
   pulls `<experimental/meta>` into the user header, so pass 2 also needs the C++26
   toolchain (`docs/PIPELINE.md`). Out-of-line `.ann.json` is the escape hatch —
   present it as *load-bearing*, not as a convenience.
3. **Type-caster asymmetry.** Reading a host value is irreducibly per-language;
   only writing is shared via `visit()`/sink. ~9 methods per language, hand-written.
4. **Dynamic dispatch cost.** Linear lookup + scoring. Wrong for inner loops.
5. **Vestigial `Value`.** Bindability is decided at generation time and cannot know
   a caster will exist, so `Value` must stay in the bound class list. A real,
   small design wart — reporting it costs nothing and buys credibility.
6. **Coverage is per-member, not semantic.** A bound method can still be wrong;
   coverage counts exposure, not correctness.
7. **No formal semantics.** The IR is defined by its implementation.

---

## 8. Immediate next steps

1. E1 (overload recovery) — highest evidence-per-hour in the whole plan.
2. Related-work matrix with real citations, especially Graphite/GOM, Qt moc, cppyy.
3. E4 micro-benchmarks — the question every reviewer asks.
4. E2 evolution experiment — the one that makes the dynamic projection a *result*.
5. E6 case study.
