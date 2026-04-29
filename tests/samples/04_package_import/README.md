# Sample `04_package_import`

IMPORT PACKAGE with semver constraints and CONFIG VENV pinning a Python virtualenv.

| Field | Value |
| --- | --- |
| Languages | C++, Python |
| Keywords  | IMPORT PACKAGE, CONFIG VENV |
| Entry     | `package_import.ploy` |

## Build

```powershell
polyc 04_package_import\package_import.ploy --emit-obj=package_import.pobj --obj-format=pobj
polyld package_import.pobj -o package_import.exe
```

## Expected runtime output

Bytes pinned in `expected_output.txt` (CR LF line ending):

```
04_package_import: ok
```

The `build_all_samples` harness under `scripts/` runs the produced
executable and byte-compares its stdout against `expected_output.txt`.
The cross-language behaviour described above is exercised by the unit
and end-to-end suites under `tests/unit/` and `tests/integration/`;
the trailing `PRINTLN` marker is the only stdout currently verified
end-to-end on the host loader.

## Chinese version

See [`README_zh.md`](./README_zh.md) for the Chinese counterpart.
