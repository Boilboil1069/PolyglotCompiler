# Sample `06_attribute_access`

GET / SET reading and writing foreign object attributes across the language boundary.

| Field | Value |
| --- | --- |
| Languages | C++, Python |
| Keywords  | GET, SET |
| Entry     | `attribute_access.ploy` |

## How to run

> **Current version skip**: depends on attribute / field access lowering across foreign objects.  The harness reads `expected_output.skip` and routes this sample to the SKIP bucket; rename back to `expected_output.txt` once the dependency lands.

## Build

```powershell
polyc 06_attribute_access\attribute_access.ploy --emit-obj=attribute_access.pobj --obj-format=pobj
polyld attribute_access.pobj -o attribute_access.exe
```

## Expected runtime output

Bytes pinned in `expected_output.txt` (CR LF line ending):

```
06_attribute_access: ok
```

The `build_all_samples` harness under `scripts/` runs the produced
executable and byte-compares its stdout against `expected_output.txt`.
The cross-language behaviour described above is exercised by the unit
and end-to-end suites under `tests/unit/` and `tests/integration/`;
the trailing `PRINTLN` marker is the only stdout currently verified
end-to-end on the host loader.

## Chinese version

See [`README_zh.md`](./README_zh.md) for the Chinese counterpart.
