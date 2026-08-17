-- The same walk as drive.py, in Lua — the point being that it IS the same walk.
--
--     ../../bin/rosetta_gen --build manifest.json
--     cd bindings/lua && lua ../../drive.lua

local meta = require("rosetta_meta")

for _, k in ipairs(meta.classes()) do
    print(k:qualified())
    for _, f in ipairs(k:fields()) do
        local bits = {}
        if f:readonly()  then bits[#bits + 1] = "readonly" end
        if f:has_range() then bits[#bits + 1] = ("range [%g, %g]"):format(f:range_min(), f:range_max()) end
        if #f:choices() > 0 then bits[#bits + 1] = "choices " .. table.concat(f:choices(), "/") end
        if f:type():kind() == "enum" then
            bits[#bits + 1] = "enum " .. table.concat(f:type():enumerator_names(), "/")
        end
        print(("    %-10s %-20s %s"):format(f:name(), f:type():spelling(), table.concat(bits, "; ")))
    end
end

print("\n--- live objects ---")
local M = meta.find_class("scene::Mesh")
local m = meta.create("scene::Mesh", {}):value()      -- -> Instance

print("describe    :", m:call("describe", {}):value())
m:set("opacity", 0.5)                                  -- a Lua number, not a Value
print("opacity     :", m:get("opacity"):value())
print("set 99      :", m:set("opacity", 99):error())

m:set("weights", { 0.25, 0.5, 1 })                     -- a Lua table -> std::vector<double>
print("weights     :", table.concat(m:get("weights"):value(), ", "))

local cube = M:call_static("cube", {}):value()
print("at(0, 1)    :", cube:call("at", { 0, 1 }):value())
print("origin.x    :", m:get("origin"):value():get("x"):value())
print("no match    :", M:why_no_match("at", { "nope" }))
