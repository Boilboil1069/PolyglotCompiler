# Sample `12_dotnet_interop`

Cross-language NEW / METHOD against a .NET (C#) class fed by Python statistics utilities.

| Field | Value |
| --- | --- |
| Languages | C#, Python |
| Keywords  | NEW, METHOD (.NET) |
| Entry     | `dotnet_interop.ploy` |

## Build

```powershell
polyc 12_dotnet_interop\dotnet_interop.ploy --emit-obj=dotnet_interop.pobj --obj-format=pobj
polyld dotnet_interop.pobj -o dotnet_interop.exe
```

## Expected runtime output

Bytes pinned in `expected_output.txt` (CR LF line ending):

```
12_dotnet_interop: ok
```

The `build_all_samples` harness under `scripts/` runs the produced
executable and byte-compares its stdout against `expected_output.txt`.
The cross-language behaviour described above is exercised by the unit
and end-to-end suites under `tests/unit/` and `tests/integration/`;
the trailing `PRINTLN` marker is the only stdout currently verified
end-to-end on the host loader.

## Chinese version

See [`README_zh.md`](./README_zh.md) for the Chinese counterpart.
