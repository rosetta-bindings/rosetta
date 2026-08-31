#!/usr/bin/env python3
"""Turn each E2 version's generated trees into evolution costs.

The metric that matters is not "how big is the binding" but "how much of it had
to change when the library grew". So every generated file is hashed and the
hashes compared against the previous version.

One normalisation is required and is worth being explicit about: generated
CMakeLists.txt files embed the absolute path of the configuration directory,
which contains the version name (".../work/v2/..."). Left alone, that would make
every file differ between versions for a reason that has nothing to do with the
library. Paths are therefore rewritten to a placeholder before hashing. Without
this the "0 changed files" result would be an artifact -- with it, the result is
about content.

Arms:
  static  the per-class binding. Also the MANUAL arm's artifact: the emitted
          pybind11 module is line-for-line what a developer would hand-write.
  dyn     stage 1 of the scriptable arm -- the metadata tables.
  meta    stage 2 -- the binding of rosetta::script itself.
"""
import argparse, csv, hashlib, json, pathlib, re, sys

ARMS = ["static", "dyn", "meta"]

# Files that are build scaffolding rather than binding logic. Counted, but also
# reported separately, because a reader should be able to see the source-only
# number without taking our word for what is scaffolding.
SCAFFOLD = {"CMakeLists.txt", "pyproject.toml", "README.md", "make_wheel.py",
            "package.json", "binding.gyp", "Project.toml", "configure.log",
            "build.log", "run.log"}


def normalise(text, version_dir):
    """Strip anything that encodes WHERE this configuration lives."""
    text = text.replace(str(version_dir), "<VER>")
    text = text.replace(str(version_dir.resolve()), "<VER>")
    # any remaining absolute path into the work tree
    text = re.sub(r"/[^\s\"']*/work/v\d+", "<VER>", text)
    return text


def scan(bindings_dir, version_dir):
    """{relative path: (sha1, loc)} for every generated file, normalised."""
    out = {}
    if not bindings_dir.is_dir():
        return out
    for p in sorted(bindings_dir.rglob("*")):
        if not p.is_file() or "build" in p.parts:
            continue
        try:
            text = p.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        text = normalise(text, version_dir)
        rel = str(p.relative_to(bindings_dir))
        out[rel] = (hashlib.sha1(text.encode()).hexdigest(),
                    text.count("\n"))
    return out


def manifest_loc(path):
    """Human-authored manifest lines, excluding the "//n" comment keys the
    repo's manifests use for prose."""
    if not path.is_file():
        return 0
    return sum(1 for ln in path.read_text().splitlines()
               if ln.strip() and not re.match(r'\s*"//', ln))


def measure(version_dir):
    v = pathlib.Path(version_dir)
    exp = json.loads((v / "expected.json").read_text())
    arms = {}
    for arm in ARMS:
        files = scan(v / arm / "bindings", v)
        src = {k: t for k, t in files.items()
               if pathlib.Path(k).name not in SCAFFOLD}
        timing = {}
        tf = v / arm / "timing.json"
        if tf.is_file():
            timing = json.loads(tf.read_text())
        arms[arm] = {
            "files": files,
            "src_files": src,
            "manifest_loc": manifest_loc(v / arm / "manifest.json"),
            "loc": sum(loc for _, loc in files.values()),
            "src_loc": sum(loc for _, loc in src.values()),
            "n_files": len(files),
            "n_src_files": len(src),
            "timing": timing,
        }
    return {"name": v.name, "classes": exp["classes"],
            "methods": exp["total_methods"], "fields": exp["total_fields"],
            "arms": arms}


def diff(prev, cur):
    """(changed, added, removed) source files between two versions."""
    if prev is None:
        return (None, None, None)
    p, c = prev["src_files"], cur["src_files"]
    added = [k for k in c if k not in p]
    removed = [k for k in p if k not in c]
    changed = [k for k in c if k in p and c[k][0] != p[k][0]]
    return (len(changed), len(added), len(removed))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("versions", nargs="+")
    ap.add_argument("--outdir", default="results")
    a = ap.parse_args()

    vs = [measure(v) for v in a.versions]
    out = pathlib.Path(a.outdir); out.mkdir(parents=True, exist_ok=True)

    rows = []
    for i, v in enumerate(vs):
        for arm in ARMS:
            cur = v["arms"][arm]
            prv = vs[i - 1]["arms"][arm] if i else None
            ch, ad, rm = diff(prv, cur)
            rows.append({
                "version": v["name"],
                "classes": v["classes"],
                "methods": v["methods"],
                "arm": arm,
                "manifest_loc": cur["manifest_loc"],
                "src_files": cur["n_src_files"],
                "src_loc": cur["src_loc"],
                "all_files": cur["n_files"],
                "all_loc": cur["loc"],
                "src_files_changed": "" if ch is None else ch,
                "src_files_added":   "" if ad is None else ad,
                "src_files_removed": "" if rm is None else rm,
                "driver_compile_s": cur["timing"].get("driver_compile_s", ""),
                "generate_s":       cur["timing"].get("generate_s", ""),
            })

    with open(out / "results.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader(); w.writerows(rows)

    # ---- markdown ----
    LABEL = {"static": "static binding (= what a developer hand-writes)",
             "dyn":    "scriptable stage 1 — metadata tables",
             "meta":   "scriptable stage 2 — the binding itself"}
    md = ["# E2 — API evolution: results", ""]
    md.append("| version | classes | methods |")
    md.append("|---|---:|---:|")
    for v in vs:
        md.append(f"| {v['name']} | {v['classes']} | {v['methods']} |")
    md.append("")

    md.append("## Binding size (generated source, excluding build scaffolding)")
    md.append("")
    md.append("| arm | " + " | ".join(v["name"] for v in vs) + " |")
    md.append("|---" * (len(vs) + 1) + "|")
    for arm in ARMS:
        md.append(f"| {LABEL[arm]} | " +
                  " | ".join(str(v["arms"][arm]["src_loc"]) for v in vs) + " |")
    md.append("")

    md.append("## Human-authored manifest lines")
    md.append("")
    md.append("| arm | " + " | ".join(v["name"] for v in vs) + " |")
    md.append("|---" * (len(vs) + 1) + "|")
    for arm in ARMS:
        md.append(f"| {LABEL[arm]} | " +
                  " | ".join(str(v["arms"][arm]["manifest_loc"]) for v in vs) + " |")
    md.append("")

    md.append("## Source files changed by the version bump")
    md.append("")
    md.append("| arm | " + " | ".join(
        f"{vs[i-1]['name']}→{vs[i]['name']}" for i in range(1, len(vs))) + " |")
    md.append("|---" * len(vs) + "|")
    for arm in ARMS:
        cells = []
        for i in range(1, len(vs)):
            ch, ad, rm = diff(vs[i-1]["arms"][arm], vs[i]["arms"][arm])
            cells.append(f"{ch} changed, {ad} added" if (ch or ad) else "**0**")
        md.append(f"| {LABEL[arm]} | " + " | ".join(cells) + " |")
    md.append("")

    md.append("## Generation wall-clock (s)")
    md.append("")
    md.append("| arm | " + " | ".join(v["name"] for v in vs) + " |")
    md.append("|---" * (len(vs) + 1) + "|")
    for arm in ARMS:
        cells = []
        for v in vs:
            t = v["arms"][arm]["timing"]
            cells.append(f"{t.get('driver_compile_s','?')} + {t.get('generate_s','?')}")
        md.append(f"| {LABEL[arm]} | " + " | ".join(cells) + " |")
    md.append("")
    md.append("_driver compile + generate._")
    md.append("")

    (out / "results.md").write_text("\n".join(md))
    print(f"wrote {out}/results.{{csv,md}}  ({len(rows)} rows)")


if __name__ == "__main__":
    main()
