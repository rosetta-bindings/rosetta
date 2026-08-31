#!/usr/bin/env python3
"""Compare the two arms of E6 and decide whether the case study's claim holds.

The claim in one line: growing the library moves the metadata and nothing else.
That is two assertions, and the analysis fails loudly on either.

  1. The stage-1 metadata tables DIFFER, and the new class is present in the
     "after" arm and absent from "before". Without this the whole comparison
     could pass by generating nothing twice, which is the failure mode a
     byte-identity test invites.

  2. Every stage-2 artifact -- the binding of `rosetta::script` that the hosts
     actually import -- is byte-identical across the arms. Stage 2 is a binding
     of the reflection API rather than of the library, so a library that grew by
     a class must not move it at all.

One normalisation is necessary and is stated rather than hidden: generated
CMakeLists.txt files embed the absolute path of their configuration directory,
which contains the arm name. Left alone, every file would differ for a reason
that has nothing to do with the library. Paths are rewritten to a placeholder
before hashing -- the same normalisation E2 documents, for the same reason.
"""
import argparse, hashlib, json, pathlib, re, sys

# Not binding logic: build scaffolding and logs. Counted separately so a reader
# can see the source-only answer without taking our word for what is scaffolding.
SCAFFOLD = {"CMakeLists.txt", "pyproject.toml", "README.md", "make_wheel.py",
            "package.json", "binding.gyp", "Project.toml"}
LOGS = {"gen.log", "configure.log", "build.log", "generate.log"}


def normalise(text, work):
    text = text.replace(str(work), "<WORK>")
    text = re.sub(r"/[^\s\"']*/work/(before|after)", "<ARM>", text)
    return re.sub(r"\b(before|after)\b", "<ARM>", text)


def scan(root, work):
    """{relative path: (sha1, lines)} over every generated file."""
    out = {}
    if not root.is_dir():
        return out
    for p in sorted(root.rglob("*")):
        if not p.is_file() or "build" in p.parts or p.name in LOGS:
            continue
        try:
            t = normalise(p.read_text(encoding="utf-8"), work)
        except (UnicodeDecodeError, OSError):
            continue
        out[str(p.relative_to(root))] = (hashlib.sha1(t.encode()).hexdigest(),
                                         t.count("\n"))
    return out


def diff(a, b):
    changed = sorted(k for k in b if k in a and a[k][0] != b[k][0])
    added = sorted(k for k in b if k not in a)
    removed = sorted(k for k in a if k not in b)
    return changed, added, removed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    ap.add_argument("--new-class", required=True)
    ap.add_argument("--outdir", default="results")
    a = ap.parse_args()

    work = pathlib.Path(a.work).resolve()
    arms = {}
    for arm in ("before", "after"):
        arms[arm] = {
            "meta": scan(work / arm / "bindings", work),
            "script": scan(work / arm / "scriptable" / "bindings", work),
        }
        if not arms[arm]["meta"]:
            raise SystemExit(f"{arm}: stage 1 produced nothing — see {work/arm}/build.log")
        if not arms[arm]["script"]:
            raise SystemExit(f"{arm}: stage 2 produced nothing — "
                             f"see {work/arm}/scriptable/build.log")

    m_changed, m_added, m_removed = diff(arms["before"]["meta"], arms["after"]["meta"])
    s_changed, s_added, s_removed = diff(arms["before"]["script"], arms["after"]["script"])

    # Guard against a vacuous pass: the new class must actually be in the
    # "after" tables and absent from "before".
    short = a.new_class.split("::")[-1]
    def mentions(arm):
        p = work / arm / "bindings" / "dynamic" / "auto_dynamic.cpp"
        return short in p.read_text() if p.is_file() else False
    present_after, present_before = mentions("after"), mentions("before")

    ok_grew = bool(m_changed or m_added) and present_after and not present_before
    ok_stable = not (s_changed or s_added or s_removed)

    out = pathlib.Path(a.outdir); out.mkdir(parents=True, exist_ok=True)
    md = ["# E6 — the case study's claim, checked", "",
          f"The library grows one class (`{a.new_class}`). `scene.h` is byte-identical "
          f"in both arms — only the manifest's class list differs — so what is "
          f"measured is a binding surface growing, not a source edit.", ""]

    md += ["## 1. The metadata moved (it must)", "",
           f"- stage-1 files changed: **{len(m_changed)}**, added: {len(m_added)}, "
           f"removed: {len(m_removed)}",
           f"- `{short}` present in the *after* tables: **{present_after}**",
           f"- `{short}` present in the *before* tables: **{present_before}** "
           f"(must be false, or the arms are not different)",
           "", f"Verdict: **{'PASS' if ok_grew else 'FAIL'}**", ""]
    if m_changed:
        md += ["Changed:", ""] + [f"- `{k}`" for k in m_changed] + [""]

    md += ["## 2. The binding did not (the claim)", "",
           "Stage 2 is a binding of `rosetta::script`. Every host imports *this*, "
           "not the library.", "",
           f"- stage-2 files compared: **{len(arms['after']['script'])}**",
           f"- changed: **{len(s_changed)}**, added: {len(s_added)}, "
           f"removed: {len(s_removed)}",
           "", f"Verdict: **{'PASS' if ok_stable else 'FAIL'}**", ""]
    if s_changed or s_added or s_removed:
        md += ["Unexpected differences:", ""]
        md += [f"- changed `{k}`" for k in s_changed]
        md += [f"- added `{k}`" for k in s_added]
        md += [f"- removed `{k}`" for k in s_removed]
        md += [""]

    total = sum(n for _, n in arms["after"]["script"].values())
    src = sum(n for k, (_, n) in arms["after"]["script"].items()
              if pathlib.Path(k).name not in SCAFFOLD)
    md += ["## Scale", "",
           f"The unchanged stage-2 binding is {total} generated lines "
           f"({src} excluding build scaffolding) across "
           f"{len(arms['after']['script'])} files. That is the artifact the "
           f"library's growth did not touch.", ""]

    (out / "results.md").write_text("\n".join(md))
    (out / "results.json").write_text(json.dumps({
        "new_class": a.new_class,
        "metadata": {"changed": m_changed, "added": m_added, "removed": m_removed,
                     "present_after": present_after, "present_before": present_before},
        "scriptable": {"changed": s_changed, "added": s_added, "removed": s_removed,
                       "files": len(arms["after"]["script"]), "lines": total},
        "pass": bool(ok_grew and ok_stable),
    }, indent=2) + "\n")

    print(f"wrote {out}/results.{{md,json}}")
    if not (ok_grew and ok_stable):
        print("E6: CLAIM FAILED", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
