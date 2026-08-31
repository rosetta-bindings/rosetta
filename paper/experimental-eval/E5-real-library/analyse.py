#!/usr/bin/env python3
"""Turn E5's two coverage reports into a stratified account of a real API.

Three conventions, all of them there to stop a single headline percentage from
doing work it has not earned:

  * The denominator is stated, never implied. What the library declares (the
    frame), what could reach a manifest at all, and what each target bound are
    three different numbers, and the losses between them have different causes.
    They are reported as a chain, not collapsed.

  * Strata are mechanical. Classes are grouped by the directory of the header
    that declares them -- `algorithms/`, `io/`, core -- so no judgement is made
    here about which parts of the API "matter". A reader who thinks the property
    machinery should not count can subtract that row; a reader who thinks it
    should can leave it. That choice is theirs, and it is only theirs if the
    breakdown is published rather than a single ratio.

  * Templates are counted, not filtered. A class or function template cannot be
    named in a manifest, so it can never bind. Excluding templates from the
    frame would raise every percentage below without changing a single fact.
"""
import argparse, collections, json, pathlib

ARMS = [("exhaustive", "arm A — exhaustive (no author)"),
        ("curated", "arm B — curated (hand-written manifest)")]

# Reported in this order: native-overload targets, then first-only, then the
# declaration file, then the late-bound projection.
TARGET_ORDER = ["python", "nanobind", "julia", "lua", "node", "wasm",
                "csharp", "java", "typescript", "dynamic"]


def load(work, arm):
    p = pathlib.Path(work) / arm / "bindings" / "coverage.json"
    return json.loads(p.read_text()) if p.is_file() else None


def target_rows(cov):
    """Per target: class-member and free-function bound/skipped, plus reasons."""
    rows = {}
    for t in cov["targets"]:
        fns = t.get("functions", {"bound": [], "skipped": []})
        mb = sum(len(c.get("bound", [])) for c in t["classes"])
        ms = sum(len(c.get("skipped", [])) for c in t["classes"])
        reasons = collections.Counter()
        for c in t["classes"]:
            for s in c.get("skipped", []):
                reasons[s["reason"]] += 1
        for s in fns["skipped"]:
            reasons[s["reason"]] += 1
        rows[t["target"]] = {
            "members_bound": mb, "members_skipped": ms,
            "fn_bound": len(fns["bound"]), "fn_skipped": len(fns["skipped"]),
            "reasons": reasons,
        }
    return rows


def md_table(md, header, aligns, rows):
    md.append("| " + " | ".join(header) + " |")
    md.append("|" + "|".join(aligns) + "|")
    md += ["| " + " | ".join(str(c) for c in r) + " |" for r in rows]
    md.append("")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    ap.add_argument("--outdir", default="results")
    a = ap.parse_args()

    work = pathlib.Path(a.work)
    frame = json.loads((work / "frame.json").read_text())
    ents = frame["entities"]
    covs = {arm: load(work, arm) for arm, _ in ARMS}
    covs = {k: v for k, v in covs.items() if v}
    if not covs:
        raise SystemExit(f"no coverage reports under {work}")

    out = pathlib.Path(a.outdir)
    out.mkdir(parents=True, exist_ok=True)
    md = [f"# E5 — coverage on a real library ({frame['library']}): results", ""]

    # ---------------- the frame -------------------------------------------
    kinds = collections.Counter(e["kind"] for e in ents)
    nameable = sum(1 for e in ents if e["bindable"])
    md += ["## What the library declares", "",
           f"The frame is the compiler's own enumeration of namespace "
           f"`{frame['namespace']}` over {frame['headers_scanned']} headers"
           + (f" (excluding `{'`, `'.join(frame['excluded_dirs'])}/`)"
              if frame.get("excluded_dirs") else "") + ".", ""]
    md_table(md, ["kind", "count", "can a manifest name it?"],
             ["---", "---:", "---"],
             [[k, kinds[k], "yes" if k in ("class", "enum", "function") else
               "**no** — a template is not an entity until instantiated"]
              for k in ("class", "enum", "function",
                        "class_template", "function_template") if kinds[k]])
    md += [f"So of {len(ents)} declared entities, **{nameable} are nameable at "
           f"all** and {len(ents) - nameable} are templates — "
           f"{100 * (len(ents) - nameable) / len(ents):.0f}% of the declared "
           f"surface is out of reach before any backend is consulted.", ""]

    # Where the templates actually live: the shape of that answer is the point.
    tmpl = collections.Counter(e["header"] for e in ents if not e["bindable"])
    if tmpl:
        md += ["Templates are not spread evenly, which matters more than the "
               "total:", ""]
        md_table(md, ["header", "templates"], ["---", "---:"],
                 sorted(tmpl.items(), key=lambda kv: -kv[1]))

    # ---------------- overload multiplicity -------------------------------
    fn = [e for e in ents if e["kind"] == "function"]
    per_name = collections.Counter(e["name"] for e in fn)
    dist = collections.Counter(per_name.values())
    md += ["## Overload multiplicity, measured rather than swept", "",
           "E1 sweeps overloads-per-name as a free parameter because a "
           "synthetic library has no opinion about it. This is what the "
           "parameter's value actually is, for free functions, in a real API:",
           ""]
    md_table(md, ["overloads per name", "names", "declarations"],
             ["---:", "---:", "---:"],
             [[k, v, k * v] for k, v in sorted(dist.items())])
    lost = sum(k - 1 for k in per_name.values())
    md += [f"{len(per_name)} names carry {len(fn)} declarations, so a "
           f"first-only policy costs **{lost} of {len(fn)} "
           f"({100 * lost / len(fn):.0f}%)** of the free-function surface — "
           f"against the two thirds E1 measures at its swept worst case of "
           f"five. The mechanism E1 isolates is real; at free-function scope "
           f"this library barely exercises it.", ""]

    # ---------------- per-arm coverage ------------------------------------
    for arm, label in ARMS:
        cov = covs.get(arm)
        if not cov:
            continue
        rows = target_rows(cov)
        md += [f"## {label}", ""]

        refl = collections.Counter(d["reason"] for k in cov["reflection"]
                                   for d in k.get("dropped", []))
        if refl:
            md += ["Dropped by the reflective walk, before any backend saw them "
                   "— backend-independent: "
                   + ", ".join(f"`{k}` {v}" for k, v in sorted(refl.items()))
                   + ".", ""]

        md_table(md,
                 ["target", "members bound", "members skipped",
                  "functions bound", "functions skipped", "top reasons"],
                 ["---", "---:", "---:", "---:", "---:", "---"],
                 [[f"`{t}`", r["members_bound"], r["members_skipped"],
                   r["fn_bound"], r["fn_skipped"],
                   ", ".join(f"`{k}` {v}" for k, v in r["reasons"].most_common(3))]
                  for t in TARGET_ORDER if (r := rows.get(t))])

    # ---------------- the two arms side by side ---------------------------
    if len(covs) > 1:
        ra = target_rows(covs["exhaustive"])
        rb = target_rows(covs["curated"])
        md += ["## What curation changed", "",
               "Same library, same targets, same rosetta. The only difference "
               "is who chose the entries.", ""]
        md_table(md,
                 ["target", "exhaustive", "curated", "difference"],
                 ["---", "---:", "---:", "---:"],
                 [[f"`{t}`",
                   ra[t]["members_bound"] + ra[t]["fn_bound"],
                   rb[t]["members_bound"] + rb[t]["fn_bound"],
                   f"{ra[t]['members_bound'] + ra[t]['fn_bound'] - rb[t]['members_bound'] - rb[t]['fn_bound']:+d}"]
                  for t in TARGET_ORDER if t in ra and t in rb])

    # ---------------- what the manifest format itself could not carry -----
    dropped = work / "exhaustive" / "manifest.dropped.json"
    if dropped.is_file():
        d = json.loads(dropped.read_text()).get("manifest_overload_collision", [])
        if d:
            md += ["## A limit above the backends", "",
                   f"{len(d)} free-function declarations never reached a "
                   f"backend at all: two manifest entries that bind under one "
                   f"exposed name are a `rosetta_gen` error, so only one "
                   f"overload of a name can be requested. This binds *before* "
                   f"the per-target overload policy E1 measures, and it applies "
                   f"to every target — including the ones pybind11 and jlcxx "
                   f"would happily have given a full overload set.", ""]
            md_table(md, ["name", "header", "signature"], ["---", "---", "---"],
                     [[f"`{x['name']}`", x["header"], f"`{x['signature']}`"]
                      for x in d])

    (out / "results.md").write_text("\n".join(md))

    # A machine-readable summary beside the prose, for replotting.
    summary = {"frame": {"entities": len(ents), "nameable": nameable,
                         "kinds": dict(kinds)},
               "overloads": {"names": len(per_name), "declarations": len(fn),
                             "distribution": dict(sorted(dist.items())),
                             "first_only_loss": lost},
               "arms": {arm: {t: {k: (dict(v) if isinstance(v, collections.Counter) else v)
                                  for k, v in r.items()}
                              for t, r in target_rows(cov).items()}
                        for arm, cov in covs.items()}}
    (out / "results.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(f"wrote {out}/results.{{md,json}}  ({len(covs)} arm(s))")


if __name__ == "__main__":
    main()
