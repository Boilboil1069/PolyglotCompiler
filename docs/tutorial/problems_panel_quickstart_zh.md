# Problems 面板快速上手

5 分钟走一遍 v1.21.0 引入的实时诊断流程。

## 1. 打开一个 Ploy 文件

启动 `polyui`，**文件 -> 打开**任意 `.ploy` 源。编辑器一打开，
`IdeLspBridge` 就会为该缓冲区拉起 `polyls` 并发送
`textDocument/didOpen`。

## 2. 故意制造一个错误

输入一行错误代码，例如：

```ploy
LET x = 1 +
```

约 200 ms 抖动窗口后，你会看到：

* 编辑器中对应位置出现红色波浪线；
* 状态栏计数器从 `E:0 W:0 H:0` 变为 `E:1 W:0 H:0`，
  其中 `E:1` 显示为红色。

## 3. 打开 Problems 面板

点击状态栏中的 `E:1` 链接，`MainWindow` 会调用 `ShowPanel("problems")`，
底部面板弹出。你会看到一行按文件名分组、来源为 `polyls:ploy` 的诊断。

## 4. 过滤

* 取消勾选 **Warning / Info / Hint** 仅显示 error。
* 在 **File** 输入框键入部分文件名——按文件名做大小写不敏感子串匹配。
* 在 **消息正则** 输入框试输 `^expected`——非法正则会被静默忽略，
  尽情试。

## 5. 跳转

双击任一行：对应标签页会被激活，光标定位到诊断位置。

## 6. CLI 回退（`polyc --check`）

在没有 LSP 的环境（CI、沙箱编辑器），可以通过编译器驱动获得相同诊断：

```
polyc --check path/to/file.ploy
```

`polyc` 会向 `stdout` 写出一份与 LSP `PublishDiagnosticsParams` 同形的
JSON，并以 `0`（干净）、`1`（存在 error）或 `2`（用法 / I/O 失败）退出。
可以管道到 `jq` 查看：

```
polyc --check broken.ploy | jq '.diagnostics[] | {sev:.severity, msg:.message}'
```

## 7. 大型工作区后台扫描

工作区文件数超过 2000 时，首检异步进行，每节拍 50 文件、节拍 50 ms。
状态栏显示 `Scanning N/M ...` 进度。扫描结束后，
`QFileSystemWatcher` 会持续侦听后续的创建 / 删除 / 重命名事件。

## 相关文档

* `docs/realization/problems_panel_zh.md`：设计说明。
* `docs/USER_GUIDE_zh.md` 第 13 章：参考手册。
* `docs/tutorial/lsp_quickstart_zh.md`：LSP 基础配置。
