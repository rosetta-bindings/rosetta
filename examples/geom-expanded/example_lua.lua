-- lua-expanded (sol2) demo — module "luageom".
--
-- Build first (needs Lua 5.1–5.4 or LuaJIT; sol2 is fetched automatically):
--   cmake -S bindings/lua-expanded -B bindings/lua-expanded/build
--   cmake --build bindings/lua-expanded/build -j
-- then run WITH THE SAME LUA VERSION THE MODULE WAS BUILT AGAINST — the CMake
-- prints it ("Lua headers: ..."). Homebrew's plain `lua` is 5.5, which sol2
-- does not support; the build therefore prefers lua@5.4, so run e.g.:
--   /opt/homebrew/opt/lua@5.4/bin/lua example_lua.lua
-- (A mismatched interpreter is refused with a "version mismatch" error.)
--
-- Mirrors example_pybind11.py: same classes, same free function — the module
-- is a plain `require`-able C module (luaopen_luageom in luageom.so).

package.cpath = package.cpath .. ";./bindings/lua-expanded/?.so"
local geom = require("luageom")

-- A Surface from plain Lua tables (positions, triangle indices): the binding
-- adds a table-accepting overload beside the exact std::vector constructor.
local s = geom.Surface.new({ 0, 0, 0, 1, 0, 0, 0, 1, 0 }, { 0, 1, 2 })

local model = geom.Model.new()
model:addSurface(s)

-- getSurfaces() returns the C++ vector as a reference-semantics userdata:
-- #, [i] (1-based) and ipairs all work without copying.
for _, surf in ipairs(model:getSurfaces()) do
    local pts, tris = surf:getPoints(), surf:getTriangles()
    for i = 1, #pts do
        local p = pts[i]
        print(string.format("point %d    : (%g, %g, %g)", i, p.x, p.y, p.z))
    end
    for i = 1, #tris do
        local t = tris[i]
        print(string.format("triangle %d : (%d, %d, %d)  kind=%d", i, t.a, t.b, t.c, t.kind))
    end
end

-- `transform` is a free (non-member) function bound from common.h. It takes a
-- Point and returns a new Point swizzled to (x*2, z*3, y*4).
local p = geom.Point.new(1, 2, 3)
local q = geom.transform(p)
print(string.format("transform  : (%g, %g, %g) -> (%g, %g, %g)", p.x, p.y, p.z, q.x, q.y, q.z))
assert(q.x == 2 and q.y == 9 and q.z == 8)

-- Surface:transform takes a std::function<Point(const Point&)> — a plain Lua
-- function converts natively (called synchronously from the C++ loop).
s:transform(function(pt) return geom.Point.new(pt.x + 10, pt.y + 10, pt.z + 10) end)
local moved = s:getPoints()[1]
print(string.format("callback   : first point now (%g, %g, %g)", moved.x, moved.y, moved.z))
assert(moved.x == 10 and moved.y == 10 and moved.z == 10)

-- Triangle.a/b/c carry an out-of-line range annotation [0, 10000] — the
-- generated setter validates it.
local t = geom.Triangle.new(1, 2, 3)
local ok, err = pcall(function() t.a = -5 end)
assert(not ok, "range validation should reject -5")
print("range      : t.a = -5 rejected (" .. tostring(err) .. ")")

-- Kind is a read-only enum table.
assert(geom.Kind.Surface ~= nil)
print("enum       : Kind.Surface =", geom.Kind.Surface)

print("done.")
