# python (pybind11) demo — module "pygeom".
#
# Build first:
#   cmake -S bindings/python -B bindings/python/build
#   cmake --build bindings/python/build -j
# then run:
#   python3 example_pybind11.py
#
# rosetta gives the pybind11 and nanobind backends an identical Python API, so
# this script and example_nanobind.py differ only in the build dir + module name.
import os
import sys

sys.path.insert(
    0, os.path.join(os.path.dirname(__file__), "bindings", "python", "build")
)

import pygeom as geom

s = geom.Surface([0, 0, 0, 1, 0, 0, 0, 1, 0], [0, 1, 2])

model = geom.Model()
model.addSurface(s)

for s in model.getSurfaces():
    print("Model surface points   :", [(n.x, n.y, n.z) for n in s.getPoints()])
    print("Model surface triangles:", [(t.a, t.b, t.c) for t in s.getTriangles()])

# `transform` is a free (non-member) function bound from common.h. It takes a
# Point and returns a new Point swizzled to (x*2, z*3, y*4).
p = geom.Point(1, 2, 3)
q = geom.transform(p)
print("transform(1, 2, 3) =>", (q.x, q.y, q.z))
