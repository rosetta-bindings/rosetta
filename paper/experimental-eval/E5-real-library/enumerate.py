#!/usr/bin/env python3
"""Enumerate a real library's public API -- with the compiler, not with grep.

E5's whole difficulty is the denominator. "What fraction of a realistic C++ API
binds?" is only a measurement if something other than the experimenter decides
what the API *is*. Geogram hands you that decision in its `GEOGRAM_API` export
macro; PMP has no such macro, so the frame is defined here as:

    every class, class template, enum and free function declared in namespace
    `pmp` by a header under src/pmp/, excluding viewers/ (OpenGL)

and it is read out of the compiler's own AST rather than pattern-matched out of
the source. Clang parses the headers, `-ast-dump-filter=pmp::` restricts the
dump to that namespace, and what comes back is the compiler's answer to "what
did this library declare" -- the same argument the paper makes for reflection
over a third-party parser, applied to the experiment's own instrument.

Two properties of this frame matter for the analysis downstream:

  * Templates are IN the frame, not filtered out of it. A class or function
    template cannot be named in a manifest -- rosetta binds entities, and a
    template is not one until it is instantiated -- so they cannot be bound.
    Dropping them from the frame would quietly inflate every percentage that
    follows; they are recorded and reported as their own stratum instead.

  * Every entity carries its declaring header, which is what the analysis
    strata are built from. Grouping by header is mechanical: no judgement is
    made about which classes are "the interesting ones", which is exactly the
    judgement a curated manifest makes and that arm A exists to avoid.

Writes frame.json. Needs only a C++20-capable system compiler -- the p2996 fork
is for the driver, not for this.
"""
import argparse, json, os, pathlib, subprocess, sys

# Kinds we take from the dump, and what each means for bindability.
BINDABLE = {"CXXRecordDecl": "class", "EnumDecl": "enum", "FunctionDecl": "function"}
TEMPLATE = {"ClassTemplateDecl": "class_template",
            "FunctionTemplateDecl": "function_template"}

# Every kind that introduces a TYPE NAME into the namespace. Not part of the
# frame's population -- an alias is not an entity anyone binds -- but needed to
# qualify signatures: clang prints a decl's type from inside the namespace, so
# `pmp::centroid`'s type comes back as "Point (const SurfaceMesh &)" and the
# driver, which sits outside, needs "pmp::Point (const pmp::SurfaceMesh &)".
TYPE_NAME_KINDS = {"CXXRecordDecl", "EnumDecl", "ClassTemplateDecl",
                   "TypeAliasDecl", "TypedefDecl", "TypeAliasTemplateDecl"}


def split_json_stream(text):
    """clang's --ast-dump-filter emits one JSON document per matched decl,
    concatenated. Parse them in sequence."""
    dec, i, out = json.JSONDecoder(), 0, []
    while i < len(text):
        while i < len(text) and text[i] in " \n\t\r":
            i += 1
        if i >= len(text):
            break
        obj, i = dec.raw_decode(text, i)
        out.append(obj)
    return out


def dump_ast(headers, includes, cxx, std, namespace):
    """One TU including every public header, dumped filtered to `namespace`."""
    tu = "".join(f"#include <{h}>\n" for h in headers)
    cmd = [cxx, f"-std={std}", "-x", "c++", "-fsyntax-only",
           "-Xclang", "-ast-dump=json",
           "-Xclang", f"-ast-dump-filter={namespace}::", "-"]
    for inc in includes:
        cmd += ["-I", inc]
    p = subprocess.run(cmd, input=tu, capture_output=True, text=True)
    if not p.stdout.strip():
        sys.stderr.write(p.stderr[:4000])
        raise SystemExit("clang produced no AST -- see the errors above")
    return p.stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", required=True,
                    help="the library's include root (the dir holding the namespace dir)")
    ap.add_argument("--header-dir", required=True,
                    help="subdirectory of --src holding the headers, e.g. 'pmp'")
    ap.add_argument("--namespace", required=True)
    ap.add_argument("--include", action="append", default=[],
                    help="extra -I (repeatable), e.g. a vendored Eigen")
    ap.add_argument("--exclude-dir", action="append", default=[],
                    help="header subdirectory to leave out of the frame (repeatable)")
    ap.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    ap.add_argument("--std", default="c++20")
    ap.add_argument("--out", default="frame.json")
    a = ap.parse_args()

    src = pathlib.Path(a.src).resolve()
    root = src / a.header_dir
    if not root.is_dir():
        raise SystemExit(f"no such header directory: {root}")

    headers = []
    for p in sorted(root.rglob("*.h")):
        rel = p.relative_to(root)
        if any(part in a.exclude_dir for part in rel.parts[:-1]):
            continue
        headers.append(f"{a.header_dir}/{rel.as_posix()}")

    dump = dump_ast(headers, [str(src)] + a.include, a.cxx, a.std, a.namespace)
    objs = split_json_stream(dump)

    # A decl's "loc" carries the file only when it CHANGES, so carry it forward.
    prefix = str(root) + "/"
    current = None
    entities, seen, type_names = [], set(), set()
    for o in objs:
        f = o.get("loc", {}).get("file")
        if f:
            current = f
        kind = o.get("kind")
        name = o.get("name")
        in_lib = bool(name and current and current.startswith(prefix))
        if in_lib and kind in TYPE_NAME_KINDS:
            type_names.add(name)
        if kind not in BINDABLE and kind not in TEMPLATE:
            continue
        if not in_lib:
            continue  # declared somewhere else (a std/Eigen decl reopened in pmp)
        # A class appearing only as a forward declaration is not the definition;
        # the definition shows up separately and is the one we want.
        if kind == "CXXRecordDecl" and not o.get("completeDefinition"):
            continue
        header = current[len(prefix):]
        # Keep the signature: two FunctionDecls sharing a name are an OVERLOAD
        # SET, and collapsing them would destroy the one distribution E1 says
        # only a real API can supply. Deduplication is on the signature, so a
        # declaration and its definition still count once.
        sig = o.get("type", {}).get("qualType", "")
        key = (kind, name, header, sig)
        if key in seen:
            continue
        seen.add(key)
        # Public bases, desugared to their qualified spelling. A manifest must
        # list a base before its derived classes, so the generator downstream
        # needs the inheritance edges to order its output.
        bases = []
        for b in o.get("bases", []) or []:
            if b.get("access") != "public":
                continue
            t = b.get("type", {})
            bases.append(t.get("desugaredQualType") or t.get("qualType", ""))

        entities.append({
            "name": name,
            "header": header,
            "stratum": header.split("/")[0] if "/" in header else "core",
            "kind": BINDABLE.get(kind) or TEMPLATE[kind],
            "signature": sig,
            "bases": bases,
            "bindable": kind in BINDABLE,
        })

    frame = {
        "library": a.namespace,
        "namespace": a.namespace,
        "header_dir": a.header_dir,
        "headers_scanned": len(headers),
        "excluded_dirs": a.exclude_dir,
        "type_names": sorted(type_names),
        "entities": sorted(entities, key=lambda e: (e["header"], e["kind"], e["name"])),
    }
    pathlib.Path(a.out).write_text(json.dumps(frame, indent=2) + "\n")

    n = len(entities)
    b = sum(1 for e in entities if e["bindable"])
    print(f"{a.namespace}: {len(headers)} headers -> {n} declared entities "
          f"({b} nameable, {n - b} templates)")
    for kind in ("class", "enum", "function", "class_template", "function_template"):
        c = sum(1 for e in entities if e["kind"] == kind)
        if c:
            print(f"    {kind:<20} {c}")
    names = {}
    for e in entities:
        if e["kind"] == "function":
            names.setdefault((e["header"], e["name"]), 0)
            names[(e["header"], e["name"])] += 1
    if names:
        multi = sum(1 for v in names.values() if v > 1)
        print(f"    free-function names  {len(names)} "
              f"({multi} overloaded, max set {max(names.values())})")


if __name__ == "__main__":
    main()
