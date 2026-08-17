#!/usr/bin/env python3
"""serie::Serie from Python.

    ./run.sh                # or:
    PYTHONPATH=bindings/python python3 drive.py
"""

import serie

# 3 items of itemSize 3 — a serie of 3D vectors
v = serie.Serie([0, 0, 0, 1, 0, 0, 0, 1, 0], 3)

print("describe   :", v.describe())
print("count      :", v.count(), "items of", v.itemSize(), "->", v.size(), "scalars")
print("item(1)    :", v.item(1))            # a Python list of 3 floats
print("raw        :", v.raw())

# itemSize 1 — a serie of scalars
s = serie.Serie([1.0, 2.0, 4.0], 1)
print("scalars    :", s.scalars())
print("scalar(1)  :", s.scalar(1))

# append returns *this, so calls chain
print("append     :", s.append(serie.Serie([9.0], 1)).scalars())

# the itemSize contract is enforced, and arrives as a native exception
try:
    v.scalars()
except Exception as e:
    print("scalars/3  :", type(e).__name__, "-", e)

# a free function taking a list of bound objects; the count-1 serie broadcasts
a = serie.Serie([1, 2, 3, 4, 5, 6], 3)
b = serie.Serie([10, 10, 10], 3)             # uniform
w = serie.weightedSum([a, b], [2.0, 1.0])
print("weightedSum:", w.describe(), w.raw())

# Callbacks: these take a std::function, not a template, so pybind11 turns a
# Python callable into one — the lambda really does run inside C++.
seen = []
s.forEach(seen.append)
print("forEach    :", seen)
print("map        :", s.map(lambda x: x * x).scalars())
print("reduce     :", s.reduce(lambda acc, x: acc + x, 0.0))

# the out-of-line doc became a real docstring
print("doc        :", serie.Serie.item.__doc__.splitlines()[-1])
