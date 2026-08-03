## 1

Example from Sift:

`SpatialField` → `SpatialFieldND<2>`, and so on for the other five. expose still carries the Python name, so `sift.SpatialField` is unchanged.

**Why**: the extension-attach loop compares the manifest's cls key against the reflected name:

```cpp
// generate.hxx:2021-2025
const std::string qualified = k.name_space.empty() ? k.name : k.name_space + "::" + k.name;
if (ext.cls == qualified || ext.cls == k.name) { target = &k; break; }
```

For an alias, `k.name` is `SpatialFieldND<2>` — the alias spelling never appears, and expose isn't consulted. So `sift::SpatialField` matched nothing and all 40 were dropped with a stderr note. Spelling the entry as the template-id makes the key match; `rosetta::generate<sift::SpatialFieldND<2>>` is the same type, so nothing else moved.

- That's a **workaround** in the manifest, not a fix in rosetta — worth noting since we own it.
- The **real fix** would be to also compare against exposed_of(k) in that loop, so expose works as the extension key too.
- It's the same class of bug as the **abi3** one: a request accepted and then silently discarded.

## 2

