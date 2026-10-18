# The pendulum from Python, through the nanobind target.
#
#   ./run.sh python        (generates and builds first)
#
# Everything this script relies on came from the C++ header and the side-car:
# the docstrings (help() below), the keyword names, the range check on
# `length`, the read-only `time`.
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "bindings", "nanobind", "build"))

import pendulum as lab

print(lab.Pendulum.__doc__)
print(lab.Pendulum.simulate.__doc__)

p = lab.Pendulum()

# The side-car's range and readonly are enforced by the binding.
for field, value in (("length", 25.0), ("time", 3.0)):
    try:
        setattr(p, field, value)
    except (ValueError, AttributeError) as e:
        print(f"{field} = {value}: {type(e).__name__}: {e}")

# The rod length for a 2 s period (a clock that ticks once per second).
g = 4 * math.pi**2 * p.length / p.period() ** 2
p.length = g / math.pi**2
print(f"\nrod for a 2 s period: L = {p.length:.5f} m, period() = {p.period():.6f} s")

# Half a period later, an undamped pendulum is at the mirror of its start.
p.damping = 0.0
end = p.simulate(seconds=p.period() / 2, dt=1e-4)
print(f"after half a period: {end:.2f} deg (started at 30 deg), time = {p.time:.3f} s")


def measured_period(a0, dt=1e-4):
    """Time two full swings from the zero crossings."""
    q = lab.Pendulum()
    q.damping, q.angle = 0.0, a0
    prev, tprev, cross = q.angle, 0.0, []
    while len(cross) < 5:
        a = q.simulate(seconds=dt, dt=dt)
        if prev * a < 0:
            cross.append(tprev + (q.time - tprev) * prev / (prev - a))
        prev, tprev = a, q.time
    return (cross[4] - cross[0]) / 2, q.period()


print("\nlarge swings break the small-angle formula:")
for a0 in (5, 30, 80):
    T, T0 = measured_period(a0)
    print(f"  {a0:2d} deg: simulated {T:.5f} s, period() {T0:.5f} s, ratio {T / T0:.4f}")
