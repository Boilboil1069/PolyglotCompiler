# 02 Errors and async — Chapters 12 and 19 — `FRONTEND`

本例证明 TRY/CATCH/FINALLY 与 ASYNC/AWAIT 的当前 parser/Sema 表面。教材已记录 exception CFG、host exception bridge 与 typed `Future<T>` 仍是分层实现，因此 runner 只执行 `polyc --check`。

This example proves the current parser and Sema surface for TRY/CATCH/FINALLY and ASYNC/AWAIT. Exception CFG, host exception bridging, and typed `Future<T>` remain layered, so the runner performs frontend analysis only.

`expected_result.md` 分开记录当前可验证结果和目标 Runtime 输出。

`expected_result.md` separates the currently verifiable result from intended Runtime output.

