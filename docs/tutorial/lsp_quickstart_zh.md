# LSP 与 polyls 快速上手

> 配套英文版：[`lsp_quickstart.md`](./lsp_quickstart.md)。需求条目 `2026-04-28-19`，版本 1.20.0。

本教程演示如何启用仓库内置的 LSP 集成，打开一份 `.ploy` 源文件，并
验证来自 `polyls` 的诊断既出现在编辑器 gutter，也出现在底部 **LSP**
面板。

## 1. 构建 polyls

```sh
cmake -S . -B build
cmake --build build --target polyls polyui
```

两个目标产物均生成在 `build/`。

## 2. 默认配置

`polyui` 为每种语言都准备了默认配置；尝试 `.ploy` 无需任何额外设置：

```jsonc
"languageServers.enabled": true,
"languageServers.changeDebounceMs": 200,
"languageServers.servers.ploy": { "command": "polyls", "args": [] }
```

如果 `polyls` 不在 `PATH` 中，可把 `command` 改为绝对路径，或在启动
`polyui` 前把 `build/` 追加到 `PATH`。

## 3. 启动 IDE 并观察通信

1. 启动 `polyui`。
2. **文件 -> 打开文件…** 选取一份 `.ploy` 源文件。桥接器会按顺序
   发送 `initialize` -> `initialized` -> `didOpen`。
3. 写入一处语法错误（例如未闭合的字符串字面量）。约 200 ms 后
   gutter 出现红色波浪线，底部 **LSP** 面板记录到一条
   `textDocument/publishDiagnostics` 通知。
4. 保存文件（`Ctrl+S`），面板记录一条 `didSave`；关闭该标签页，
   面板记录 `didClose` 以及随后清空诊断的 `publishDiagnostics`。
5. 退出 IDE 时，桥接器对所有活动会话发送 `shutdown` + `exit`。

## 4. 替换某种语言的服务器

打开 **设置 -> Language Servers**，比如把
`languageServers.servers.python.command` 指向自编译的 Pyright。重新
打开受影响的文件即可让新配置生效。

## 5. 从其他编辑器驱动 polyls

`polyls` 是标准的 stdio LSP 服务器，任何 LSP 3.17 客户端
（VS Code、Neovim、Helix 等）都可以拉起它。完整契约见
[`docs/api/polyls_zh.md`](../api/polyls_zh.md)。
