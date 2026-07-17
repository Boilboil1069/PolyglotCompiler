# Expected result

- `polyc --check type_error.ploy` exits with code 1.
- stdout is one JSON object with `uri` and a non-empty `diagnostics` array.
- At least one diagnostic has `severity: 1`, `source: "polyc"`, and a type/return mismatch message.
- LSP positions are zero-based; the absolute `file://` URI is intentionally not compared across machines.

该负例的成功标准是“诊断被可靠拒绝”，不是进程退出 0。

Success for this negative example means reliable rejection, not process exit code 0.

