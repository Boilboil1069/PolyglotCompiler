# Sample `16_config_and_venv`

CONFIG VENV plus IMPORT PACKAGE version constraints driving a Python data-science task that hands results to C#.

| Field | Value |
| --- | --- |
| Languages | Python, C# |
| Keywords  | CONFIG VENV, IMPORT PACKAGE, CONVERT |
| Entry     | `config_and_venv.ploy` |

## Build

```powershell
polyc 16_config_and_venv\config_and_venv.ploy --emit-obj=config_and_venv.pobj --obj-format=pobj
polyld config_and_venv.pobj -o config_and_venv.exe
```

## Expected runtime output

Bytes pinned in `expected_output.txt` (CR LF line ending):

```
16_config_and_venv: ok
```

The `build_all_samples` harness under `scripts/` runs the produced
executable and byte-compares its stdout against `expected_output.txt`.
The cross-language behaviour described above is exercised by the unit
and end-to-end suites under `tests/unit/` and `tests/integration/`;
the trailing `PRINTLN` marker is the only stdout currently verified
end-to-end on the host loader.

## Chinese version

See [`README_zh.md`](./README_zh.md) for the Chinese counterpart.
