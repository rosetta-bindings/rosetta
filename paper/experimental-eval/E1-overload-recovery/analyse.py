#!/usr/bin/env python3
"""Turn the coverage.json of each E1 configuration into results.

Two properties of coverage.json have to be handled or the comparison is wrong,
and both were found by inspecting real output rather than assumed:

  1. The `dynamic` backend records bound FIELDS as well as bound methods
     (note_bound_field), while the language backends here record only methods.
     Comparing the top-level "bound" counts would therefore credit `dynamic`
     with the fields too. We count methods only, identified by a signature
     containing "(" -- a field's signature is a bare type ("double").

  2. The `typescript` backend records skips but never calls note_bound, so its
     bound count is 0 regardless of what it emitted. That is an instrumentation
     gap, not a measurement. Such targets are detected (skips>0 and bound==0)
     and reported separately instead of being silently averaged in.
"""
import argparse, csv, json, pathlib, sys

REF = "dynamic"          # the late-bound projection: the recovery reference


def is_method(entry):
    return "(" in entry.get("signature", "")


def read_config(cfgdir):
    cfg = pathlib.Path(cfgdir)
    cov = json.loads((cfg / "bindings" / "coverage.json").read_text())
    exp = json.loads((cfg / "expected.json").read_text())

    targets = {}
    for t in cov["targets"]:
        mb = ms = fb = 0
        reasons = {}
        for c in t["classes"]:
            for b in c.get("bound", []):
                if is_method(b):
                    mb += 1
                else:
                    fb += 1
            for s in c.get("skipped", []):
                if is_method(s):
                    ms += 1
                    reasons[s["reason"]] = reasons.get(s["reason"], 0) + 1
        targets[t["target"]] = {
            "methods_bound": mb,
            "methods_skipped": ms,
            "fields_bound": fb,
            "reasons": reasons,
            "instrumentation_gap": (mb == 0 and ms > 0),
        }

    # Members the reflective walk dropped before any backend saw them. Should be
    # zero for this library; reported so a non-zero value cannot hide.
    refl = cov.get("reflection", [])
    if isinstance(refl, dict):
        refl = refl.get("classes", [])
    refl_drops = sum(len(c.get("dropped", [])) for c in refl)

    return {"expected": exp, "targets": targets, "reflection_drops": refl_drops}


def analyse(configs):
    parsed = [(c, read_config(c)) for c in configs]

    # Classify each target's overload policy ONCE, across every configuration.
    # Inferring it per-config would mislabel every target at 1 overload per
    # name, where there are no overload sets to drop and the policies are
    # indistinguishable by observation.
    policy = {}
    for _, r in parsed:
        for name, t in r["targets"].items():
            if name == REF:
                policy[name] = "late-bound"
            elif t["reasons"].get("overload_not_expressible", 0):
                policy[name] = "first-only"
            else:
                policy.setdefault(name, "all-overloads")

    rows = []
    for cfgdir, r in parsed:
        exp, tg = r["expected"], r["targets"]
        if REF not in tg:
            sys.exit(f"{cfgdir}: no '{REF}' target -- add it to the manifest")
        ref_bound = tg[REF]["methods_bound"]

        for name, t in sorted(tg.items()):
            drops = t["reasons"].get("overload_not_expressible", 0)
            other = t["methods_skipped"] - drops
            rows.append({
                "config": pathlib.Path(cfgdir).name,
                "classes": exp["classes"],
                "names": exp["names"],
                "overloads": exp["overloads"],
                "total_methods": exp["total_methods"],
                "target": name,
                "policy": policy[name],
                "methods_bound": t["methods_bound"],
                "overload_drops": drops,
                "other_drops": other,
                "recoverable": max(0, ref_bound - t["methods_bound"]),
                "instrumentation_gap": int(t["instrumentation_gap"]),
                "reflection_drops": r["reflection_drops"],
            })
    return rows


def write_csv(rows, path):
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)


def write_markdown(rows, path):
    by_cfg = {}
    for r in rows:
        by_cfg.setdefault(r["overloads"], []).append(r)

    out = ["# E1 — overload recovery: results", ""]
    out.append("`bound` counts **methods only**. `recoverable` is the number of "
               "methods reachable through the late-bound projection that the "
               "target's own binding surface could not express.")
    out.append("")

    for o in sorted(by_cfg):
        rs = by_cfg[o]
        e = rs[0]
        out.append(f"## {e['classes']} classes x {e['names']} names x {o} overloads "
                   f"({e['total_methods']} methods)")
        out.append("")
        out.append("| target | policy | bound | overload drops | other drops | recoverable |")
        out.append("|---|---|---:|---:|---:|---:|")
        for r in sorted(rs, key=lambda x: (x["policy"], x["target"])):
            gap = " ⚠" if r["instrumentation_gap"] else ""
            out.append(f"| `{r['target']}`{gap} | {r['policy']} | {r['methods_bound']} | "
                       f"{r['overload_drops']} | {r['other_drops']} | {r['recoverable']} |")
        out.append("")
        if any(r["instrumentation_gap"] for r in rs):
            out.append("> ⚠ records skips but never calls `note_bound`, so its bound "
                       "count is an instrumentation gap, not a measurement.")
            out.append("")
    path.write_text("\n".join(out))


def write_tex(rows, path):
    """pgfplots coordinates: bound-vs-overload-multiplicity, one line per policy."""
    firsts = {}
    natives = {}
    late = {}
    for r in rows:
        if r["instrumentation_gap"]:
            continue
        d = {"first-only": firsts, "all-overloads": natives, "late-bound": late}[r["policy"]]
        d.setdefault(r["overloads"], []).append(r["methods_bound"])

    def coords(d):
        return " ".join(f"({o},{sum(v)//len(v)})" for o, v in sorted(d.items()))

    path.write_text(
        "% E1 pgfplots coordinates -- generated by analyse.py\n"
        "% x = overloads per name, y = methods bound\n"
        f"\\def\\ELfirstonly{{{coords(firsts)}}}\n"
        f"\\def\\ELalloverloads{{{coords(natives)}}}\n"
        f"\\def\\ELlatebound{{{coords(late)}}}\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("configs", nargs="+")
    ap.add_argument("--outdir", default="results")
    a = ap.parse_args()

    rows = analyse(a.configs)
    out = pathlib.Path(a.outdir)
    out.mkdir(parents=True, exist_ok=True)

    write_csv(rows, out / "results.csv")
    write_markdown(rows, out / "results.md")
    write_tex(rows, out / "results.tex")

    print(f"wrote {out}/results.{{csv,md,tex}}  ({len(rows)} rows)")
    gaps = sorted({r["target"] for r in rows if r["instrumentation_gap"]})
    if gaps:
        print("instrumentation gap (no note_bound calls): " + ", ".join(gaps))
    refl = sorted({r["reflection_drops"] for r in rows})
    print(f"reflection-stage drops across configs: {refl}")


if __name__ == "__main__":
    main()
