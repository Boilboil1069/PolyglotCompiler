# Sample `01_basic_linking_v2`

Mirror of [`01_basic_linking`](../01_basic_linking/) using the **canonical / signed `LINK` form**.

| Field     | Value |
| ---       | --- |
| Languages | C++, Python |
| Keywords  | LINK (signed form), CALL, IMPORT, EXPORT, MAP_TYPE |
| Entry     | `basic_linking.ploy` |

## What is different from v1

The v1 sample uses the historical comma form:

```ploy
LINK(cpp, python, math_ops::add, string_utils::concat) RETURNS cpp::int { ... }
```

This v2 sample uses the recommended **signed** form, which embeds an
explicit function signature:

```ploy
LINK cpp::math_ops::add AS FUNC(cpp::int, cpp::int) -> cpp::int { ... }
```

Both forms still parse, but the comma form is now reported by the
semantic analyzer with a deprecation warning (diagnostic
`kDeprecatedKeyword`).  New code SHOULD use the signed form.

## How to run

> **Current version skip**: depends on cross-language LINK lowering (variant V2).  The harness reads `expected_output.skip` and routes this sample to the SKIP bucket; rename back to `expected_output.txt` once the dependency lands.

## Build

```powershell
polyc 01_basic_linking_v2\basic_linking.ploy --emit-obj=basic_linking.pobj --obj-format=pobj
polyld basic_linking.pobj -o basic_linking.exe
```

## Expected runtime output

Bytes pinned in `expected_output.txt` (CR LF line ending):

```
01_basic_linking_v2: ok
```

The `build_all_samples` harness under `scripts/` runs the produced
executable and byte-compares its stdout against `expected_output.txt`.

## Chinese version

See [`README_zh.md`](./README_zh.md) for the Chinese counterpart.
