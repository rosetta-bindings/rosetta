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

3. Callbacks (std::function parameters). These are currently skipped. Passing a Python, JS or Lua function into C++ (progress callbacks, user-defined functions in solvers) is probably the most-asked-for gap once people use the bindings for real.

4. Operators and protocols. Mapping operator+, operator==, operator[], begin/end and size to __add__, __eq__, __getitem__, __iter__ and __len__ (and the equivalents in other languages). Reflection can detect these automatically.

5. More of the standard library: std::variant (a union type in TypeScript and Python), std::shared_ptr, std::array, std::tuple/std::pair, and mapping exceptions to the host language.

6.  Schemas. JSON Schema on its own (for validating configs), .proto or FlatBuffers, and maybe GraphQL SDL. These are cheap because the intermediate representation already carries types and annotations.

7.  An API-diff tool. Dump the intermediate representation per release and compare versions to report breaking changes (removed or renamed members, changed signatures). It's a nice use of reflection that most binding tools can't offer.

8.  Tests generated from annotations. range{lo, hi} already defines what valid input is, so rosetta could emit property-based or fuzz tests (Hypothesis in Python, RapidCheck in C++) that check validation on every backend.