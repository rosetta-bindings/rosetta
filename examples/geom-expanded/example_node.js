// node target  (target name: jsgeom)
// ---------------------------------------------------------------------------
// Plain N-API bindings. The node_runtime.h marshaller converts std::vector
// <-> JS Array transparently, so you pass and receive ordinary JS arrays —
// no wrapper vector types, no manual memory management.
//
//   Build:  ( cd bindings/node && npm install && npm run build )
//   Run:    node example_node.js

const path = require("path");

// CMakeLists' POST_BUILD step copies the built addon next to its source dir.
const geom = require(
    path.join(__dirname, "bindings", "node", "jsgeom.node")
);

// A Surface is built from a flat positions array (x,y,z, x,y,z, ...) and a
// flat triangle-index array. Plain JS arrays cross the boundary directly.
const surface = new geom.Surface([0, 0, 0, 1, 0, 0, 0, 1, 0], [0, 1, 2]);

const model = new geom.Model();
model.addSurface(surface);

for (const s of model.getSurfaces()) {
    console.log("Model surface points");
    for (const p of s.getPoints()) {
        console.log(" ", p.x, p.y, p.z);
    }
    console.log("Model surface triangles");
    for (const t of s.getTriangles()) {
        console.log(" ", t.a, t.b, t.c);
    }
}

// `transform` is a free (non-member) function bound from common.h. It takes a
// Point and returns a new Point swizzled to (x*2, z*3, y*4).
const p = new geom.Point(1, 2, 3);
const q = geom.transform(p);
console.log("transform(1, 2, 3) =>", q.x, q.y, q.z);

// Field validation generated from Triangle.ann.json (range 0..1000000) is
// enforced in C++: assigning out of range throws.
const t = new geom.Triangle(0, 1, 2);
try {
    t.a = -1;
} catch (e) {
    console.log("range check fired:", e.message);
}
