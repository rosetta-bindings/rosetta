# E3 — generation cost and scalability: results

## Where the time goes

Per configuration, seconds. `cmake configure` probes the toolchain and is not a per-library cost; it is shown so that the total is accounted for.

| config | members | targets | rosetta_gen | cmake configure | **driver compile** | generator run | peak RSS (MB) |
|---|---:|---:|---:|---:|---:|---:|---:|
| A-c1 | 7 | 3 | 0.009 | 0.583 | **2.39** | 0.52 | 252.3 |
| A-c2 | 14 | 3 | 0.007 | 0.357 | **2.373** | 0.255 | 266.6 |
| A-c4 | 28 | 3 | 0.007 | 0.348 | **2.507** | 0.221 | 261.2 |
| A-c8 | 56 | 3 | 0.007 | 0.341 | **2.994** | 0.26 | 266.4 |
| A-c16 | 112 | 3 | 0.007 | 0.343 | **3.663** | 0.233 | 287.5 |
| A-c32 | 224 | 3 | 0.006 | 0.349 | **5.161** | 0.208 | 316.5 |
| A-c64 | 448 | 3 | 0.007 | 0.351 | **8.302** | 0.225 | 338.2 |
| A-c128 | 896 | 3 | 0.008 | 0.335 | **14.446** | 0.265 | 424.8 |
| B-m1 | 64 | 3 | 0.01 | 0.361 | **3.087** | 0.81 | 269.6 |
| B-m2 | 80 | 3 | 0.006 | 0.344 | **3.21** | 0.265 | 278.3 |
| B-m4 | 112 | 3 | 0.006 | 0.338 | **3.827** | 0.289 | 283.8 |
| B-m8 | 176 | 3 | 0.007 | 0.353 | **4.722** | 0.291 | 292.0 |
| B-m16 | 304 | 3 | 0.007 | 0.338 | **6.436** | 0.258 | 314.9 |
| C-f1 | 80 | 3 | 0.007 | 0.33 | **3.364** | 0.263 | 280.2 |
| C-f2 | 96 | 3 | 0.007 | 0.335 | **3.516** | 0.344 | 275.8 |
| C-f4 | 128 | 3 | 0.007 | 0.348 | **3.803** | 0.26 | 280.6 |
| C-f8 | 192 | 3 | 0.007 | 0.347 | **4.407** | 0.283 | 286.7 |
| C-f16 | 320 | 3 | 0.007 | 0.338 | **5.468** | 0.205 | 293.9 |
| D-r1 | 112 | 3 | 0.007 | 0.339 | **3.681** | 0.202 | 273.0 |
| D-r2 | 176 | 3 | 0.007 | 0.336 | **4.715** | 0.305 | 300.7 |
| D-r3 | 240 | 3 | 0.007 | 0.366 | **5.71** | 0.206 | 312.5 |
| D-r4 | 304 | 3 | 0.007 | 0.35 | **6.82** | 0.216 | 305.0 |
| D-r5 | 368 | 3 | 0.011 | 0.385 | **7.74** | 0.229 | 326.1 |
| E-t1 | 112 | 1 | 0.007 | 0.381 | **3.645** | 0.156 | 287.3 |
| E-t3 | 112 | 3 | 0.007 | 0.335 | **3.668** | 0.788 | 282.3 |
| E-t5 | 112 | 5 | 0.007 | 0.332 | **3.599** | 0.303 | 299.7 |
| E-t10 | 112 | 10 | 0.007 | 0.331 | **3.618** | 0.174 | 293.3 |
| E-t19 | 112 | 19 | 0.007 | 0.34 | **3.636** | 0.174 | 284.5 |

The P2996 driver compile is **79.0–98.1%** of pipeline wall-clock (excluding the CMake probe), median 93.8%.

## Scaling

Ordinary least squares over each sweep, against the quantity that moved. Sweep E is excluded: its x-axis counts targets, which are not interchangeable units, so a slope over it would not mean anything. It gets its own section below.

| sweep | varied | driver compile (s) | generator run (s) | emitted source (lines) |
|---|---|---|---|---|
| A | classes in the library | 2.18 + 0.09563·class s (R²=0.9998) | 0.29 + -0.0005112·class s (R²=0.0495) | 171.00 + 116·class lines (R²=1.0000) |
| B | method names per class | 2.86 + 0.2259·method/class s (R²=0.9975) | 0.50 + -0.01955·method/class s (R²=0.2481) | 1259.00 + 192·method/class lines (R²=1.0000) |
| C | fields per class | 3.24 + 0.1404·field/class s (R²=0.9991) | 0.31 + -0.00582·field/class s (R²=0.5033) | 1498.21 + 176.1·field/class lines (R²=1.0000) |
| D | overloads per method name | 2.67 + 1.022·overload s (R²=0.9994) | 0.24 + -0.0035·overload s (R²=0.0171) | 1261.20 + 872.6·overload lines (R²=0.9958) |

Pooled over sweeps A–D — every configuration that changes the library, whatever knob moved it — the driver compile is **2.18 s + 13.69 ms per reflected member** (R²=0.9870).

## What a member costs, by kind

Sweeps B, C and D each add exactly one kind of member, so fitting each against the member count reads off the cost of that kind. Sweep A adds whole classes, so its slope carries the per-class overhead as well.

| sweep | member added | ms per member | R² |
|---|---|---:|---:|
| A | whole class (3 fields + 4 methods) | 13.66 | 0.9998 |
| B | method, distinct name | 14.12 | 0.9975 |
| C | field | 8.78 | 0.9991 |
| D | overload of an existing name | 15.97 | 0.9994 |

## Emitting more targets

The library is identical across this sweep; only the number of targets the manifest asks for changes.

| targets | driver compile (s) | generator run (s) | emitted source (lines) | files |
|---:|---:|---:|---:|---:|
| 1 | 3.645 | 0.156 | 1582 | 6 |
| 3 | 3.668 | 0.788 | 2027 | 15 |
| 5 | 3.599 | 0.303 | 2460 | 21 |
| 10 | 3.618 | 0.174 | 5553 | 58 |
| 19 | 3.636 | 0.174 | 9998 | 85 |

Driver compile across 1→19 targets: 3.60–3.67 s (spread 1.9% of the mean). The reflective walk is per-library, not per-target: the backends consume the IR at run time, and all nineteen are linked into every driver whether the manifest names them or not.

## Compiled footprint of the metadata tables

`auto_dynamic.cpp` built with the system compiler at `-O2`. `data` is the descriptor tables and their strings; `text` is the emitted per-member call thunks.

| config | members | data (B) | text (B) | total (B) | B/member |
|---|---:|---:|---:|---:|---:|
| A-c1 | 7 | 1780 | 6272 | 8052 | 1150 |
| A-c2 | 14 | 2814 | 7320 | 10134 | 724 |
| A-c4 | 28 | 4882 | 9400 | 14282 | 510 |
| A-c8 | 56 | 9018 | 13592 | 22610 | 404 |
| B-m1 | 64 | 11521 | 18456 | 29977 | 468 |
| B-m2 | 80 | 13448 | 19608 | 33056 | 413 |
| C-f1 | 80 | 13788 | 14804 | 28592 | 357 |
| C-f2 | 96 | 15459 | 16532 | 31991 | 333 |
| A-c16 | 112 | 17302 | 21912 | 39214 | 350 |
| B-m4 | 112 | 17302 | 21912 | 39214 | 350 |
| D-r1 | 112 | 17302 | 21912 | 39214 | 350 |
| E-t1 | 112 | 17302 | 21912 | 39214 | 350 |
| E-t3 | 112 | 17302 | 21912 | 39214 | 350 |
| E-t5 | 112 | 17302 | 21912 | 39214 | 350 |
| E-t10 | 112 | 17302 | 21912 | 39214 | 350 |
| E-t19 | 112 | 17302 | 21912 | 39214 | 350 |
| C-f4 | 128 | 18973 | 23640 | 42613 | 333 |
| B-m8 | 176 | 25010 | 26520 | 51530 | 293 |
| D-r2 | 176 | 26523 | 28824 | 55347 | 314 |
| C-f8 | 192 | 25657 | 33240 | 58897 | 307 |
| A-c32 | 224 | 33878 | 38548 | 72426 | 323 |
| D-r3 | 240 | 34203 | 33688 | 67891 | 283 |
| B-m16 | 304 | 40432 | 35736 | 76168 | 251 |
| D-r4 | 304 | 44960 | 42136 | 87096 | 286 |
| C-f16 | 320 | 39031 | 54360 | 93391 | 292 |
| D-r5 | 368 | 52640 | 49304 | 101944 | 277 |
| A-c64 | 448 | 67030 | 71888 | 138918 | 310 |
| A-c128 | 896 | 133390 | 138504 | 271894 | 303 |

Fit: **5228 B + 287 B per member** (R²=0.9906).

Compiling those tables with the system compiler costs **0.51 s + 1.32 ms per member** (R²=0.9409) — an order of magnitude less per member than the reflective walk that produced them.

## Repeatability

The base configuration (16 classes × 4 methods × 3 fields, 3 targets) is generated independently by 4 sweeps. Driver compile: 3.66, 3.67, 3.68, 3.83 s — mean 3.71 s, spread 4.4% of the mean.

Sweep E gives a second, independent estimate: 5 configurations over the *same* library, differing only in how many targets they emit — a quantity the driver compile does not depend on. Spread 1.9% of the mean.

Differences below those spreads are noise, not measurements.
