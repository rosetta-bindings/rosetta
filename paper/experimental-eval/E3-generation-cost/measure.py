#!/usr/bin/env python3
"""Run one command and record what it cost, into a per-configuration JSON file.

    measure.py timing.json driver_compile -- cmake --build gen/build

Wall-clock comes from a monotonic clock rather than from `date +%s`, because the
smallest configurations here finish in about two seconds and whole-second
resolution would quantise the very slope the experiment is trying to fit.

Peak memory is the max RSS of the largest reaped descendant (RUSAGE_CHILDREN),
which for a `cmake --build` of a single translation unit is the compiler itself
-- the number that decides whether a P2996 build fits on a given machine.
"""
import json, pathlib, resource, subprocess, sys, time


def main():
    if "--" not in sys.argv:
        raise SystemExit("usage: measure.py OUT.json KEY -- cmd ...")
    split = sys.argv.index("--")
    out_path, key = sys.argv[1], sys.argv[2]
    cmd = sys.argv[split + 1:]

    before = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    t0 = time.monotonic()
    rc = subprocess.call(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    wall = time.monotonic() - t0
    after = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss

    # ru_maxrss is bytes on Darwin, kilobytes on Linux.
    scale = 1 << 20 if sys.platform == "darwin" else 1 << 10
    peak_mb = max(after, before) / scale

    p = pathlib.Path(out_path)
    data = json.loads(p.read_text()) if p.is_file() else {}
    data[key] = {"wall_s": round(wall, 3), "peak_rss_mb": round(peak_mb, 1), "rc": rc}
    p.write_text(json.dumps(data, indent=4) + "\n")

    print(f"    {key:<16} {wall:7.2f}s  peak {peak_mb:6.0f} MB"
          + ("" if rc == 0 else f"  [rc={rc}]"))
    return rc


if __name__ == "__main__":
    sys.exit(main())
