# serie::Serie from Julia (CxxWrap).
#
#     ./run.sh julia       # or:  julia drive.jl

include(joinpath(@__DIR__, "bindings", "julia", "serie.jl"))
using .serie

# 3 items of itemSize 3 — a serie of 3D vectors
v = serie.Serie([0.0, 0, 0, 1, 0, 0, 0, 1, 0], 3)

println("describe   : ", serie.describe(v))
println("count      : ", serie.count(v), " items of ", serie.itemSize(v),
        " -> ", serie.size(v), " scalars")
println("item(1)    : ", serie.item(v, 1))          # 0-based, as in C++
println("raw        : ", serie.raw(v))

# itemSize 1 — a serie of scalars
s = serie.Serie([1.0, 2.0, 4.0], 1)
println("scalars    : ", serie.scalars(s))
println("scalar(1)  : ", serie.scalar(s, 1))

println("append     : ", serie.scalars(serie.append(s, serie.Serie([9.0], 1))))

try
    serie.scalars(v)
catch e
    println("scalars/3  : ", sprint(showerror, e))
end

# NOT available here: forEach / map / reduce. They take a std::function, and
# jlcxx has no conversion for one, so all three are skipped WITH A REASON —
# python, node and wasm bind them. See README.md, "Getting the templates back".
