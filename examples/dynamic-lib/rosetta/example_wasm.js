// wasm binding for the `space` library.
//
//   # first build space as a wasm static archive with the SAME emsdk:
//   emcmake cmake -S space -B space/build-wasm && cmake --build space/build-wasm
//   # then the embind module (links libspace.a statically — no .dylib, no rpath):
//   emcmake cmake -S rosetta/bindings/wasm -B rosetta/bindings/wasm/build
//   cmake --build rosetta/bindings/wasm/build
//   node example_wasm.js
//
// Unlike the node/python bindings (which dlopen libspace.dylib at run time),
// wasm cannot link a native shared object: every method body is pulled in from
// the wasm static archive libspace.a and baked into space.wasm at link time.

const path = require("path");
// Emscripten MODULARIZE=1, EXPORT_NAME=createModule — the module is an async
// factory returning a promise for the instantiated wasm instance.
const createModule = require(path.join(__dirname, "bindings", "wasm", "build", "space.js"));

createModule().then((space) => {
    const v = new space.Vector3(3, 0, 4);
    console.log("Vector3(3, 0, 4).length() =", v.length()); // 5 — from libspace.a
    const n = v.normalized();
    console.log("           .normalized() =", [n.x, n.y, n.z]); // [0.6, 0, 0.8]

    const a = new space.Vector3(1, 2, 3);
    const b = new space.Vector3(4, 5, 6);
    console.log("a.dot(b)   =", a.dot(b)); // 32
    const c = a.cross(b);
    console.log("a.cross(b) =", [c.x, c.y, c.z]); // [-3, 6, -3]

    const box = new space.BoundingBox(new space.Vector3(0, 0, 0), new space.Vector3(2, 2, 2));
    const ctr = box.center();
    console.log("box.center()   =", [ctr.x, ctr.y, ctr.z]); // [1, 1, 1]
    console.log("box.diagonal() =", box.diagonal().toFixed(4)); // 3.4641
    console.log("box.contains((1,1,1)) =", box.contains(new space.Vector3(1, 1, 1)));

    // embind hands back C++ objects by value as handles — release them so the
    // wasm heap doesn't leak (embind has no GC finalizer for these).
    [v, n, a, b, c, box, ctr].forEach((h) => h.delete());
});
