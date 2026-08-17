// The same walk as drive.py, in Node.
//
//     ../../bin/rosetta_gen --build manifest.json
//     node drive.js

const meta = require("./bindings/node/rosetta_meta.node");

for (const k of meta.classes()) {
    console.log(k.qualified());
    for (const f of k.fields()) {
        const bits = [];
        if (f.readonly()) bits.push("readonly");
        if (f.has_range()) bits.push(`range [${f.range_min()}, ${f.range_max()}]`);
        if (f.choices().length) bits.push("choices " + f.choices().join("/"));
        if (f.type().kind() === "enum") bits.push("enum " + f.type().enumerator_names().join("/"));
        console.log(`    ${f.name().padEnd(10)} ${f.type().spelling().padEnd(20)} ${bits.join("; ")}`);
    }
}

console.log("\n--- live objects ---");
const M = meta.find_class("scene::Mesh");
const m = meta.create("scene::Mesh", []).value();      // -> Instance

console.log("describe    :", m.call("describe", []).value());
m.set("opacity", 0.5);                                  // a JS number, not a Value
console.log("opacity     :", m.get("opacity").value());
console.log("set 99      :", m.set("opacity", 99).error());

m.set("weights", [0.25, 0.5, 1]);                       // a JS array -> std::vector<double>
console.log("weights     :", m.get("weights").value());

const cube = M.call_static("cube", []).value();
console.log("at(0, 1)    :", cube.call("at", [0, 1]).value());
console.log("origin.x    :", m.get("origin").value().get("x").value());
console.log("no match    :", M.why_no_match("at", ["nope"]));
