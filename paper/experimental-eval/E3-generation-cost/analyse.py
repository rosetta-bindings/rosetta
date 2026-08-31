#!/usr/bin/env python3
"""Turn E3's per-configuration timings and outputs into scaling laws.

Every sweep varies one knob and holds the rest, so each one supports an ordinary
least-squares fit of cost against the quantity that moved. Two things are worth
stating about how that fit is read:

  * The intercept is a result, not a nuisance. A driver compile has a fixed
    cost -- <experimental/meta>, the rosetta headers, and all nineteen backends,
    which are linked into every driver regardless of which targets the manifest
    names. Splitting it off is what lets the per-member slope be quoted honestly.

  * The base configuration is generated independently by four different sweeps.
    Those repeats are not deduplicated: their spread is the only estimate of
    measurement noise this harness has, and it is reported.

R^2 is reported alongside every fit so a superlinear cost cannot hide inside a
linear one.
"""
import argparse, csv, json, pathlib, re, statistics, sys

STAGES = ["rosetta_gen", "cmake_configure", "driver_compile", "generator_run"]

# Build scaffolding rather than binding logic -- counted, but separable.
SCAFFOLD = {"CMakeLists.txt", "pyproject.toml", "README.md", "make_wheel.py",
            "package.json", "binding.gyp", "Project.toml", "coverage.json"}

SWEEPS = {
    "A": ("classes",   "classes",           "classes in the library"),
    "B": ("methods",   "methods_per_class", "method names per class"),
    "C": ("fields",    "fields_per_class",  "fields per class"),
    "D": ("overloads", "overloads",         "overloads per method name"),
    "E": ("targets",   "n_targets",         "targets emitted"),
}


def parse_size(text):
    """Section sizes from either BSD `size -m` or GNU `size -A`, in bytes."""
    out = {}
    for line in text.splitlines():
        # BSD:  "\tSection (__DATA, __const): 21432"
        m = re.search(r"Section \(__\w+, (__\w+)\):\s+(\d+)", line)
        if m:
            out[m.group(1)] = out.get(m.group(1), 0) + int(m.group(2))
            continue
        # GNU:  ".rodata            21432      0"
        m = re.match(r"^(\.[\w.]+)\s+(\d+)\s+\d+", line)
        if m:
            out[m.group(1)] = out.get(m.group(1), 0) + int(m.group(2))
    return out


def metadata_bytes(sizes):
    """(data, text) -- the descriptor tables and their strings, and the emitted
    per-member call thunks. The dynamic model costs both, and folding them
    together would hide which one scales."""
    data = (sizes.get("__const", 0) + sizes.get("__cstring", 0)
            + sizes.get("__data", 0)
            + sizes.get(".rodata", 0) + sizes.get(".data.rel.ro", 0)
            + sizes.get(".data", 0))
    text = sizes.get("__text", 0) + sizes.get(".text", 0)
    return data, text


def count_loc(bindings):
    """(all lines, source-only lines, files) over a generated tree."""
    if not bindings.is_dir():
        return (0, 0, 0)
    total = src = files = 0
    for p in sorted(bindings.rglob("*")):
        if not p.is_file() or "build" in p.parts:
            continue
        try:
            n = p.read_text(encoding="utf-8").count("\n")
        except (UnicodeDecodeError, OSError):
            continue
        total += n
        files += 1
        if p.name not in SCAFFOLD:
            src += n
    return (total, src, files)


def measure(cfg):
    exp = json.loads((cfg / "expected.json").read_text())
    timing = json.loads((cfg / "timing.json").read_text())
    all_loc, src_loc, n_files = count_loc(cfg / "bindings")

    sizes = {}
    sf = cfg / "metadata.size.txt"
    if sf.is_file():
        sizes = parse_size(sf.read_text())
    meta_data, meta_text = metadata_bytes(sizes)

    row = {
        "config": cfg.name,
        "sweep": cfg.name[0],
        "classes": exp["classes"],
        "methods_per_class": exp["methods_per_class"],
        "fields_per_class": exp["fields_per_class"],
        "overloads": exp["overloads"],
        "n_targets": exp["n_targets"],
        "members": exp["total_members"],
        "methods": exp["total_methods"],
        "fields": exp["total_fields"],
        "emitted_loc": all_loc,
        "emitted_src_loc": src_loc,
        "emitted_files": n_files,
        "metadata_data_b": meta_data,
        "metadata_text_b": meta_text,
    }
    for s in STAGES + ["metadata_compile"]:
        row[f"{s}_s"] = timing.get(s, {}).get("wall_s", "")
    row["driver_peak_rss_mb"] = timing.get("driver_compile", {}).get("peak_rss_mb", "")
    return row


def fit(xs, ys):
    """OLS slope, intercept and R^2. Returns None when x never moves."""
    n = len(xs)
    if n < 2 or len(set(xs)) < 2:
        return None
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    slope = sxy / sxx
    icpt = my - slope * mx
    ss_tot = sum((y - my) ** 2 for y in ys)
    ss_res = sum((y - (slope * x + icpt)) ** 2 for x, y in zip(xs, ys))
    r2 = 1.0 if ss_tot == 0 else 1 - ss_res / ss_tot
    return slope, icpt, r2


def fmt_fit(f, unit, per):
    if f is None:
        return "--"
    slope, icpt, r2 = f
    return f"{icpt:.2f} + {slope:.4g}·{per} {unit} (R²={r2:.4f})"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    ap.add_argument("--outdir", default="results")
    a = ap.parse_args()

    work = pathlib.Path(a.work)
    cfgs = sorted(p for p in work.iterdir()
                  if p.is_dir() and (p / "timing.json").is_file())
    if not cfgs:
        raise SystemExit(f"no measured configurations under {work}")
    rows = [measure(c) for c in cfgs]
    # Sort by sweep, then by whatever that sweep varied, so the tables read in
    # the order the sweep was run rather than in lexicographic config order
    # (which puts "A-c128" before "A-c16").
    rows.sort(key=lambda r: (r["sweep"], r[SWEEPS[r["sweep"]][1]]))

    out = pathlib.Path(a.outdir); out.mkdir(parents=True, exist_ok=True)
    with open(out / "results.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader(); w.writerows(rows)

    by_sweep = {k: [r for r in rows if r["sweep"] == k] for k in SWEEPS}

    md = ["# E3 — generation cost and scalability: results", ""]

    # ---- the headline: where the time goes -----------------------------
    md += ["## Where the time goes", "",
           "Per configuration, seconds. `cmake configure` probes the toolchain "
           "and is not a per-library cost; it is shown so that the total is "
           "accounted for.", "",
           "| config | members | targets | rosetta_gen | cmake configure | "
           "**driver compile** | generator run | peak RSS (MB) |",
           "|---|---:|---:|---:|---:|---:|---:|---:|"]
    for r in rows:
        md.append(
            f"| {r['config']} | {r['members']} | {r['n_targets']} | "
            f"{r['rosetta_gen_s']} | {r['cmake_configure_s']} | "
            f"**{r['driver_compile_s']}** | {r['generator_run_s']} | "
            f"{r['driver_peak_rss_mb']} |")
    md.append("")

    # ---- share of the pipeline taken by the reflective compile ---------
    shares = []
    for r in rows:
        stages = [r[f"{s}_s"] for s in ("rosetta_gen", "driver_compile", "generator_run")]
        if all(isinstance(v, (int, float)) for v in stages) and sum(stages) > 0:
            shares.append(100 * r["driver_compile_s"] / sum(stages))
    if shares:
        md += [f"The P2996 driver compile is **{min(shares):.1f}–{max(shares):.1f}%** "
               f"of pipeline wall-clock (excluding the CMake probe), "
               f"median {statistics.median(shares):.1f}%.", ""]

    # ---- fits, one per sweep -------------------------------------------
    md += ["## Scaling", "",
           "Ordinary least squares over each sweep, against the quantity that "
           "moved. Sweep E is excluded: its x-axis counts targets, which are "
           "not interchangeable units, so a slope over it would not mean "
           "anything. It gets its own section below.", "",
           "| sweep | varied | driver compile (s) | generator run (s) | "
           "emitted source (lines) |",
           "|---|---|---|---|---|"]
    for key, (name, field, label) in SWEEPS.items():
        if key == "E":
            continue
        rs = [r for r in by_sweep[key] if isinstance(r["driver_compile_s"], (int, float))]
        if not rs:
            continue
        xs = [r[field] for r in rs]
        per = {"A": "class", "B": "method/class", "C": "field/class",
               "D": "overload"}[key]
        md.append(
            f"| {key} | {label} | "
            f"{fmt_fit(fit(xs, [r['driver_compile_s'] for r in rs]), 's', per)} | "
            f"{fmt_fit(fit(xs, [r['generator_run_s'] for r in rs]), 's', per)} | "
            f"{fmt_fit(fit(xs, [r['emitted_src_loc'] for r in rs]), 'lines', per)} |")
    md.append("")

    # Per-member slope, the number that generalises across sweeps A-D.
    lib_rows = [r for r in rows if r["sweep"] in "ABCD"
                and isinstance(r["driver_compile_s"], (int, float))]
    f = fit([r["members"] for r in lib_rows],
            [r["driver_compile_s"] for r in lib_rows])
    if f:
        slope, icpt, r2 = f
        md += [f"Pooled over sweeps A–D — every configuration that changes the "
               f"library, whatever knob moved it — the driver compile is "
               f"**{icpt:.2f} s + {slope*1000:.2f} ms per reflected member** "
               f"(R²={r2:.4f}).", ""]

    # ---- what a member costs, by kind -----------------------------------
    # Each of B, C and D grows the library by adding exactly one kind of
    # member, so fitting that sweep against the MEMBER count -- rather than
    # against its own knob -- reads off what that kind costs. A grows classes,
    # so its slope is an average member plus the per-class overhead.
    md += ["## What a member costs, by kind", "",
           "Sweeps B, C and D each add exactly one kind of member, so fitting "
           "each against the member count reads off the cost of that kind. "
           "Sweep A adds whole classes, so its slope carries the per-class "
           "overhead as well.", "",
           "| sweep | member added | ms per member | R² |",
           "|---|---|---:|---:|"]
    KIND = {"A": "whole class (3 fields + 4 methods)",
            "B": "method, distinct name",
            "C": "field",
            "D": "overload of an existing name"}
    for key in "ABCD":
        rs = [r for r in by_sweep[key]
              if isinstance(r["driver_compile_s"], (int, float))]
        f = fit([r["members"] for r in rs], [r["driver_compile_s"] for r in rs])
        if f:
            slope, _, r2 = f
            md.append(f"| {key} | {KIND[key]} | {slope*1000:.2f} | {r2:.4f} |")
    md.append("")

    # ---- the target axis ------------------------------------------------
    if by_sweep["E"]:
        e = sorted(by_sweep["E"], key=lambda r: r["n_targets"])
        md += ["## Emitting more targets", "",
               "The library is identical across this sweep; only the number of "
               "targets the manifest asks for changes.", "",
               "| targets | driver compile (s) | generator run (s) | "
               "emitted source (lines) | files |",
               "|---:|---:|---:|---:|---:|"]
        for r in e:
            md.append(f"| {r['n_targets']} | {r['driver_compile_s']} | "
                      f"{r['generator_run_s']} | {r['emitted_src_loc']} | "
                      f"{r['emitted_files']} |")
        md.append("")
        dc = [r["driver_compile_s"] for r in e if isinstance(r["driver_compile_s"], (int, float))]
        if len(dc) > 1:
            md += [f"Driver compile across 1→{e[-1]['n_targets']} targets: "
                   f"{min(dc):.2f}–{max(dc):.2f} s "
                   f"(spread {100*(max(dc)-min(dc))/statistics.mean(dc):.1f}% of the mean). "
                   f"The reflective walk is per-library, not per-target: the "
                   f"backends consume the IR at run time, and all nineteen are "
                   f"linked into every driver whether the manifest names them "
                   f"or not.", ""]

    # ---- metadata footprint --------------------------------------------
    meta = [r for r in rows if r["metadata_data_b"]]
    if meta:
        md += ["## Compiled footprint of the metadata tables", "",
               "`auto_dynamic.cpp` built with the system compiler at `-O2`. "
               "`data` is the descriptor tables and their strings; `text` is the "
               "emitted per-member call thunks.", "",
               "| config | members | data (B) | text (B) | total (B) | B/member |",
               "|---|---:|---:|---:|---:|---:|"]
        for r in sorted(meta, key=lambda r: r["members"]):
            tot = r["metadata_data_b"] + r["metadata_text_b"]
            md.append(f"| {r['config']} | {r['members']} | {r['metadata_data_b']} | "
                      f"{r['metadata_text_b']} | {tot} | {tot/r['members']:.0f} |")
        md.append("")
        f = fit([r["members"] for r in meta],
                [r["metadata_data_b"] + r["metadata_text_b"] for r in meta])
        if f:
            slope, icpt, r2 = f
            md += [f"Fit: **{icpt:.0f} B + {slope:.0f} B per member** "
                   f"(R²={r2:.4f}).", ""]
        # E2 deferred this cost here explicitly ("compiling the tables is not
        # free, and that cost belongs to E3"), so it is reported rather than
        # left implicit in the CSV.
        mc = [r for r in meta if isinstance(r["metadata_compile_s"], (int, float))]
        f = fit([r["members"] for r in mc], [r["metadata_compile_s"] for r in mc])
        if f:
            slope, icpt, r2 = f
            md += [f"Compiling those tables with the system compiler costs "
                   f"**{icpt:.2f} s + {slope*1000:.2f} ms per member** "
                   f"(R²={r2:.4f}) — an order of magnitude less per member than "
                   f"the reflective walk that produced them.", ""]

    # ---- repeatability ---------------------------------------------------
    md += ["## Repeatability", ""]
    base = [r for r in rows
            if (r["classes"], r["methods_per_class"], r["fields_per_class"],
                r["overloads"], r["n_targets"]) ==
               (16, 4, 3, 1, 3)
            and isinstance(r["driver_compile_s"], (int, float))]
    if len(base) > 1:
        ts = [r["driver_compile_s"] for r in base]
        md += [f"The base configuration (16 classes × 4 methods × 3 fields, "
               f"3 targets) is generated independently by {len(base)} sweeps. "
               f"Driver compile: {', '.join(f'{t:.2f}' for t in sorted(ts))} s "
               f"— mean {statistics.mean(ts):.2f} s, spread "
               f"{100*(max(ts)-min(ts))/statistics.mean(ts):.1f}% of the mean.", ""]
    ident = [r["driver_compile_s"] for r in by_sweep["E"]
             if isinstance(r["driver_compile_s"], (int, float))]
    if len(ident) > 1:
        md += [f"Sweep E gives a second, independent estimate: {len(ident)} "
               f"configurations over the *same* library, differing only in how "
               f"many targets they emit — a quantity the driver compile does "
               f"not depend on. Spread "
               f"{100*(max(ident)-min(ident))/statistics.mean(ident):.1f}% of "
               f"the mean.", ""]
    md += ["Differences below those spreads are noise, not measurements.", ""]

    (out / "results.md").write_text("\n".join(md))
    print(f"wrote {out}/results.{{csv,md}}  ({len(rows)} configurations)")


if __name__ == "__main__":
    main()
