# Experimental evaluation

The measurement campaign behind the paper (`../rosetta.tex`, §10). Each
experiment is self-contained: its own generator, its own `run.sh`, its own
analysis, and a `results/` directory holding what the paper cites.

| # | Experiment | Status | Needs |
|---|---|---|---|
| [E1](E1-overload-recovery/) | Overload recovery — what late binding regains | ✅ done | `clang-p2996` only |
| [E2](E2-api-evolution/) | API evolution cost — what has to change when a library grows | ✅ done | `clang-p2996` only |
| [E3](E3-generation-cost/) | Generation cost and scalability — where the time goes, and how it scales | ✅ done | `clang-p2996` + a system C++ compiler |
| [E4](E4-runtime-overhead/) | Runtime overhead — what late binding costs per call | ✅ done | + C++ compiler, Python, pybind11 |
| [E5](E5-real-library/) | Coverage on a real third-party library (PMP) — exhaustive vs curated | ✅ done | `clang-p2996` + a system C++ compiler |

## Conventions

**Only measure what the stage produces.** E1, E2, E3 and E5 need the *generation*
stage alone, because `coverage.json` and the emitted sources exist before
anything is compiled. Keeping an experiment inside that stage removes the
emsdk / JDK / .NET / Julia toolchain requirements and makes it reproducible on
any machine that can build the driver. Reach for a full build only when the
question is genuinely about runtime (E4) or build cost (E3).

**Record the ground truth next to the inputs.** Each generated configuration
writes an `expected.json` stating what the library contains, so the analysis
never re-derives its baseline from the output it is judging. E5 cannot generate
its subject, so it does the same thing one step earlier: `frame.json` records
what the *compiler* says the real library declares, established before any
manifest is written.

**Report confounds separately, never folded in.** Every analysis separates the
effect under test from everything else that could reduce a count (`other drops`
in E1). A number that silently mixes the two is not evidence.

**Say what was not measured.** If an instrument is broken, flag it in the
output rather than letting it average into a result. Two were, and both were
caught this way rather than published: `typescript` recorded no bound members at
all (E1), and no backend recorded free functions at all (E5). Both are fixed;
the analyses still detect the shape (`skips > 0` with `bound == 0`) so a
regression cannot pass silently.

## Reproducing

```sh
cd E1-overload-recovery && ./run.sh
```

Each experiment's README states its prerequisites and expected wall-clock.
`bin/rosetta_gen` must exist; build it with:

```sh
cmake -G Ninja -S tools/rosetta_gen -B tools/rosetta_gen/build
cmake --build tools/rosetta_gen/build
```
