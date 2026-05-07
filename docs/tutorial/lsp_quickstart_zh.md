# LSP 与 `polyls` 快速上手

> **文档版本**：2.0.0  
> **更新日期**：2026-05-07  
> **项目**：PolyglotCompiler 1.45.2  
> **配套文件**：[lsp_quickstart.md](lsp_quickstart.md)

本教程演示如何启用树内 LSP 集成、打开 `.ploy` 文件，并验证 `polyls` 的诊断显示在编辑器侧边槽与底部 **LSP** 面板。

## 1. 构建 `polyls`

```sh
cmake -S . -B build -G Ninja
cmake --build build --target polyls polyui
```

两个目标都会在 `build/` 下生成可执行文件。

## 2. 默认配置

`polyui` 自带每种语言的 LSP 默认配置，试用 `.ploy` 无需改动：

```jsonc
"languageServers.enabled": true,
"languageServers.changeDebounceMs": 200,
"languageServers.servers.ploy":       { "command": "polyls", "args": [] }
"languageServers.servers.cpp":        { "command": "clangd", "args": [] }
"languageServers.servers.python":     { "command": "pyright-langserver", "args": ["--stdio"] }
"languageServers.servers.rust":       { "command": "rust-analyzer", "args": [] }
"languageServers.servers.java":       { "command": "jdtls", "args": [] }
"languageServers.servers.dotnet":     { "command": "csharp-ls", "args": [] }
"languageServers.servers.go":         { "command": "gopls", "args": [] }
"languageServers.servers.javascript": { "command": "typescript-language-server", "args": ["--stdio"] }
"languageServers.servers.ruby":       { "command": "solargraph", "args": ["stdio"] }
```

若 `polyls` 不在 `PATH` 中，可将 `command` 设为绝对路径，或在启动 `polyui` 前把 `build/` 加入 `PATH`。

## 3. 运行 IDE 并观察通信

1. 启动 `polyui`。
2. **文件 → 打开文件…** 选择一个 `.ploy` 源文件。桥层向 `polyls` 发送 `initialize` → `initialized` → `didOpen`。
3. 输入一个语法错误（如未闭合的字符串字面量）。约 200 ms 内会看到红色波浪线，底部 **LSP** 面板记录入站 `textDocument/publishDiagnostics`。
4. 保存（`Ctrl+S`），面板记录 `didSave`；关闭标签时面板记录 `didClose` 并附一条空的 `publishDiagnostics`。
5. 退出 IDE 时向每个活动会话发送 `shutdown` + `exit`。

## 4. 替换某个语言服务器

打开 **设置 → 语言服务器**，例如把 `languageServers.servers.python.command` 改为自建 Pyright 路径。重启对应标签（关闭再打开文件）以应用新配置。

## 5. `polyls` 暴露的能力

`polyls` 实现下表中的 LSP 3.17 子集；IDE 的 **LSP** 面板实时记录每条入站 / 出站消息。

| 能力                             | 说明                                                                  |
|----------------------------------|-----------------------------------------------------------------------|
| `textDocument/publishDiagnostics`| 实时诊断；payload 与 `polyc --check` 一致。                            |
| `textDocument/hover`             | 类型、文档注释摘要（来自 `///`）、源位置。                             |
| `textDocument/completion`        | 关键字 + 标识符 + 成员补全，由 `test_completion_ranker` 排序。         |
| `textDocument/signatureHelp`     | 带默认参数与泛型约束的函数签名。                                       |
| `textDocument/definition`        | 跨 `.ploy`、`LINK` 目标、宿主导入跳转到声明。                          |
| `textDocument/references`        | 工作区级引用。                                                         |
| `textDocument/documentSymbol`    | IDE 符号面板的文件大纲。                                               |
| `workspace/symbol`               | 工作区级符号搜索。                                                     |
| `textDocument/formatting`        | 由 `polyc` 内置 `.ploy` 格式化器驱动。                                 |

## 6. 在其它编辑器中驱动 `polyls`

`polyls` 是普通的 stdio LSP 服务器；任意客户端（VS Code、Neovim、Helix、Emacs `lsp-mode` 等）都能启动它并按 LSP 3.17 通信。完整契约见 [docs/api/polyls.md](../api/polyls.md)。抓取帧排查时：

```sh
polyls --log polyls.log
```

## 参见

- [docs/realization/polyls_server.md](../realization/polyls_server.md) — 服务器设计说明。
- [docs/USER_GUIDE_zh.md](../USER_GUIDE_zh.md) 第 12 章 — 完整 LSP 参考。
- [problems_panel_quickstart_zh.md](problems_panel_quickstart_zh.md) — 问题面板如何消费 `polyls` 诊断。
