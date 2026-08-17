#!/usr/bin/env python3
"""Drive bank::Account from Python without any binding generated for it.

The only thing that knows bank::Account exists is the metadata published by
bank::register_all() at import time. This file names it once, as a string.

    ../run.sh
    PYTHONPATH=bindings/python python3 drive.py
"""

import rosetta_meta as meta

for k in meta.classes():
    print(k.qualified())
    for f in k.fields():
        print(f"    {f.name():<8} {f.type().spelling()}")
    for m in k.methods():
        args = ", ".join(p.type().spelling() for p in m.params())
        print(f"    {m.name()}({args}) -> {m.ret().spelling()}"
              f"{' [static]' if m.is_static() else ''}")

print("\n--- a live account ---")
Account = meta.find_class("bank::Account")

a = Account.call_static("open", ["ada", 100.0]).value()      # -> Instance
print("describe :", a.call("describe", []).value())

a.call("deposit", [50])
print("balance  :", a.get("balance").value())

print("withdraw :", a.call("withdraw", [500]).value(), "(more than the balance)")

a.set("frozen", True)
a.call("deposit", [10])
print("frozen   :", a.get("frozen").value(), "-> deposit ignored, balance still",
      a.get("balance").value())

print("\n--- a form, built by query ---")
for f in Account.fields():
    kind = f.type().kind()
    widget = {"boolean": "checkbox", "number": "spinbox", "string": "text"}.get(kind, "text")
    print(f"    {f.name():<8} {widget:<9} = {a.get(f.name()).value()!r}")
