# Copyright (c) fmaerten@gmail.com
# License: MIT

"""Exercise the auto-generated bindings produced by auto_pybind.cpp.

Run after building the `reflected_person` extension. The annotations on
the C++ side drive everything you see here — docstrings, range
validation, read-only properties.
"""

import reflected_person as r


def main() -> None:
    p = r.Person()
    print("Person() created:", p)

    print("\n-- docstrings (from [[=rosetta::doc{...}]]) --")
    print(" name doc :", r.Person.name.__doc__)
    print(" age  doc :", r.Person.age.__doc__)
    print(" id   doc :", r.Person.id.__doc__)
    print(" greet doc:", r.Person.greet.__doc__)

    print("\n-- valid writes --")
    p.name = "Alice"
    p.age = 42
    print(f"  name={p.name!r}, age={p.age}")

    print("\n-- range violation (age=999) --")
    try:
        p.age = 999
    except ValueError as e:
        print("  caught ValueError:", e)

    print("\n-- readonly violation (id=...) --")
    try:
        p.id = "p-999"
    except AttributeError as e:
        print("  caught AttributeError:", e)

    print("\n-- method call --")
    print("  greet('Hello') ->", p.greet("Hello"))

    print("\n-- clear() --")
    p.clear()
    print(f"  after clear: name={p.name!r}, age={p.age}")


if __name__ == "__main__":
    main()
