# E2 — API evolution: results

| version | classes | methods |
|---|---:|---:|
| v1 | 10 | 40 |
| v2 | 30 | 120 |
| v3 | 130 | 520 |

## Binding size (generated source, excluding build scaffolding)

| arm | v1 | v2 | v3 |
|---|---|---|---|
| static binding (= what a developer hand-writes) | 763 | 2103 | 8803 |
| scriptable stage 1 — metadata tables | 1106 | 3006 | 12506 |
| scriptable stage 2 — the binding itself | 1208 | 1208 | 1208 |

## Human-authored manifest lines

| arm | v1 | v2 | v3 |
|---|---|---|---|
| static binding (= what a developer hand-writes) | 53 | 133 | 533 |
| scriptable stage 1 — metadata tables | 54 | 134 | 534 |
| scriptable stage 2 — the binding itself | 25 | 25 | 25 |

## Source files changed by the version bump

| arm | v1→v2 | v2→v3 |
|---|---|---|
| static binding (= what a developer hand-writes) | 4 changed, 0 added | 4 changed, 0 added |
| scriptable stage 1 — metadata tables | 2 changed, 0 added | 2 changed, 0 added |
| scriptable stage 2 — the binding itself | **0** | **0** |

## Generation wall-clock (s)

| arm | v1 | v2 | v3 |
|---|---|---|---|
| static binding (= what a developer hand-writes) | 3 + 1 | 4 + 1 | 14 + 1 |
| scriptable stage 1 — metadata tables | 3 + 0 | 5 + 0 | 14 + 0 |
| scriptable stage 2 — the binding itself | 5 + 0 | 5 + 0 | 4 + 1 |

_driver compile + generate._
