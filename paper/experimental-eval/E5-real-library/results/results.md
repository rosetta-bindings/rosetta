# E5 — coverage on a real library (pmp): results

## What the library declares

The frame is the compiler's own enumeration of namespace `pmp` over 40 headers (excluding `viewers/`).

| kind | count | can a manifest name it? |
|---|---:|---|
| class | 19 | yes |
| enum | 2 | yes |
| function | 99 | yes |
| class_template | 7 | **no** — a template is not an entity until instantiated |
| function_template | 39 | **no** — a template is not an entity until instantiated |

So of 166 declared entities, **120 are nameable at all** and 46 are templates — 28% of the declared surface is out of reach before any backend is consulted.

Templates are not spread evenly, which matters more than the total:

| header | templates |
|---|---:|
| mat_vec.h | 39 |
| surface_mesh.h | 4 |
| properties.h | 2 |
| algorithms/barycentric_coordinates.h | 1 |

## Overload multiplicity, measured rather than swept

E1 sweeps overloads-per-name as a free parameter because a synthetic library has no opinion about it. This is what the parameter's value actually is, for free functions, in a real API:

| overloads per name | names | declarations |
|---:|---:|---:|
| 1 | 87 | 87 |
| 2 | 3 | 6 |
| 6 | 1 | 6 |

91 names carry 99 declarations, so a first-only policy costs **8 of 99 (8%)** of the free-function surface — against the two thirds E1 measures at its swept worst case of five. The mechanism E1 isolates is real; at free-function scope this library barely exercises it.

## arm A — exhaustive (no author)

Dropped by the reflective walk, before any backend saw them — backend-independent: `function_template` 20, `hidden_by_derived` 6, `no_identifier` 12.

| target | members bound | members skipped | functions bound | functions skipped | top reasons |
|---|---:|---:|---:|---:|---|
| `python` | 123 | 34 | 64 | 27 | `unmarshalable_signature` 61 |
| `nanobind` | 123 | 34 | 64 | 27 | `unmarshalable_signature` 61 |
| `julia` | 123 | 34 | 64 | 27 | `unmarshalable_signature` 61 |
| `lua` | 104 | 53 | 64 | 27 | `unmarshalable_signature` 53, `overload_not_expressible` 27 |
| `node` | 106 | 51 | 82 | 9 | `unmarshalable_signature` 33, `overload_not_expressible` 27 |
| `wasm` | 104 | 53 | 64 | 27 | `unmarshalable_signature` 53, `overload_not_expressible` 27 |
| `csharp` | 61 | 96 | 2 | 89 | `unmarshalable_signature` 158, `overload_not_expressible` 27 |
| `java` | 61 | 96 | 2 | 89 | `unmarshalable_signature` 158, `overload_not_expressible` 27 |
| `typescript` | 148 | 27 | 90 | 1 | `overload_not_expressible` 27, `unmarshalable_signature` 1 |
| `dynamic` | 141 | 34 | 54 | 37 | `unmarshalable_type` 71 |

_`typescript` and `dynamic` count bound FIELDS in the member column; the language backends record methods only. Their member totals therefore sit over a larger denominator and are not directly comparable with the rows above — compare the function columns, which every backend records identically._

## arm B — curated (hand-written manifest)

Dropped by the reflective walk, before any backend saw them — backend-independent: `function_template` 16, `no_identifier` 12.

| target | members bound | members skipped | functions bound | functions skipped | top reasons |
|---|---:|---:|---:|---:|---|
| `python` | 100 | 28 | 49 | 10 | `unmarshalable_signature` 38 |
| `nanobind` | 100 | 28 | 49 | 10 | `unmarshalable_signature` 38 |
| `julia` | 100 | 28 | 49 | 10 | `unmarshalable_signature` 38 |
| `lua` | 81 | 47 | 49 | 10 | `unmarshalable_signature` 30, `overload_not_expressible` 27 |
| `node` | 84 | 44 | 53 | 6 | `overload_not_expressible` 27, `unmarshalable_signature` 23 |
| `wasm` | 81 | 47 | 49 | 10 | `unmarshalable_signature` 30, `overload_not_expressible` 27 |
| `csharp` | 40 | 88 | 0 | 59 | `unmarshalable_signature` 120, `overload_not_expressible` 27 |
| `java` | 40 | 88 | 0 | 59 | `unmarshalable_signature` 120, `overload_not_expressible` 27 |
| `typescript` | 101 | 27 | 59 | 0 | `overload_not_expressible` 27 |
| `dynamic` | 100 | 28 | 49 | 10 | `unmarshalable_type` 38 |

_`typescript` and `dynamic` count bound FIELDS in the member column; the language backends record methods only. Their member totals therefore sit over a larger denominator and are not directly comparable with the rows above — compare the function columns, which every backend records identically._

## What curation changed

Same library, same targets, same rosetta. The only difference is who chose the entries.

| target | exhaustive | curated | difference |
|---|---:|---:|---:|
| `python` | 187 | 149 | +38 |
| `nanobind` | 187 | 149 | +38 |
| `julia` | 187 | 149 | +38 |
| `lua` | 168 | 130 | +38 |
| `node` | 188 | 137 | +51 |
| `wasm` | 168 | 130 | +38 |
| `csharp` | 63 | 40 | +23 |
| `java` | 63 | 40 | +23 |
| `typescript` | 238 | 160 | +78 |
| `dynamic` | 195 | 149 | +46 |

## A limit above the backends

8 free-function declarations never reached a backend at all: two manifest entries that bind under one exposed name are a `rosetta_gen` error, so only one overload of a name can be requested. This binds *before* the per-target overload policy E1 measures, and it applies to every target — including the ones pybind11 and jlcxx would happily have given a full overload set.

| name | header | signature |
|---|---|---|
| `centroid` | algorithms/differential_geometry.h | `Point (const SurfaceMesh &)` |
| `cholesky_solve` | algorithms/numerics.h | `DenseMatrix (const SparseMatrix &, const DenseMatrix &, const std::function<bool (unsigned int)> &, const DenseMatrix &)` |
| `triangulate` | algorithms/triangulation.h | `void (SurfaceMesh &, Face)` |
| `operator<<` | stop_watch.h | `std::ostream &(std::ostream &, const StopWatch &)` |
| `operator<<` | surface_mesh.h | `std::ostream &(std::ostream &, Vertex)` |
| `operator<<` | surface_mesh.h | `std::ostream &(std::ostream &, Halfedge)` |
| `operator<<` | surface_mesh.h | `std::ostream &(std::ostream &, Edge)` |
| `operator<<` | surface_mesh.h | `std::ostream &(std::ostream &, Face)` |
