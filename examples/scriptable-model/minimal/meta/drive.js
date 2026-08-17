// The same walk as drive.py, in Node.
//
//     ../run.sh node

const meta = require("./bindings/node/rosetta_meta.node");

for (const k of meta.classes()) {
    console.log(k.qualified());
    for (const f of k.fields()) {
        console.log(`    ${f.name().padEnd(8)} ${f.type().spelling()}`);
    }
    for (const m of k.methods()) {
        const args = m.params().map((p) => p.type().spelling()).join(", ");
        console.log(`    ${m.name()}(${args}) -> ${m.ret().spelling()}` +
                    (m.is_static() ? " [static]" : ""));
    }
}

console.log("\n--- a live account ---");
const Account = meta.find_class("bank::Account");

const a = Account.call_static("open", ["ada", 100.0]).value();   // -> Instance
console.log("describe :", a.call("describe", []).value());

a.call("deposit", [50]);
console.log("balance  :", a.get("balance").value());

console.log("withdraw :", a.call("withdraw", [500]).value(), "(more than the balance)");

a.set("frozen", true);
a.call("deposit", [10]);
console.log("frozen   :", a.get("frozen").value(),
            "-> deposit ignored, balance still", a.get("balance").value());

console.log("\n--- a form, built by query ---");
const widgets = { boolean: "checkbox", number: "spinbox", string: "text" };
for (const f of Account.fields()) {
    const w = widgets[f.type().kind()] || "text";
    console.log(`    ${f.name().padEnd(8)} ${w.padEnd(9)} = ` +
                JSON.stringify(a.get(f.name()).value()));
}
