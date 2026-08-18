// serie::Serie from WebAssembly, under Node.
//
//     ./run.sh wasm        # or:  node drive.wasm.js
//
// Line for line drive.js, modulo the two things embind really does differently:
// C++ objects are handles the caller must .delete(), and a C++ throw arrives as
// an emscripten CppException rather than a TypeError. Vectors are NOT among the
// differences — the generated binding puts std::vector<T> on the emval wire, so
// `[0, 0, 0]` goes in and an Array comes out, exactly as in the other three.

const createModule = require("./bindings/wasm/build/serie.js");

createModule().then((M) => {
    // 3 items of itemSize 3 — a serie of 3D vectors
    const v = new M.Serie([0, 0, 0, 1, 0, 0, 0, 1, 0], 3);

    console.log("describe   :", v.describe());
    console.log("count      :", v.count(), "items of", v.itemSize(), "->", v.size(), "scalars");
    console.log("item(1)    :", v.item(1));            // a JS Array
    console.log("raw        :", v.raw());

    // itemSize 1 — a serie of scalars
    const s = new M.Serie([1.0, 2.0, 4.0], 1);
    console.log("scalars    :", s.scalars());
    console.log("scalar(1)  :", s.scalar(1));

    const tail = new M.Serie([9.0], 1);
    // append returns Serie& — a borrowed handle to `s` itself, not ours to free
    console.log("append     :", s.append(tail).scalars());
    tail.delete();

    // std::invalid_argument from C++ arrives as a catchable CppException
    // carrying what() — where the N-API backend raises a TypeError.
    try {
        v.scalars();
    } catch (e) {
        console.log("scalars/3  :", e.constructor.name, "-", String(e));
    }

    const a = new M.Serie([1, 2, 3, 4, 5, 6], 3);
    const b = new M.Serie([10, 10, 10], 3);            // uniform, broadcast
    const w = M.weightedSum([a, b], [2.0, 1.0]);       // vector<Serie> is an Array too
    console.log("weightedSum:", w.describe(), w.raw());

    // Callbacks: a JS function becomes the std::function the C++ signature asks
    // for, through the rosetta_wx::make_fn shim the generator emits — see
    // README.md, "Getting the templates back". forEach's callback returns void;
    // map's returns a number.
    const seen = [];
    s.forEach((x) => seen.push(x));
    console.log("forEach    :", seen);

    const sq = s.map((x) => x * x);                    // Serie by value: we own it
    console.log("map        :", sq.scalars());
    console.log("reduce     :", s.reduce((acc, x) => acc + x, 0.0));

    // a JS exception thrown mid-iteration comes back out through wasm intact
    try {
        s.map((x) => { if (x > 2) throw new Error("boom at " + x); return x; });
    } catch (e) {
        console.log("js throw   :", e.message);
    }

    // embind hands out C++ objects the caller must release
    for (const o of [v, s, a, b, w, sq]) o.delete();
});
