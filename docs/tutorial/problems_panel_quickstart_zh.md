# 问题面板快速上手

> **文档版本**：2.0.0  
> **更新日期**：2026-05-07  
> **项目**：PolyglotCompiler 1.45.2  
> **配套文件**：[problems_panel_quickstart.md](problems_panel_quickstart.md)

本 5 分钟教程展示首发于 v1.21.0、并在 v1.45.2 持续打磨的实时诊断流程。

## 1. 打开 `.ploy` 文件

启动 `polyui`，然后 **文件 → 打开** 任意 `.ploy` 源文件。编辑器一打开，`IdeLspBridge` 即为该缓冲区启动 `polyls` 并发送 `textDocument/didOpen`。

## 2. 制造一个错误

故意输入残缺一行，如：

```ploy
LET x = 1 +
```

约 200 ms（变更去抖窗口）内可见：

- 编辑器中相关列出现红色波浪线；
- 状态栏计数从 `E:0 W:0 H:0` 切换为 `E:1 W:0 H:0`，`E:1` 显示为红色。

## 3. 打开问题面板

点击状态栏中的 `E:1` — `MainWindow` 调用 `ShowPanel("problems")`，停靠窗在底部弹出。你会看到一行按文件名分组的条目，来源为 `polyls:ploy`。

## 4. 过滤

- 取消勾选 **Warning / Info / Hint** 仅显示错误。
- 在 **File** 框中输入文件名片段 — 对 basename 做大小写不敏感的子串匹配。
- 在 **Message regex** 中输入如 `^expected` 的正则 — 无效模式被静默忽略，可放心试验。

## 5. 跳转

双击任意一行。对应标签被激活，光标置于诊断位置。

## 6. CLI 回退（`polyc --check`）

无运行中 LSP 的场景（CI、沙箱编辑器），同一份诊断可经编译器驱动获取：

```sh
polyc --check path/to/file.ploy
```

`polyc` 在 `stdout` 写出符合 LSP `PublishDiagnosticsParams` 形态的单份 JSON，并以 `0`（干净）、`1`（有错误）或 `2`（用法 / I/O 失败）退出。可管道送入 `jq` 检视：

```sh
polyc --check broken.ploy | jq '.diagnostics[] | {sev:.severity, msg:.message}'
```

## 7. 大型工作区的后台扫描

当活动工作区包含超过 2 000 个文件时，首扫异步进行，按每 50 ms tick 50 个文件分批执行。进度以 `Scanning N/M …` 显示在状态栏。扫描完成后，`QFileSystemWatcher` 对任何后续的创建 / 删除 / 重命名事件保持结果实时。

## 8. 诊断码目录

每条诊断携带稳定 id，形如 `polyc-(err|warn)-<E####|W####>`。完整目录见 [docs/specs/ploy_diagnostics.md](../specs/ploy_diagnostics.md)；常见条目列于 [ploy_language_tutorial_zh.md](ploy_language_tutorial_zh.md) 第 20 节。

## 参见

- [docs/realization/problems_panel.md](../realization/problems_panel.md) — 设计说明。
- [docs/USER_GUIDE_zh.md](../USER_GUIDE_zh.md) 第 13 章 — 完整参考。
- [lsp_quickstart_zh.md](lsp_quickstart_zh.md) — 基础 LSP 设置。
