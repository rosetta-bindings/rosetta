#!/usr/bin/env python3
"""E4 layer B -- the cost seen from a host language.

Two arms, same C++ library, same operations:

  static  the generated pybind11 module: one bound method per C++ method.
          This is also what a developer would hand-write.
  script  the scriptable meta-object binding: nothing here knows `bench::Widget`
          exists at compile time; the class is reached by string at run time.

Layer A measured late binding with no language boundary. This measures the
combination. The difference between the two layers is the interpreter's share,
which is why both exist.

A third arm, `script-cached`, resolves the ClassInfo once and reuses the
Instance -- the Python-level equivalent of layer A's dyn-cached. There is no
way to cache the *method* lookup through this API today, which is itself worth
reporting.
"""
import argparse, csv, json, pathlib, sys, timeit


def bench(stmt, setup, number):
    """Best-of-5 ns/op. Best-of, not mean: we want the floor, and on a laptop
    the upper tail is scheduler noise, not the system under test."""
    t = timeit.Timer(stmt, setup=setup)
    best = min(t.repeat(repeat=5, number=number))
    return best / number * 1e9


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--static-dir", required=True)
    ap.add_argument("--meta-dir", required=True)
    ap.add_argument("--number", type=int, default=20000)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    sys.path.insert(0, a.static_dir)
    sys.path.insert(0, a.meta_dir)

    common = f"""
import sys
sys.path.insert(0, {a.static_dir!r})
sys.path.insert(0, {a.meta_dir!r})
import benchstatic, benchmeta
w = benchstatic.Widget()
k = benchmeta.find_class('bench::Widget')
inst = k.create([]).value()
vec = [1.0] * 1000
"""

    # (operation, static stmt, script stmt)
    CASES = [
        ("trivial-call",  "w.ping()",                       "inst.call('ping', [])"),
        ("field-get",     "w.value",                        "inst.get('value')"),
        ("field-set",     "w.value = 2.0",                  "inst.set('value', 2.0)"),
        ("3-arg-call",    "w.combine(2.0, 3.0, 4)",         "inst.call('combine', [2.0, 3.0, 4])"),
        ("object-return", "w.clone()",                      "inst.call('clone', [])"),
        ("vector-1000",   "w.scale_all(vec, 2.0)",          "inst.call('scale_all', [vec, 2.0])"),
        ("overload-3way", "w.at(2, 3)",                     "inst.call('at', [2, 3])"),
    ]

    rows = []
    print(f"E4 layer B -- Python, best of 5 x {a.number}\n")
    for op, s_stmt, m_stmt in CASES:
        n = a.number // 20 if op == "vector-1000" else a.number
        for arm, stmt in (("static", s_stmt), ("script", m_stmt)):
            ns = bench(stmt, common, n)
            rows.append({"layer": "python", "op": op, "arm": arm,
                         "ns_per_op": round(ns, 3)})
            print(f"  {op:<22} {arm:<12} {ns:10.1f} ns")

    # The overload the static arm cannot reach at all. pybind11 keeps every
    # overload, so this is not about pybind11 -- it is the reference point for
    # what a first-only target would have lost (see E1).
    print()
    with open(a.out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["layer", "op", "arm", "ns_per_op"])
        w.writeheader()
        w.writerows(rows)
    print(f"wrote {a.out}")


if __name__ == "__main__":
    main()
