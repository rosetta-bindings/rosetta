# E6 — the case study's claim, checked

The library grows one class (`scene::Relaxer`). `scene.h` is byte-identical in both arms — only the manifest's class list differs — so what is measured is a binding surface growing, not a source edit.

## 1. The metadata moved (it must)

- stage-1 files changed: **4**, added: 0, removed: 0
- `Relaxer` present in the *after* tables: **True**
- `Relaxer` present in the *before* tables: **False** (must be false, or the arms are not different)

Verdict: **PASS**

Changed:

- `coverage.json`
- `dynamic/README.md`
- `dynamic/auto_dynamic.cpp`
- `markdown/scene.md`

## 2. The binding did not (the claim)

Stage 2 is a binding of `rosetta::script`. Every host imports *this*, not the library.

- stage-2 files compared: **18**
- changed: **0**, added: 0, removed: 0

Verdict: **PASS**

## Scale

The unchanged stage-2 binding is 4173 generated lines (2402 excluding build scaffolding) across 18 files. That is the artifact the library's growth did not touch.
