# Expected result

## 当前可验证 / Currently verifiable

- `polyc --check main.poly`：退出码 0，`diagnostics` 含两个 severity 2 warning，正规化后的完整对象见 `expected_diagnostics.json`。
- 第一条 `E3003` 说明 `AWAIT` 结果当前解析为 `Any/Unknown`，应增加显式类型或补全 typed `Future<T>` 传播。
- 第二条 `E3003` 说明 `CATCH err: ERROR` 尚未被识别为内建 `Error`，当前按 `Error` 继续分析。
- `run_async` 的逻辑结果是 `8`，但该函数没有被入口调用，也不会产生 stdout。

- `polyc --check main.poly`: exit code 0 with two severity-2 warnings; the complete normalised object is in `expected_diagnostics.json`.
- The first `E3003` says the `AWAIT` result currently resolves to `Any/Unknown`, exposing incomplete typed-`Future<T>` propagation.
- The second `E3003` says `CATCH err: ERROR` is not recognised as the built-in `Error` and is treated as `Error` for continued analysis.
- The logical result of `run_async` is `8`; it is not invoked by the entry point and produces no stdout.

## 完整 EH 路径闭合后的目标输出 / Intended output after the EH path is closed

```text
caught
finally
```

在当前版本中，不应仅凭 parser/Sema 成功就把这两行当作已经证明的 executable output。

Do not treat these lines as proven executable output from parser and Sema success alone in the current version.
