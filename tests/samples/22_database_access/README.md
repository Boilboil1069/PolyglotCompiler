# Sample `22_database_access`

> Database access layer.

| Field | Value |
| --- | --- |
| Languages | Python, Java |
| Entry | `database_access.ploy` |
| Theme | Database access layer |
| Expected stdout | `22_database_access: ok\r\n` |

## Files

- `database_access.ploy` — `.ploy` entry that wires the host sources together.
- `expected_output.txt` — byte-exact runtime stdout the regression harness compares against.
- `db_connection.py` — host source file
- `UserDao.java` — host source file

## Build

```powershell
polyc database_access.ploy --emit-obj=build/database_access.obj --quiet
polyld build/database_access.obj -o build/database_access.exe
./build/database_access.exe
```

## Expected runtime output

```
22_database_access: ok
```

## Regression harness

The sample participates in the matrix exercised by
`scripts/build_all_samples.ps1` (and its POSIX twin
`scripts/build_all_samples.sh`).  The harness writes
`build/samples_report.json` containing one entry per sample with status
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`.

Bilingual sibling: [README_zh.md](./README_zh.md).
