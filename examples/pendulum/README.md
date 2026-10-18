# Pendulum — one documented class, every target, and an AI agent

A single C++ class, `lab::Pendulum`, documented the way a scientific library
usually is, and bound from one [manifest](manifest.json):

| Target | What you get | Try it |
|---|---|---|
| `nanobind` | a Python module, `import pendulum` | `./run.sh python` |
| `lua` | a sol2 module, `require("pendulum")` | `./run.sh lua` |
| `mcp` | an MCP server an AI agent (Claude) drives | `./run.sh mcp` |
| `qt`, `qml`, `imgui` | property inspectors (sections, units, a plot of the trace, a confirmation before `reset`) | `cmake -S bindings/qt -B bindings/qt/build && cmake --build bindings/qt/build` |
| `markdown`, `html`, `typescript`, `openapi` | references, typings and a JSON Schema carrying every constraint | generated with the rest |

This is the example of chapter 16 of the [book](../../docs/book/rosetta-book.pdf)
("Driving a library from an AI agent"). The five conversations in that chapter
were run against this server.

## Where the documentation comes from

[`pendulum.h`](pendulum.h) is plain C++: no rosetta include, no annotations.
What every target knows about the class comes from two places:

- **the header's Doxygen comments**: the class comment, and `@brief` / `@param`
  / `@return` on `simulate()`;
- **[`Pendulum.ann.json`](Pendulum.ann.json)**, a side-car using most of the
  [annotation set](../../docs/ANNOTATIONS.md): `doc`, `unit`, `range`, `min` /
  `max`, enforced `combobox` choices, `length` and `regex` on `name`, `readonly`,
  a `plot` of the trace shown only while `recordTrace` is on (`visible_if`), a
  `hidden` scratch field, `group` / `order` / `step` / `decimals` for the
  panels, and on the methods a `default` for `dt`, a `requires` precondition,
  an `example`, `since`, a `deprecated` `oldPeriod()` and a `confirm` on
  `reset()`.

All targets read the same metadata:

| You wrote | Python (nanobind) | Lua | MCP (what the agent reads) |
|---|---|---|---|
| class `///` comment | `Pendulum.__doc__` | — | class `doc` in `list_classes` |
| `@brief` / `@param` / `@return` | `simulate.__doc__`, with Args / Returns | — | method `doc`, with Args / Returns |
| parameter names | keyword arguments | — | `params[].name`, named arguments |
| `range`, `min` / `max`, choices, `length`, `regex` | `ValueError` on assignment | error on assignment | in `describe_class`; rejected by `set_fields` |
| `readonly` | no setter (`AttributeError`) | read-only property | `"readonly": true`; rejected by `set_fields` |
| `unit`, `example`, `since`, `deprecated` | docstring lines | — | in `describe_class` |
| `default` on `simulate` | `simulate(seconds, dt=0.001)` | — | `"default": 0.001`; `dt` may be omitted |
| `requires` on `simulate` | docstring line | — | `dt = 0.5` refused: "time step too coarse" |
| `confirm` on `reset` | docstring line | — | refused without `"confirm": true` |
| `hidden` on `scratch` | not bound | not bound | not listed |

Keeping the annotations in a side-car rather than inline
(`[[= rosetta::range{0.1, 10.0}]]`) is what lets every target build with a
stock compiler: the bindings include `pendulum.h`, and inline annotations would
make it C++26. The book shows the inline form; the metadata is identical.

## Run it

```sh
./run.sh            # Python and Lua
./run.sh mcp        # the MCP server: a scripted session, then how to connect Claude
```

Generation (the first run) needs clang-p2996, like every example. After that,
everything builds with a stock compiler.

What the Python script prints, in short:

```
length = 25.0: ValueError: length out of range [0.1, 10]
time = 3.0: AttributeError: property of 'Pendulum' object has no setter
rod for a 2 s period: L = 0.99396 m, period() = 2.000000 s
large swings break the small-angle formula:
   5 deg: simulated 2.00702 s, period() 2.00607 s, ratio 1.0005
  80 deg: simulated 2.28189 s, period() 2.00607 s, ratio 1.1375
```

## Ask Claude

```sh
claude mcp add pendulum -- "$PWD/bindings/mcp/build/pendulum_mcp"
claude
```

Then ask questions about the physics, not about the API. These are the prompts
used in the book:

- *What can this library do? Give me a one-paragraph tour.*
- *Make a pendulum with a 15 metre rod and release it from 120 degrees.* (It
  reads the ranges and declines before touching anything.)
- *I want a pendulum that ticks once per second, i.e. a period of exactly 2
  seconds. What rod length do I need? Set it and verify it by simulation.*
- *Starting from 30 degrees, what damping makes the swing amplitude drop to
  half after 20 seconds of simulation? Find it numerically.*
- *Is the small-angle period formula still accurate if I release the pendulum
  from 80 degrees? Measure the real period by simulation and compare.*

The server includes `run_script` when CMake finds Lua 5.3+, so Claude can
answer the last three in one Lua script each. Configure with
`-DROSETTA_MCP_PYTHON=ON` to add `run_python` as well (not sandboxed).

In the book's damping session, Claude noticed that the header said what
`damping` is for but not how it enters the equation of motion. The side-car
here now says so (`theta'' = -(g/L) sin(theta) - c theta'`). An agent is only
as precise as the documentation it reads.
