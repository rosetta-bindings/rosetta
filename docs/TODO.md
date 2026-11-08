## New languages

1. A plain C-ABI backend (c). Our C# and Java backends already generate a C-ABI shared library with handle-backed wrappers. If we make that a standalone backend that emits a clean rosetta_<lib>.h plus the .so, we get a lot of languages almost for free:
- Rust: bindgen, or a thin generated safe wrapper with Drop.
- Go: cgo.
- Zig: @cImport.
- Dart/Flutter: dart:ffi.
- Fortran: iso_c_binding.

Each of these then becomes a small "idiomatic wrapper over the C header" generator, not a new marshalling layer.

2. MATLAB (MEX / C++ Data API) and R (Rcpp). Both have large scientific user bases. MATLAB in particular is everywhere in geomechanics and geophysics, which matches our examples (Sift, mesh relaxer, ParaView). Rcpp modules have a structure very close to pybind11, so the generator would look like our python backend.

3. Swift. Swift 5.9+ has native C++ interop, so the backend would mostly emit a module map, API notes and Swift-side conveniences. It opens up iOS/macOS apps.


## Features beyond languages

1. An MCP server backend. We already have REST, OpenAPI and the Dynamic backend (call by name). Turning reflected methods into MCP tools would let an LLM agent drive a C++ model directly: doc{} becomes the tool description, range{} and combobox{} become the JSON Schema constraints. It's mostly the REST and OpenAPI pieces reassembled, and it's very current.

2. Zero-copy arrays. There's actually no buffer-protocol, ndarray or mdspan support. Mapping std::span, std::mdspan or contiguous std::vector<double> to NumPy arrays (Python), Float64Array (Node/Wasm) and Array (Julia) without copying matters a lot to scientific users. std::vector marshalling by copy is the bottleneck for big meshes and fields.

3. Callbacks: the remaining gaps. `std::function` parameters work in Python (pybind11 and nanobind), Node, WebAssembly, Lua, and the dynamic object model with everything built on it that has a function to offer: the MCP server (`{"$record": true}` from a JSON tool call, a real function from `run_script` / `run_python`). What's still missing:
- Julia: CxxWrap has no Julia-closure → `std::function` conversion, so we'd need an adapter (e.g. a `@cfunction` trampoline plus a captured closure).
- REST, C# and Java: the core accepts a `dyn::Function`, but a caller over HTTP or through the C ABI has no function to hand it. That needs a reverse-call path (a callback handle the C++ side calls back through), which is a protocol design. The `rosetta::script` casters (`script/python.h` and friends) do not turn a host function into a `Value` yet either.
- The dynamic model only takes callbacks as PARAMETERS. A `std::function` field or return value (C++ handing a callable to the host) is still described but not callable.
- TypeScript: a callback is typed as `any` in the `.d.ts`. It should be a typed signature such as `(value: number) => void`.
- Node, Wasm and Lua skip a callback whose signature includes a raw pointer, a nested callback or `shared_ptr`, and Node also skips one that takes a bound class by reference (`std::function<void(Thing&)>`), because of object identity. Python and the dynamic model have neither restriction.

4. Operators and protocols. Mapping operator+, operator==, operator[], begin/end and size to __add__, __eq__, __getitem__, __iter__ and __len__ (and the equivalents in other languages). Reflection can detect these automatically.

5. More of the standard library: std::variant (a union type in TypeScript and Python), std::shared_ptr, std::array, std::tuple/std::pair, and mapping exceptions to the host language.

6.  Schemas. JSON Schema on its own (for validating configs), .proto or FlatBuffers, and maybe GraphQL SDL. These are cheap because the intermediate representation already carries types and annotations.

7.  ~~An API-diff tool.~~ Done: every generation writes `api.json`, and `rosetta_gen --api-diff old new` reports breaking changes (see [API_DIFF.md](API_DIFF.md)). Possible follow-ups: a per-target diff of `coverage.json` ("python stopped binding X"), and a GitHub Action that posts the report on a PR.

8.  Tests generated from annotations. range{lo, hi} already defines what valid input is, so rosetta could emit property-based or fuzz tests (Hypothesis in Python, RapidCheck in C++) that check validation on every backend.