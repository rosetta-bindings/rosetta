#!/usr/bin/env python3
"""Take an existing, hand-written manifest and point it at this experiment,
changing NOTHING about what it selects.

Arm B is a real curated manifest -- one somebody wrote to get a working binding,
with its own choice of classes, its own `signature` disambiguations, its own
`out_params` and `sequences`. Comparing it against arm A only means something if
the two differ in curation and in nothing else, so the two arms must emit the
same targets from the same rosetta.

This rewrites only infrastructure: where rosetta lives, which targets to emit,
what the module is called. It also drops keys that exist to BUILD the emitted
bindings rather than to generate them (`user_sources`, `wheel`, ...), because
E5, like E1 and E2, stays inside the generation stage.

`classes`, `functions`, `extensions`, `out_params`, `sequences`, `namespace`,
`header_dir` and every other selection or tuning key are copied through
untouched. Those are the curation, and the curation is the independent variable.
"""
import argparse, json, pathlib

# Keys this script owns. Everything else in the source manifest is curation or
# library description and is copied verbatim.
INFRA = {"cpp26_root", "cpp26_cxx", "cpp26_cc", "cpp26_lib",
         "rosetta_include", "user_include", "generator_name", "module_name",
         "targets"}

# Build-stage keys: they describe how to COMPILE the emitted bindings, which
# this experiment never does.
BUILD_ONLY = {"user_sources", "wheel", "wheel_dir", "compile_definitions",
              "link_options", "user_libs", "user_lib_name", "user_lib_dir",
              "user_lib_link", "generated_headers", "module_init", "build_type",
              "qt_dir"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest", required=True, help="the curated manifest to retarget")
    ap.add_argument("--out", required=True)
    ap.add_argument("--targets", required=True)
    ap.add_argument("--module", required=True)
    ap.add_argument("--user-include", action="append", required=True)
    ap.add_argument("--rosetta-include", required=True)
    ap.add_argument("--cpp26-root", default="$ENV{HOME}/devs/c++/clang-p2996/build")
    a = ap.parse_args()

    src = json.loads(pathlib.Path(a.manifest).read_text())

    out, dropped = {}, []
    out["//"] = ("E5 arm B -- CURATED. The selection is verbatim from "
                 f"{a.manifest}; only infrastructure keys were rewritten, by "
                 "retarget.py, so both arms emit the same targets.")
    for k, v in src.items():
        if k.startswith("//") or k in INFRA:
            continue
        if k in BUILD_ONLY:
            dropped.append(k)
            continue
        out[k] = v

    out.update({
        "cpp26_root": a.cpp26_root,
        "cpp26_cxx": a.cpp26_root + "/bin/clang++",
        "cpp26_cc": a.cpp26_root + "/bin/clang",
        "cpp26_lib": a.cpp26_root + "/lib",
        "user_include": a.user_include,
        "rosetta_include": a.rosetta_include,
        "generator_name": "generator",
        "module_name": a.module,
        "targets": [t.strip() for t in a.targets.split(",")],
    })

    pathlib.Path(a.out).write_text(json.dumps(out, indent=4) + "\n")

    def count(key):
        """Entries under a manifest list, flattening group entries."""
        def walk(node):
            if isinstance(node, list):
                return sum(walk(x) for x in node)
            if isinstance(node, dict):
                if "entries" in node:
                    return walk(node["entries"])
                return 1 if "name" in node else 0
            return 0
        return walk(src.get(key, []))

    print(f"curated manifest: {count('classes')} class entries, "
          f"{count('functions')} function entries"
          + (f" (dropped build-only keys: {', '.join(dropped)})" if dropped else ""))


if __name__ == "__main__":
    main()
