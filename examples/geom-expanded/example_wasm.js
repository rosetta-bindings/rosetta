// wasm target  (target name: geom)
// ---------------------------------------------------------------------------
// emscripten/embind module. Two differences from the node binding:
//
//  1. Loading is async — the module is compiled with -sMODULARIZE=1
//     -sEXPORT_NAME=createModule, so `require()` hands back a factory that
//     returns a Promise resolving to the module instance.
//
//  2. Objects are handles onto C++ memory: anything you `new`, and anything a
//     call hands back by value, must be released with .delete(). JS has no
//     finaliser embind can hook.
//
// std::vector is NOT one of the differences — the generated binding registers
// it on the emval wire, so a plain Array goes in and a plain Array comes out,
// the same as in the node binding. (An Array *of objects* still holds handles,
// so its elements need deleting even though the Array itself does not.)
//
// The same geom.js/geom.wasm pair runs in the browser too (-sENVIRONMENT=node,web).
//
//   Build:  emcmake cmake -S bindings/wasm -B bindings/wasm/build
//           cmake --build bindings/wasm/build -j
//   Run:    node example_wasm.js

const path = require("path");
const createModule = require(
    path.join(__dirname, "bindings", "wasm", "build", "geom.js")
);

createModule().then((geom) => {
    // The std::vector arguments the Surface constructor expects are Arrays.
    const surface = new geom.Surface([0, 0, 0, 1, 0, 0, 0, 1, 0], [0, 1, 2]);

    const model = new geom.Model();
    model.addSurface(surface);

    // An Array — but of Surface handles, each a copy that has to be released.
    for (const s of model.getSurfaces()) {
        console.log("Model surface points");
        for (const p of s.getPoints()) {
            console.log(" ", p.x, p.y, p.z);
            p.delete();
        }

        console.log("Model surface triangles");
        for (const t of s.getTriangles()) {
            console.log(" ", t.a, t.b, t.c);
            t.delete();
        }

        s.delete();
    }

    // `transform` is a free function bound from common.h: it swizzles a Point
    // to (x*2, z*3, y*4).
    const p = new geom.Point(1, 2, 3);
    const q = geom.transform(p);
    console.log("transform(1, 2, 3) =>", q.x, q.y, q.z);
    p.delete();
    q.delete();

    // Field validation generated from Triangle.ann.json (range 0..1000000) is
    // enforced in C++: assigning out of range throws.
    const t = new geom.Triangle(0, 1, 2);
    try {
        t.a = -1;
    } catch (e) {
        console.log("range check fired:", e.message || e);
    }
    t.delete();

    model.delete();
    surface.delete();
});
