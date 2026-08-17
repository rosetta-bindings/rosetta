// serie::Serie from Node.
//
//     ./run.sh node        # or:  node drive.js

const serie = require("./bindings/node/serie.node");

// 3 items of itemSize 3 — a serie of 3D vectors
const v = new serie.Serie([0, 0, 0, 1, 0, 0, 0, 1, 0], 3);

console.log("describe   :", v.describe());
console.log("count      :", v.count(), "items of", v.itemSize(), "->", v.size(), "scalars");
console.log("item(1)    :", v.item(1));            // a JS Array
console.log("raw        :", v.raw());

// itemSize 1 — a serie of scalars
const s = new serie.Serie([1.0, 2.0, 4.0], 1);
console.log("scalars    :", s.scalars());
console.log("scalar(1)  :", s.scalar(1));

console.log("append     :", s.append(new serie.Serie([9.0], 1)).scalars());

// std::invalid_argument from C++ arrives as a catchable JS TypeError
try {
    v.scalars();
} catch (e) {
    console.log("scalars/3  :", e.constructor.name, "-", e.message);
}

const a = new serie.Serie([1, 2, 3, 4, 5, 6], 3);
const b = new serie.Serie([10, 10, 10], 3);        // uniform, broadcast
const w = serie.weightedSum([a, b], [2.0, 1.0]);
console.log("weightedSum:", w.describe(), w.raw());

// Callbacks: a JS function becomes the std::function the C++ signature asks
// for, through rosetta::napi_make_fn — see README.md, "Getting the templates
// back". forEach's callback returns void; map's returns a number.
const seen = [];
s.forEach((x) => seen.push(x));
console.log("forEach    :", seen);
console.log("map        :", s.map((x) => x * x).scalars());
console.log("reduce     :", s.reduce((acc, x) => acc + x, 0.0));

// a JS exception thrown mid-iteration comes back out intact
try {
    s.map((x) => { if (x > 2) throw new Error("boom at " + x); return x; });
} catch (e) {
    console.log("js throw   :", e.message);
}
