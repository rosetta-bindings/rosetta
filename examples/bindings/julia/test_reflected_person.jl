# Copyright (c) fmaerten@gmail.com
# License: MIT

# Exercise the auto-generated bindings produced by auto_jlcxx.cpp.
#
# Run after building the `reflected_person` shared library. The annotations
# on the C++ side drive everything you see here — read-only fields (no
# setter) and range validation (a thrown C++ exception surfacing in Julia).

module ReflectedPerson
using CxxWrap
# CxxWrap appends the platform extension (.so/.dylib/.dll).
@wrapmodule(() -> joinpath(@__DIR__, "build", "libreflected_person"))
__init__() = @initcxx
end

using .ReflectedPerson
const R = ReflectedPerson

function main()
    p = R.Person()
    println("Person() created")

    println("\n-- valid writes (getter `name`, setter `name!`) --")
    R.name!(p, "Alice")
    R.age!(p, 42)
    println("  name=", R.name(p), ", age=", R.age(p))

    println("\n-- range violation (age = 999) --")
    try
        R.age!(p, 999)
    catch e
        println("  caught: ", e)
    end

    println("\n-- readonly field `id` has no `id!` setter --")
    println("  id setter defined? ", isdefined(R, Symbol("id!")))

    println("\n-- method call --")
    println("  greet(p, \"Hello\") -> ", R.greet(p, "Hello"))

    println("\n-- clear() --")
    R.clear(p)
    println("  after clear: name=", R.name(p), ", age=", R.age(p))
end

main()
