# 06 Structured diagnostics — Chapters 4 and 28 — negative `RUNNABLE`

本例故意从声明为 `i32` 的函数返回字符串。`polyc --check` 应返回非零，并输出一个 LSP-shaped JSON object；绝对 URI 和精确 range 来自本机路径，稳定断言见 `expected_result.md`。

This intentionally returns a string from an `i32` function. `polyc --check` should return nonzero and emit an LSP-shaped JSON object; the absolute URI and exact range are machine-derived, while stable assertions are in `expected_result.md`.

正规化后的本机实测对象见 `expected_diagnostic.json`；其中 `<SOURCE>` 仅替代绝对目录。

The normalised object observed locally is in `expected_diagnostic.json`; `<SOURCE>` only replaces the absolute directory.
