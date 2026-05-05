# LSP 集成架构

> 需求条目：`2026-04-28-19`，版本：1.20.0，英文版本：[`lsp_integration.md`](./lsp_integration.md)。

PolyglotCompiler 在 polyui IDE 中内置了 LSP 客户端框架以及自研语言服务器
**polyls**，把 polyglot 各前端（首先是 `.ploy`）通过标准 LSP 线协议暴露给
任意编辑器。本文从体系结构、线协议保证、公共类型与配置面四个角度做完整说明。

## 1. 组件总览

```
┌──────────────────────────── polyui (Qt) ─────────────────────────────┐
│  CodeEditor ─┐                                                       │
│              │ contentsChanged                                       │
│              ▼                                                       │
│   IdeLspBridge ──► QTimer (debounce 200ms) ──► LspClient.DidChange   │
│        │                                          │                  │
│        │ TrackEditor / Untrack / NotifySaved      │ JSON-RPC 帧      │
│        ▼                                          ▼                  │
│   LspSessionRegistry            ┌─── StdioTransport (QProcess) ───┐  │
│        │                        │                                 │  │
│        ▼                        ▼                                 │  │
│   LspCapabilityRegistry      polyls / clangd / pyright / …        │  │
│                                                                   │  │
│   LspLogPanel  ◄── SetLogHandler(direction, payload)              │  │
└──────────────────────────────────────────────────────────────────────┘
```

* **`tools/ui/common/lsp/`** —— 不依赖 Qt 的 LSP 核心，构建为 `lsp_lib`
  静态库，被 polyui 与单元测试共同使用。
  * `lsp_message.{h,cpp}`：JSON-RPC 信封与 LSP 消息类型（Position、Range、
    Diagnostic、Location、Hover、CompletionItem、SignatureHelp、
    SymbolInformation、CodeAction、TextEdit、WorkspaceEdit、
    DocumentSymbol、生命周期与同步信封），以及 `EncodeFrame` /
    `TryDecodeFrame`。
  * `lsp_client.{h,cpp}`：`ILspTransport` 抽象、`LoopbackTransport`
    （进程内对端，用于单元测试）以及 `LspClient`（请求/响应配对、通知
    分发、日志钩子）。
  * `lsp_session.{h,cpp}`：以 `(workspace_uri, language_id)` 为键的
    `LspSessionRegistry`。
  * `lsp_capability_registry.{h,cpp}`：线程安全的服务器能力缓存，按成员
    指针查询。
  * `lsp_log_panel.{h,cpp}`：Qt 面板，带方向 / 类型 / 方法名过滤。
* **`tools/ui/common/{include,src}/lsp_bridge.{h,cpp}`** —— Qt 胶水：
  通过 `QProcess` 启动服务器、把编辑事件汇入防抖 `didChange` 管线、把
  `publishDiagnostics` 转发到 `CodeEditor::SetDiagnostics`。
* **`tools/polyls/`** —— 无头语言服务器。
  * `polyls_core/polyls_server.{h,cpp}`：协议级派发器。
  * `polyls.cpp`：stdio 驱动（Windows 下显式打开二进制模式）。

## 2. 线协议

严格遵循 JSON-RPC 2.0，外加 LSP 的 `Content-Length: <bytes>\r\n\r\n`
分帧。线上字段全部 camelCase；C++ 侧使用 snake_case，由
`lsp_message.h` 中的自由函数 `ToJson` / `FromJson` 完成映射，避免依赖
任何宏生成的序列化器。

`TryDecodeFrame()` 对部分缓冲安全：当尚未收齐 `Content-Length` 字节时
返回 `false` 并保持缓冲不变。这一性质同时支持同步的 LoopbackTransport
与流式的 `QProcess` 传输。

## 3. 1.20.0 中已声明的能力

按需求条目要求，本版本只暴露最小可用面：

| 能力                        | 取值      |
|-----------------------------|-----------|
| `textDocumentSync`          | `1`（全量）|
| `diagnosticProvider`        | `true`    |
| `hoverProvider`             | `false`   |
| `completionProvider`        | `false`   |
| `signatureHelpProvider`     | `false`   |
| `definitionProvider`        | `false`   |
| `referencesProvider`        | `false`   |
| `documentSymbolProvider`    | `false`   |
| `renameProvider`            | `false`   |
| `codeActionProvider`        | `false`   |

Hover / completion / definition / references / rename / code-action 留待
2026-04-28-21..23 三条完成。

## 4. 编辑器接入

`MainWindow` 在 `SetupDockWidgets()` 内构造唯一的 `IdeLspBridge`。每当
`OpenFileInTab(path)` 成功结束，最后一步即调用
`lsp_bridge_->TrackEditor(editor, language)`：

1. 从 `SettingsService` 读取 `languageServers.servers.<language>`；若
   缺失或 `languageServers.enabled = false`，整体跳过。
2. 通过 `QStandardPaths::findExecutable` 解析 `command`；若不在
   `PATH`，桥接器会发出非阻塞的状态栏提示而非抛出异常 —— 与需求 §4
   完全一致。
3. 用 `StdioTransport`（基于 `QProcess` 的 `ILspTransport`）启动
   服务器，构造 `LspClient`，依次发送 `initialize` → `initialized`，
   随后立即对当前编辑内容发出版本号 `1` 的 `didOpen`。
4. 把 `editor->document()->contentsChanged` 接到 `QTimer`
   （`languageServers.changeDebounceMs`，默认 200 ms）。每次超时发送
   一条 `didChange`，包含全文及单调递增的版本号。

`Save()` / `CloseTab()` 经由 `NotifySaved()` / `Untrack()` 转换为
`didSave` / `didClose`。`~MainWindow()` 调用 `Shutdown()`，向所有已
初始化会话发送 `shutdown` + `exit`。

## 5. 设置项

JSON 键（默认值通过 QRC 资源打包）：

| 键                                                       | 类型     | 默认值 | 说明 |
|----------------------------------------------------------|----------|--------|------|
| `languageServers.enabled`                                | bool     | `true` | 总开关 |
| `languageServers.changeDebounceMs`                       | integer  | `200`  | `didChange` 防抖毫秒 |
| `languageServers.logCapacity`                            | integer  | `2000` | 日志面板环形缓冲容量 |
| `languageServers.servers.<lang>.command`                 | string   | 见下   | 可执行文件 |
| `languageServers.servers.<lang>.args`                    | string[] | 见下   | 启动参数 |
| `languageServers.servers.<lang>.env`                     | object   | `{}`   | 附加环境变量 |
| `languageServers.servers.<lang>.initializationOptions`   | object   | `{}`   | 透传给 `initialize` |

默认配置：`.ploy` → `polyls`，`cpp` → `clangd`，
`python` → `pyright-langserver --stdio`，`rust` → `rust-analyzer`，
`java` → `jdtls`，`csharp` → `omnisharp -lsp`。

## 6. 测试

* `tests/unit/polyui/lsp_client_test.cpp`（`test_lsp`）：分帧往返、部分
  缓冲、JSON-RPC 信封、`LoopbackTransport` 投递、请求/响应配对、通知
  分发、强类型 `publishDiagnostics`、日志钩子、能力注册表、会话注册表
  幂等性，共 9 例。
* `tests/unit/polyls/lifecycle_test.cpp`（`test_polyls`）：未初始化
  请求被拒（`-32002`）、已声明能力形状、文档存储增删、语法错误触发
  `publishDiagnostics`、shutdown→exit，共 5 例。
* `tests/integration/lsp_diagnostics_e2e_test.cpp`：用真实
  `LspClient` 与进程内 `PolylsServer` 通过 LoopbackTransport 对端
  端到端联调，打开包含语法错误的 `.ploy`，断言收到的诊断
  `source = "polyls"` 且为错误级；随后验证关闭时清空诊断与
  shutdown / exit 握手。

## 7. 参考

* LSP 3.17 规范：<https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/>
* JSON-RPC 2.0：<https://www.jsonrpc.org/specification>
* polyls API 文档：[`docs/api/polyls_zh.md`](../api/polyls_zh.md)
