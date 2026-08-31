# E4 — runtime overhead: results

## Layer A — C++ only (isolates late binding)

| operation | direct | dyn (by name) | dyn (cached) | dyn/direct | cached/direct |
|---|---:|---:|---:|---:|---:|
| trivial-call | 3.6 ns | 51.5 ns | 4.2 ns | 14.4× | 1.2× |
| field-get | 3.6 ns | 10.8 ns | 4.2 ns | 3.0× | 1.2× |
| field-set | 3.6 ns | 75.2 ns | 4.2 ns | 21.0× | 1.2× |
| 3-arg-call | 3.6 ns | 159.2 ns | 14.6 ns | 44.4× | 4.1× |
| object-return | 3.4 ns | 187.7 ns | 107.4 ns | 54.8× | 31.3× |
| vector-1000 | 386.6 ns | 17.5 µs | 7,822 ns | 45.4× | 20.2× |
| overload-3way | 3.6 ns | 629.0 ns | 14.2 ns | 175.3× | 4.0× |

## Layer B — from Python (late binding + language boundary)

| operation | generated pybind11 | scriptable meta-object | ratio |
|---|---:|---:|---:|
| trivial-call | 86.9 ns | 467.0 ns | 5.4× |
| field-get | 87.6 ns | 317.3 ns | 3.6× |
| field-set | 92.6 ns | 436.9 ns | 4.7× |
| 3-arg-call | 119.8 ns | 895.6 ns | 7.5× |
| object-return | 245.0 ns | 615.2 ns | 2.5× |
| vector-1000 | 17.1 µs | 42.9 µs | 2.5× |
| overload-3way | 108.4 ns | 1,216 ns | 11.2× |

## The two layers compared

Same operation, two ratios: what late binding costs with no interpreter (layer A), and what it costs a script author (layer B).

| operation | A: dyn/direct | B: script/static |
|---|---:|---:|
| trivial-call | 14.4× | 5.4× |
| field-get | 3.0× | 3.6× |
| field-set | 21.0× | 4.7× |
| 3-arg-call | 44.4× | 7.5× |
| object-return | 54.8× | 2.5× |
| vector-1000 | 45.4× | 2.5× |
| overload-3way | 175.3× | 11.2× |
