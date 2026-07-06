## Geogram binding
Split into what rosetta must fix (compile-robustness) and what it would need to make a direct `GEO::Mesh` binding actually usable. Five features, in increasing order of effort:

1. Bindability gate on fields (small, worth doing regardless). Today `walk.hxx` pushes every public data member into the IR and the expanded backends emit def_readwrite unconditionally — that's why MeshVertices vertices is a hard compile error. Mirror what we already do for trampoline signatures (sig_type_bindable): at reflection time, classify each field — copy-assignable + marshallable → def_readwrite; copyable only → def_readonly; neither (unless it's a bound class, see #2) → skip. With just this, `GEO::Mesh` compiles when listed in the manifest — as an opaque handle with `clear()`, `copy()`, `show_stats()`… but still no way to move geometry.

2. Reference-semantics properties for class-typed fields (the big one). The natural direct binding is mesh.vertices returning a borrowed handle to a bound MeshVertices — no copy, lifetime tied to the mesh. pybind supports this today (def_readonly on a registered type is reference_internal), embind is pointer-based too, but rosetta's node runtime can't represent it: `Wrap<T>` stores Tramp inner by value, so a non-owning reference has no representation — and a non-default-constructible class like MeshVertices (ctor takes `Mesh&`) can't even instantiate Wrap. The structural change is storing `T*` + an owned flag in Wrap, with a second creation path "wrap this existing pointer, don't delete it". That's a real refactor across the node backend (and the expanded emitters), plus dangling-handle policy questions (what happens when the mesh dies first). This is the feature that makes "sub-object APIs" work in general — Qt-ish libraries would benefit too.

3. A sequence-trait instead of hardcoded std::vector (moderate). The marshalling layers special-case `std::vector<T>`. Generalize to a concept (`size()`, `operator[]`, value_type, contiguous) or an opt-in trait specialization, and `GEO::vector<double>` crosses the boundary like a vector. Same mechanism would cover small fixed-size types (vec3 ↔ [x,y,z]).

4. Extension methods in the manifest (small-medium, best value). Even with #1–#3, the remaining holes are inherent: point_ptr() returns a bare `double*` (no size — nothing any binding framework can do), UVs live in an `Attribute<double>` that only exists as a template instantiation, mesh_load is overloaded. Somebody has to write the ~8 conversion functions; the question is only where they live. Let the manifest attach free functions to a bound class as methods:

```json
"classes": [{
  "name": "GEO::Mesh", "header": "geogram/mesh/mesh.h",
  "extensions": [
    {"name": "georo::set_surface", "header": "mesh_ext.h"},
    {"name": "georo::vertices",    "header": "mesh_ext.h"}
  ]
}]
```

where void `georo::set_surface(GEO::Mesh&, const std::vector<double>&, const std::vector<int>&)` binds as `mesh.set_surface(...)`. That dissolves the facade class: no wrapper type, no pImpl, no parallel Mesh — just a header of stateless glue functions, and scripts hold real `GEO::Mesh` objects that geogram's directly-bound free functions (`GEO::remesh_smooth`, `GEO::mesh_repair`, …) accept without any translation layer.

5. The lifecycle + odds and ends. A manifest "module_init": "georo::init" hook emitted into `PYBIND11_MODULE` / `Init` / `EMSCRIPTEN_BINDINGS` (for `GEO::initialize()` + `CmdLine::import_arg_group`); overload selection by explicit signature (`{"name": "GEO::mesh_load", "signature": "bool(const std::string&, GEO::Mesh&)"}` — resolvable at generation time by filtering the namespace's members by type instead of splicing `^^name`); and `shared_ptr<T>` returns for `CSGCompiler::compile_string` (pybind holders and embind smart-ptr support exist; node again needs the #2 refactor first).

Best method(s): #1 + #4 + the module_init hook give us "no facade" for geogram at a fraction of the cost — GEO::Mesh bound directly, geogram's algorithm functions bound directly, and the glue reduced from a wrapper class to a flat header of extension functions. #2 is the architecturally interesting feature (it's what "rosetta handles sub-object APIs" really means) but it's the expensive one, and even with it, per-point access through mesh.vertices.point(v) would be miserably slow from Python/JS compared to one flat-array extension call — so #4 stays the workhorse either way. One caveat to verify early: without the pImpl shield, the generator will parse geogram's headers under clang-p2996 in C++26 mode — geogram is clean C++17 so it should go through, but that's an assumption worth a 5-minute smoke test before committing to the design.
