-- The same walk as drive.py, in Lua.
--
--     ../run.sh lua

local meta = require("rosetta_meta")

for _, k in ipairs(meta.classes()) do
    print(k:qualified())
    for _, f in ipairs(k:fields()) do
        print(("    %-8s %s"):format(f:name(), f:type():spelling()))
    end
    for _, m in ipairs(k:methods()) do
        local args = {}
        for _, p in ipairs(m:params()) do args[#args + 1] = p:type():spelling() end
        print(("    %s(%s) -> %s%s"):format(m:name(), table.concat(args, ", "),
                                            m:ret():spelling(),
                                            m:is_static() and " [static]" or ""))
    end
end

print("\n--- a live account ---")
local Account = meta.find_class("bank::Account")

local a = Account:call_static("open", { "ada", 100.0 }):value()
print("describe :", a:call("describe", {}):value())

a:call("deposit", { 50 })
print("balance  :", a:get("balance"):value())
print("withdraw :", a:call("withdraw", { 500 }):value(), "(more than the balance)")

a:set("frozen", true)
a:call("deposit", { 10 })
print("frozen   :", a:get("frozen"):value(), "-> balance still", a:get("balance"):value())
