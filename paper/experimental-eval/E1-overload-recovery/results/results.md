# E1 — overload recovery: results

`bound` counts **methods only**. `recoverable` is the number of methods reachable through the late-bound projection that the target's own binding surface could not express.

## 8 classes x 6 names x 1 overloads (96 methods)

| target | policy | bound | overload drops | other drops | recoverable |
|---|---|---:|---:|---:|---:|
| `julia` | all-overloads | 96 | 0 | 0 | 0 |
| `nanobind` | all-overloads | 96 | 0 | 0 | 0 |
| `python` | all-overloads | 96 | 0 | 0 | 0 |
| `csharp` | first-only | 96 | 0 | 0 | 0 |
| `java` | first-only | 96 | 0 | 0 | 0 |
| `lua` | first-only | 96 | 0 | 0 | 0 |
| `node` | first-only | 96 | 0 | 0 | 0 |
| `typescript` | first-only | 96 | 0 | 0 | 0 |
| `wasm` | first-only | 96 | 0 | 0 | 0 |
| `dynamic` | late-bound | 96 | 0 | 0 | 0 |

## 8 classes x 6 names x 2 overloads (144 methods)

| target | policy | bound | overload drops | other drops | recoverable |
|---|---|---:|---:|---:|---:|
| `julia` | all-overloads | 144 | 0 | 0 | 0 |
| `nanobind` | all-overloads | 144 | 0 | 0 | 0 |
| `python` | all-overloads | 144 | 0 | 0 | 0 |
| `csharp` | first-only | 96 | 48 | 0 | 48 |
| `java` | first-only | 96 | 48 | 0 | 48 |
| `lua` | first-only | 96 | 48 | 0 | 48 |
| `node` | first-only | 96 | 48 | 0 | 48 |
| `typescript` | first-only | 96 | 48 | 0 | 48 |
| `wasm` | first-only | 96 | 48 | 0 | 48 |
| `dynamic` | late-bound | 144 | 0 | 0 | 0 |

## 8 classes x 6 names x 3 overloads (192 methods)

| target | policy | bound | overload drops | other drops | recoverable |
|---|---|---:|---:|---:|---:|
| `julia` | all-overloads | 192 | 0 | 0 | 0 |
| `nanobind` | all-overloads | 192 | 0 | 0 | 0 |
| `python` | all-overloads | 192 | 0 | 0 | 0 |
| `csharp` | first-only | 96 | 96 | 0 | 96 |
| `java` | first-only | 96 | 96 | 0 | 96 |
| `lua` | first-only | 96 | 96 | 0 | 96 |
| `node` | first-only | 96 | 96 | 0 | 96 |
| `typescript` | first-only | 96 | 96 | 0 | 96 |
| `wasm` | first-only | 96 | 96 | 0 | 96 |
| `dynamic` | late-bound | 192 | 0 | 0 | 0 |

## 8 classes x 6 names x 4 overloads (240 methods)

| target | policy | bound | overload drops | other drops | recoverable |
|---|---|---:|---:|---:|---:|
| `julia` | all-overloads | 240 | 0 | 0 | 0 |
| `nanobind` | all-overloads | 240 | 0 | 0 | 0 |
| `python` | all-overloads | 240 | 0 | 0 | 0 |
| `csharp` | first-only | 96 | 144 | 0 | 144 |
| `java` | first-only | 96 | 144 | 0 | 144 |
| `lua` | first-only | 96 | 144 | 0 | 144 |
| `node` | first-only | 96 | 144 | 0 | 144 |
| `typescript` | first-only | 96 | 144 | 0 | 144 |
| `wasm` | first-only | 96 | 144 | 0 | 144 |
| `dynamic` | late-bound | 240 | 0 | 0 | 0 |

## 8 classes x 6 names x 5 overloads (288 methods)

| target | policy | bound | overload drops | other drops | recoverable |
|---|---|---:|---:|---:|---:|
| `julia` | all-overloads | 288 | 0 | 0 | 0 |
| `nanobind` | all-overloads | 288 | 0 | 0 | 0 |
| `python` | all-overloads | 288 | 0 | 0 | 0 |
| `csharp` | first-only | 96 | 192 | 0 | 192 |
| `java` | first-only | 96 | 192 | 0 | 192 |
| `lua` | first-only | 96 | 192 | 0 | 192 |
| `node` | first-only | 96 | 192 | 0 | 192 |
| `typescript` | first-only | 96 | 192 | 0 | 192 |
| `wasm` | first-only | 96 | 192 | 0 | 192 |
| `dynamic` | late-bound | 288 | 0 | 0 | 0 |
