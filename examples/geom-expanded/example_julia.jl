# julia-expanded (CxxWrap / jlcxx) demo — module "jlgeom".
#
# Build first (needs Julia with the CxxWrap package installed):
#   cmake -S bindings/julia-expanded -B bindings/julia-expanded/build
#   cmake --build bindings/julia-expanded/build -j
# then run:
#   julia example_julia.jl
#
# Mirrors example_pybind11.py: same classes, same free function. Unlike the
# thin `julia` target, std::vector crosses the boundary here (the expanded
# binding builds against the stock libc++, so <jlcxx/stl.hpp> compiles).
# Julia spellings: fields are `name(obj)` / `name!(obj, v)` functions, statics
# are module-level, and a plain Vector works wherever C++ wants a std::vector
# (a zero-copy ArrayRef overload is generated beside the exact one).

include(joinpath(@__DIR__, "bindings", "julia-expanded", "jlgeom.jl"))
using .jlgeom

# A Surface straight from plain Julia Vectors (positions, triangle indices).
s = jlgeom.Surface([0.0, 0, 0, 1, 0, 0, 0, 1, 0], Int32[0, 1, 2])

model = jlgeom.Model()
jlgeom.addSurface(model, s)

# getSurfaces / getPoints / getTriangles return StdVector values (copies, like
# the pybind binding) — 1-based indexable and iterable from Julia.
for surf in jlgeom.getSurfaces(model)
    pts = jlgeom.getPoints(surf)
    tris = jlgeom.getTriangles(surf)
    for (i, p) in enumerate(pts)
        println("point $i    : (", jlgeom.x(p), ", ", jlgeom.y(p), ", ", jlgeom.z(p), ")")
    end
    for (i, t) in enumerate(tris)
        println("triangle $i : (", jlgeom.a(t), ", ", jlgeom.b(t), ", ", jlgeom.c(t), ")")
    end
end

# `transform` is a free (non-member) function bound from common.h. It takes a
# Point and returns a new Point swizzled to (x*2, z*3, y*4).
p = jlgeom.Point(1.0, 2.0, 3.0)
q = jlgeom.transform(p)
println("transform  : (1, 2, 3) -> (", jlgeom.x(q), ", ", jlgeom.y(q), ", ", jlgeom.z(q), ")")
@assert jlgeom.x(q) == 2 && jlgeom.y(q) == 9 && jlgeom.z(q) == 8

# Triangle.a/b/c carry an out-of-line range annotation [0, 1000000] — the
# generated setter (`a!`, Julia's mutating-function convention) validates it.
t = jlgeom.Triangle(Int32(1), Int32(2), Int32(3))
jlgeom.a!(t, Int32(5))
@assert jlgeom.a(t) == 5
ok = try
    jlgeom.a!(t, Int32(-5))
    true
catch e
    println("range      : a!(t, -5) rejected (", e, ")")
    false
end
@assert !ok "range validation should reject -5"

# Enum constants are module-level, prefixed with the enum name (a bare
# `Point` / `Surface` would collide with the classes of the same name).
@assert jlgeom.kind(t) == jlgeom.Kind_Surface
println("enum       : kind(t) == Kind_Surface")

println("done.")
