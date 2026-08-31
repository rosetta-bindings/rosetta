#!/usr/bin/env python3
"""Merge the two E4 layers into one report.

Layer A (C++)    isolates LATE BINDING: no interpreter is involved.
Layer B (Python) measures late binding AND the language boundary together.

Reading them side by side is the point. The ratio in layer A is the cost of the
meta-object model; the ratio in layer B is what a script author actually feels,
which is smaller because the boundary already dominates.
"""
import argparse, csv, pathlib


def load(path):
    with open(path) as f:
        return [dict(r, ns_per_op=float(r["ns_per_op"])) for r in csv.DictReader(f)]


def pivot(rows, layer):
    out = {}
    for r in rows:
        if r["layer"] != layer:
            continue
        out.setdefault(r["op"], {})[r["arm"]] = r["ns_per_op"]
    return out


def fmt(v):
    if v is None:
        return "—"
    if v >= 10000:
        return f"{v/1000:,.1f} µs"
    if v >= 1000:
        return f"{v:,.0f} ns"
    return f"{v:.1f} ns"


def ratio(a, b):
    return "—" if (a is None or b is None or a == 0) else f"{b/a:.1f}×"


OPS = ["trivial-call", "field-get", "field-set", "3-arg-call",
       "object-return", "vector-1000", "overload-3way"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csvs", nargs="+")
    ap.add_argument("--outdir", default="results")
    a = ap.parse_args()

    rows = []
    for c in a.csvs:
        rows += load(c)

    out = pathlib.Path(a.outdir)
    out.mkdir(parents=True, exist_ok=True)
    with open(out / "results.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["layer", "op", "arm", "ns_per_op"])
        w.writeheader()
        w.writerows(rows)

    cpp = pivot(rows, "cpp")
    py = pivot(rows, "python")

    md = ["# E4 — runtime overhead: results", ""]

    md += ["## Layer A — C++ only (isolates late binding)", "",
           "| operation | direct | dyn (by name) | dyn (cached) | dyn/direct | cached/direct |",
           "|---|---:|---:|---:|---:|---:|"]
    for op in OPS:
        d = cpp.get(op, {})
        md.append(f"| {op} | {fmt(d.get('direct'))} | {fmt(d.get('dyn'))} | "
                  f"{fmt(d.get('dyn-cached'))} | {ratio(d.get('direct'), d.get('dyn'))} | "
                  f"{ratio(d.get('direct'), d.get('dyn-cached'))} |")
    md.append("")

    md += ["## Layer B — from Python (late binding + language boundary)", "",
           "| operation | generated pybind11 | scriptable meta-object | ratio |",
           "|---|---:|---:|---:|"]
    for op in OPS:
        d = py.get(op, {})
        md.append(f"| {op} | {fmt(d.get('static'))} | {fmt(d.get('script'))} | "
                  f"{ratio(d.get('static'), d.get('script'))} |")
    md.append("")

    md += ["## The two layers compared", "",
           "Same operation, two ratios: what late binding costs with no "
           "interpreter (layer A), and what it costs a script author (layer B).",
           "",
           "| operation | A: dyn/direct | B: script/static |",
           "|---|---:|---:|"]
    for op in OPS:
        c, p = cpp.get(op, {}), py.get(op, {})
        md.append(f"| {op} | {ratio(c.get('direct'), c.get('dyn'))} | "
                  f"{ratio(p.get('static'), p.get('script'))} |")
    md.append("")

    (out / "results.md").write_text("\n".join(md))
    print(f"wrote {out}/results.{{csv,md}}  ({len(rows)} rows)")


if __name__ == "__main__":
    main()
