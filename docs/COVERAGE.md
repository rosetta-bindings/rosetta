# Overloads and the coverage report

Two features that ship together, because the second is what makes the first
readable: rosetta now binds **overload sets**, and every generation writes a
machine-readable **`coverage.json`** saying what bound, what did not, and why.

---

## 1. Overloads

Until now the reflection walk deduplicated member functions **by name**. Of

```cpp
struct Grid {
    double at(int i) const;
    double at(int i, int j) const;
};
```

exactly one entry reached the IR and the other disappeared — with no diagnostic,
no note, nothing. The missing method looked identical to a method that had never
been asked for.

The walk now deduplicates by **(name, signature)**, so the whole set arrives.
What happens next depends on the target language, because "two functions with one
name" is not something every runtime can express.

### Per-target policy

| Target | Policy | Why |
|---|---|---|
| python (pybind11) | **all overloads** | repeated `.def("at", …)` builds one overload set; pybind dispatches on argument types |
| nanobind | **all overloads** | same model as pybind11 *(previously skipped overload sets entirely)* |
| julia (jlcxx) | **all overloads** | each becomes a Julia method; Julia's multiple dispatch picks |
| qt | **all overloads** | builds an independent widget row per method — nothing is keyed by name |
| wasm (embind) | first only | a second `.function("at", …)` throws `BindingError` at module init |
| node (N-API) | first only | property descriptors are keyed by name; JS has no type dispatch |
| lua (sol2) | first only | `c["at"] = …` is an assignment — a second one overwrites *(previously skipped the whole set)* |
| csharp / java | first only | the op table is a name-keyed map, and marshalling goes through JSON |
| typescript | first only | the `.d.ts` describes the N-API module, so it must not promise what node did not bind |
| rest | first only | a route is a URL path; two overloads would register the same path |
| qml | first only | `registerInvoker` is keyed by name and QML calls with an untyped `QVariantList` |

"First" always means **first-declared**, which is stable across regenerations.

Every dropped overload appears in `coverage.json` with the reason
`overload_not_expressible`, so you can see exactly what a name-keyed target left
behind — and reach it with a manifest
[extension method](MANIFEST.md#extensions) under a distinct name if you need it.

### C++ name hiding is honoured

```cpp
struct Base    { int f(int) const; int f(int, int) const; };
struct Derived : Base { int f(double) const; };
```

`Derived` binds **only** `f(double)`. That is what a C++ caller sees without a
`using Base::f`, so binding the hidden base overloads would hand scripts a call
the C++ API does not offer. Both hidden declarations are recorded as
`hidden_by_derived` drops rather than vanishing.

A diamond-shared base member is still emitted exactly once. Two *different*
bases declaring the same name both bind — that call is ambiguous in C++ without
qualification, but the binding has no ambiguity to resolve (each entry is
spliced from its own reflection), so binding both beats binding neither.

### Note for backend authors

`&T::name` names an **overload set**, not a function, so any emitter that spells
a member pointer for an overloaded name must cast it to the exact signature.
Use the shared helper:

```cpp
const std::string mp = px_member_pointer(k, m);   // &T::f, or a static_cast when needed
```

The gate is `GenMethod::is_overloaded` (does the *C++ class* overload the name?),
**not** `overload_count` (how many entries reached the IR) — a set whose siblings
were all gated out still needs the cast. Fixing this also repaired the C# and
Java backends, which previously emitted a bare `&T::f` that could not compile for
any overloaded method.

---

## 2. `coverage.json`

`generate()` writes it to `<out_dir>/coverage.json` on every run, always — an
empty skip list is itself the useful answer, and a file that is always present is
one you can diff.

```jsonc
{
  "rosetta_coverage": 1,
  "reflection": [                       // never reached ANY backend
    { "class": "Shape",
      "dropped": [
        {"member": "operator<", "signature": "bool(const Shape &) const",
         "reason": "no_identifier"},
        {"member": "debug", "signature": "", "reason": "function_template"}
      ] }
  ],
  "targets": [
    { "target": "wasm",
      "bound": 3, "skipped": 1,
      "classes": [
        { "class": "Shape",
          "bound":   [ {"member": "area", "signature": "double () const"} ],
          "skipped": [ {"member": "scale", "signature": "void (double, double)",
                        "reason": "overload_not_expressible",
                        "detail": "the target binds methods by name and scale is …"} ] }
      ] }
  ]
}
```

### The two stages

**`reflection`** — members the walk never handed to any backend. Backend-independent:

| reason | meaning |
|---|---|
| `no_identifier` | an operator or conversion function — no name to bind to |
| `function_template` | a member template: no fixed parameter pack to splice |
| `hidden_by_derived` | a base overload hidden by a derived declaration of the name |

**`targets`** — what each backend's own gates decided:

| reason | meaning |
|---|---|
| `unmarshalable_signature` | no conversion for a type in the signature; `detail` names it |
| `overload_not_expressible` | the target keys methods by name and this one lost the tie |
| `sequence_not_adaptable` | a registered sequence with no `std::vector` boundary adapter here |
| `static_callback_receiver` | embind cannot give a static member the receiver a callback adapter needs |
| `extension_has_no_member_pointer` | this backend dispatches through a member pointer |

Both the **bound** and **skipped** sides are recorded. Counting only skips cannot
tell "everything bound" apart from "this class never reached this backend" — the
distinction that matters when a whole target silently produces nothing.

### Using it

The point is the diff. Check `coverage.json` into your repo next to the manifest
and a method that stops binding becomes a reviewable line in a pull request
instead of an `AttributeError` a week later. To gate CI on it:

```sh
jq -e '[.targets[].skipped] | add == 0' bindings/coverage.json
```

or, more usefully, allow known skips and fail on new ones by diffing against a
committed baseline.

### Instrumenting another backend

Skips are recorded at the existing gate sites — one line each:

```cpp
for (const auto &m : k.methods) {
    if (!coverage::emit_overload(coverage::overloads::first_only, "my-backend", k, m)) {
        continue;                                   // records the dropped overload
    }
    if (!my_method_ok(m, c)) {
        coverage::note_skip("my-backend", k, m, "unmarshalable_signature",
                            my_skip_reason(m, c));  // a slug plus a human sentence
        continue;
    }
    segs.push_back(my_method(k, m));
    coverage::note_bound("my-backend", k, m);
}
```

Keep the reason function next to the predicate it explains: a reason that drifts
out of step with the decision is worse than no reason at all.

Backends instrumented today: python-, nanobind-, node-, wasm-, lua-,
julia-, csharp- and java, plus typescript. The others still bind
correctly — they simply do not yet contribute rows to the report.
