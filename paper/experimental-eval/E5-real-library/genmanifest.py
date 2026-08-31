#!/usr/bin/env python3
"""frame.json -> the EXHAUSTIVE manifest (E5 arm A).

Arm A is "point rosetta at the library and press go": every class, enum and free
function the compiler found in the namespace goes into the manifest, in
declaration order, with no selection and no per-entity tuning. It is the arm
that has no author, so its coverage figure has no author's judgement in it.

Two properties of the manifest format shape what "exhaustive" can even mean
here, and both are results rather than nuisances.

1. An overloaded free function cannot be named without a `signature`.
   `^^pmp::centroid` is ill-formed the moment the namespace declares that name
   twice, so a bare entry would not fail to *bind* -- it would fail to COMPILE,
   taking the whole run with it. Such entries carry a signature taken verbatim
   from the AST and namespace-qualified here. That is mechanical, not judgement.

2. A free-function overload SET cannot be expressed at all. Two entries that
   bind under one exposed name are a hard error from `rosetta_gen`
   ("both bind as ... -- rename one with expose"), so only one overload of a
   name can survive into any target -- including the all-overloads targets like
   pybind11, which lose it here, before a backend ever sees it. The alternative
   would be to invent names (`centroid_2`), which would measure this script's
   naming scheme rather than the library. So the first-declared overload is
   kept, the siblings are recorded as `manifest_overload_collision`, and the
   count is reported. This is a stricter limit than the per-backend one E1
   measures, and it applies to every target.

Qualification is textual, against the set of type names the frame collected from
the namespace. Clang prints a declaration's type from inside the namespace it
lives in, so `pmp::centroid` comes back as `Point (const SurfaceMesh &)` while
the driver, which sits outside, needs `pmp::Point (const pmp::SurfaceMesh &)`.
Substitution is on whole identifiers only and leaves builtins and `std::` names
alone. A signature that cannot be qualified this way would produce a driver that
does not compile -- loudly, not silently -- which is the failure mode to prefer.
"""
import argparse, json, pathlib, re


def qualify(sig, ns, type_names):
    """Namespace-qualify whole-identifier occurrences of the library's own type
    names, leaving builtins, `std::` and already-qualified spellings alone."""
    def sub(m):
        word = m.group(0)
        if word not in type_names:
            return word
        start = m.start()
        # already qualified (preceded by "::") -- leave it
        if start >= 2 and sig[start - 2:start] == "::":
            return word
        return f"{ns}::{word}"
    return re.sub(r"\b[A-Za-z_][A-Za-z0-9_]*\b", sub, sig)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frame", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--targets", required=True,
                    help="comma-separated target languages")
    ap.add_argument("--module", required=True)
    ap.add_argument("--user-include", action="append", required=True)
    ap.add_argument("--rosetta-include", required=True)
    ap.add_argument("--cpp26-root", default="$ENV{HOME}/devs/c++/clang-p2996/build")
    a = ap.parse_args()

    frame = json.loads(pathlib.Path(a.frame).read_text())
    ns = frame["namespace"]
    type_names = set(frame.get("type_names", []))
    ents = frame["entities"]

    # Classes go out in base-before-derived order. rosetta warns (and the
    # module would fail to import) when a class is bound before a base it
    # declares, and declaration order in the headers does not guarantee that --
    # PMP declares Vertex/Halfedge/Edge/Face before their common `Handle` base.
    # Topological, so it is still the compiler's ordering rather than an
    # author's, with ties broken by the original order to stay deterministic.
    kls = [e for e in ents if e["kind"] in ("class", "enum")]
    index = {f"{ns}::{e['name']}": i for i, e in enumerate(kls)}
    ordered, placed = [], set()

    def place(i, stack=()):
        if i in placed or i in stack:
            return
        for b in kls[i].get("bases", []):
            j = index.get(b)
            if j is not None:
                place(j, stack + (i,))
        if i not in placed:
            placed.add(i)
            ordered.append(kls[i])

    for i in range(len(kls)):
        place(i)
    classes = [{"name": e["name"], "header": e["header"]} for e in ordered]

    # One entry per overload; a `signature` only where the name is ambiguous.
    by_name = {}
    for e in ents:
        if e["kind"] == "function":
            by_name.setdefault(e["name"], []).append(e)

    functions, disambiguated, collided = [], 0, []
    emitted = set()
    for e in ents:
        if e["kind"] != "function":
            continue
        if e["name"] in emitted:
            # A sibling of an already-emitted overload: two entries under one
            # exposed name are a rosetta_gen error, so it cannot be carried.
            collided.append({"name": e["name"], "header": e["header"],
                             "signature": e["signature"],
                             "reason": "manifest_overload_collision"})
            continue
        emitted.add(e["name"])
        entry = {"name": e["name"], "header": e["header"]}
        if len(by_name[e["name"]]) > 1:
            entry["signature"] = qualify(e["signature"], ns, type_names)
            disambiguated += 1
        functions.append(entry)

    manifest = {
        "//": f"E5 arm A -- EXHAUSTIVE. Generated by genmanifest.py from the "
              f"compiler's own enumeration of namespace {ns}. Not hand-edited: "
              f"that is the point.",
        "cpp26_root": a.cpp26_root,
        "cpp26_cxx": a.cpp26_root + "/bin/clang++",
        "cpp26_cc": a.cpp26_root + "/bin/clang",
        "cpp26_lib": a.cpp26_root + "/lib",
        "user_include": a.user_include,
        "rosetta_include": a.rosetta_include,
        "generator_name": "generator",
        "module_name": a.module,
        "namespace": ns,
        "header_dir": frame["header_dir"],
        "targets": [t.strip() for t in a.targets.split(",")],
        "classes": classes,
        "functions": functions,
    }
    pathlib.Path(a.out).write_text(json.dumps(manifest, indent=4) + "\n")

    # What the manifest format itself could not carry, beside the manifest, so
    # the analysis never has to re-derive it from the output it is judging.
    pathlib.Path(a.out).with_suffix(".dropped.json").write_text(
        json.dumps({"manifest_overload_collision": collided}, indent=2) + "\n")

    print(f"exhaustive manifest: {len(classes)} classes/enums, "
          f"{len(functions)} function entries "
          f"({disambiguated} carrying a signature; "
          f"{len(collided)} overload siblings the format cannot express)")


if __name__ == "__main__":
    main()
