# Sample `23_http_client`

> HTTP client demo.

| Field | Value |
| --- | --- |
| Languages | Python, Go |
| Entry | `http_client.ploy` |
| Theme | HTTP client demo |
| Expected stdout | `23_http_client: ok\r\n` |

## Files

- `http_client.ploy` — `.ploy` entry that wires the host sources together.
- `expected_output.txt` — byte-exact runtime stdout the regression harness compares against.
- `HttpTransport.go` — host source file
- `response_decoder.py` — host source file

## Build

```powershell
polyc http_client.ploy --emit-obj=build/http_client.obj --quiet
polyld build/http_client.obj -o build/http_client.exe
./build/http_client.exe
```

## Expected runtime output

```
23_http_client: ok
```

## Regression harness

The sample participates in the matrix exercised by
`scripts/build_all_samples.ps1` (and its POSIX twin
`scripts/build_all_samples.sh`).  The harness writes
`build/samples_report.json` containing one entry per sample with status
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`.

Bilingual sibling: [README_zh.md](./README_zh.md).
