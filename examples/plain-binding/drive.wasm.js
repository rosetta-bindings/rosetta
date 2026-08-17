// serie::Serie from WebAssembly, under Node.
//
//     ./run.sh wasm        # or:  node drive.wasm.js
//
// NOT VERIFIED: there was no emsdk on the machine this example was written on,
// so the wasm target was skipped and this driver has never run. The shape is
// the one bindings/wasm/README.md documents — a MODULARIZE'd factory called
// createModule. Treat the first run as the review.

const createModule = require("./bindings/wasm/build/serie.js");

createModule().then((M) => {
    const v = new M.Serie([0, 0, 0, 1, 0, 0, 0, 1, 0], 3);

    console.log("describe   :", v.describe());
    console.log("count      :", v.count(), "items of", v.itemSize());
    console.log("item(1)    :", v.item(1));
    console.log("raw        :", v.raw());

    // embind hands out C++ objects the caller must release
    v.delete();
});
