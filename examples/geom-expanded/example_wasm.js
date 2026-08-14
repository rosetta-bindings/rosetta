// wasm target  (target name: geom)
// ---------------------------------------------------------------------------
// emscripten/embind module. Two differences from the node binding:
//
//  1. Loading is async — the module is compiled with -sMODULARIZE=1
//     -sEXPORT_NAME=createModule, so `require()` hands back a factory that
//     returns a Promise resolving to the module instance.
//
//  2. std::vector is NOT a JS Array here. embind exposes the registered vector
//     types (Module.vector_double, Module.vector_int, ...) which you fill with
//     push_back() and read with .size()/.get(i). Objects you `new` from JS own
//     C++ memory and must be released with .delete().
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

function toVector(VecType, values) {
    const v = new VecType();
    for (const x of values) v.push_back(x);
    return v;
}

createModule().then((geom) => {
    // Build the std::vector arguments the Surface constructor expects.
    const positions = toVector(geom.vector_double, [0, 0, 0, 1, 0, 0, 0, 1, 0]);
    const indices = toVector(geom.vector_int, [0, 1, 2]);

    const surface = new geom.Surface(positions, indices);
    positions.delete();
    indices.delete();

    const model = new geom.Model();
    model.addSurface(surface);

    const surfaces = model.getSurfaces();
    for (let i = 0; i < surfaces.size(); ++i) {
        const s = surfaces.get(i);

        console.log("Model surface points");
        const pts = s.getPoints();
        for (let k = 0; k < pts.size(); ++k) {
            const p = pts.get(k);
            console.log(" ", p.x, p.y, p.z);
            p.delete();
        }
        pts.delete();

        console.log("Model surface triangles");
        const tris = s.getTriangles();
        for (let k = 0; k < tris.size(); ++k) {
            const t = tris.get(k);
            console.log(" ", t.a, t.b, t.c);
            t.delete();
        }
        tris.delete();

        s.delete();
    }
    surfaces.delete();

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
