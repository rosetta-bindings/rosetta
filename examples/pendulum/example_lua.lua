-- The pendulum from Lua, through the lua (sol2) target.
--
--   ./run.sh lua           (generates and builds first)
package.cpath = "./bindings/lua/build/?.so;" .. package.cpath
local lab = require("pendulum")

local p = lab.Pendulum.new()
print(("length %.2f m, period() %.4f s"):format(p.length, p:period()))

-- The side-car's range and readonly reach Lua too.
print(pcall(function() p.length = 25 end))
print(pcall(function() p.time = 3 end))

-- Run until the swing has decayed to half its amplitude: track the peak of
-- each swing, reset at every zero crossing.
local prev, swing_peak, amp = p.angle, 0, math.abs(p.angle)
while amp > 15 do
  local a = p:simulate(0.001, 0.001)
  swing_peak = math.max(swing_peak, math.abs(a))
  if prev * a < 0 then amp, swing_peak = swing_peak, 0 end
  prev = a
end
print(("damping %.2f: amplitude below 15 deg after %.1f s"):format(p.damping, p.time))
