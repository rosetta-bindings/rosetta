#!/usr/bin/env python3
"""Drive rosetta's metadata from Python.

Nothing here names scene::Mesh, scene::Vec3 or scene::Shading as a type. They
arrive at import time from the tables the `dynamic` backend generated in
../dynamic/bindings/dynamic, published by the manifest's module_init.

Values cross natively — 0.5 goes in, 0.5 comes out — because value_caster.h
specializes pybind11's type_caster for rosetta::script::Value.

    ../../bin/rosetta_gen --build manifest.json
    PYTHONPATH=bindings/python python3 drive.py
"""

import rosetta_meta as meta


def dump_registry():
    for k in meta.classes():
        print(f"{k.qualified()}  {k.doc()}")
        for f in k.fields():
            bits = []
            if f.readonly():
                bits.append("readonly")
            if f.has_range():
                bits.append(f"range [{f.range_min()}, {f.range_max()}]")
            if f.choices():
                bits.append("choices " + str(f.choices()))
            if f.type().kind() == "enum":
                bits.append("enum " + str(f.type().enumerator_names()))
            if not f.readable():
                bits.append("NOT READABLE")
            print(f"    {f.name():<10} {f.type().spelling():<20} {'; '.join(bits)}")


def exercise_objects():
    M = meta.find_class("scene::Mesh")
    m = meta.create("scene::Mesh", []).value()          # -> Instance

    print("describe    :", m.call("describe", []).value())
    print("name        :", repr(m.get("name").value()))
    print("visible     :", m.get("visible").value())

    # range annotations are enforced in the core, not per backend
    m.set("opacity", 0.5)
    print("opacity     :", m.get("opacity").value())
    print("set 99      :", m.set("opacity", 99).error())

    # a Python list crosses as std::vector<double> and comes back a list
    m.set("weights", [0.25, 0.5, 1])
    print("weights     :", m.get("weights").value())

    # a sub-object comes back as an Instance that pins its parent, and can be
    # handed straight back in as an argument
    origin = m.get("origin").value()
    print("origin.x    :", origin.get("x").value())

    # the whole overload set survives, and a failed call names every candidate
    cube = M.call_static("cube", []).value()
    print("at(0, 1)    :", cube.call("at", [0, 1]).value())
    print("no match    :", M.why_no_match("at", ["nope"]))

    # described, not deleted
    print("uncallable  :", [(x.name(), x.skip_reason())
                            for x in M.methods() if not x.callable()])

    # the Add-menu scan from examples/dynamic/qt/mainwindow.h, in one line
    print("factories   :", [x.name() for x in M.methods()
                            if x.is_static() and x.ret().kind() == "object"])


def build_panel_spec(inst):
    """What a Qt/Tk/web property sheet would consume — the point of the exercise.

    This is qt/propertypanel.h's dispatch, in the language the toolkit binding
    lives in. Returns widget descriptors rather than widgets so the example
    stays dependency-free.
    """
    rows = []
    for f in inst.cls().fields():
        if not f.readable():
            continue
        t = f.type()
        if f.choices():
            w = ("combo", f.choices())
        elif t.kind() == "enum":
            w = ("combo", t.enumerator_names())
        elif f.has_range():
            w = ("slider", (f.range_min(), f.range_max()))
        elif t.kind() == "boolean":
            w = ("checkbox", None)
        elif t.kind() == "vector":
            w = ("list", t.element().spelling())
        else:
            w = ("text", None)
        rows.append({"label": f.name(), "tooltip": f.doc(), "widget": w,
                     "value": inst.get(f.name()).value(),
                     "enabled": not f.readonly()})
    return rows


if __name__ == "__main__":
    dump_registry()
    print("\n--- live objects ---")
    exercise_objects()
    print("\n--- generated property sheet ---")
    for row in build_panel_spec(meta.create("scene::Mesh", []).value()):
        print("   ", row)
