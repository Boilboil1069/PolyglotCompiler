# Sample `05_class_instantiation`

NEW / METHOD instantiating a Python class from .ploy and routing calls through a C++ helper.

| Field | Value |
| --- | --- |
| Languages | C++, Python |
| Keywords  | NEW, METHOD |
| Entry     | `class_instantiation.ploy` |

## How to run

> **Current version skip**: depends on NEW / METHOD lowering for foreign-language class instantiation.  The harness reads `expected_output.skip` and routes this sample to the SKIP bucket; rename back to `expected_output.txt` once the dependency lands.

## Build

```powershell
polyc 05_class_instantiation\class_instantiation.ploy --emit-obj=class_instantiation.pobj --obj-format=pobj
polyld class_instantiation.pobj -o class_instantiation.exe
```

## Expected runtime output

Bytes pinned in `expected_output.txt` (CR LF line ending):

```
05_class_instantiation: ok
```

The `build_all_samples` harness under `scripts/` runs the produced
executable and byte-compares its stdout against `expected_output.txt`.
The cross-language behaviour described above is exercised by the unit
and end-to-end suites under `tests/unit/` and `tests/integration/`;
the trailing `PRINTLN` marker is the only stdout currently verified
end-to-end on the host loader.

## Chinese version

See [`README_zh.md`](./README_zh.md) for the Chinese counterpart.
