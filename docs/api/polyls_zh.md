# `polyls` —— Polyglot 语言服务器

> 英文版本：[`polyls.md`](./polyls.md)，需求条目：`2026-04-28-19`，版本：1.20.0。

`polyls` 是 PolyglotCompiler 自带的 LSP 服务器，把内置前端通过标准
Language Server Protocol 暴露给任意编辑器。本文档约定可执行契约
（进程生命周期、传输、能力、错误语义），便于 polyui 之外的客户端
直接对接。

## 调用方式

```
polyls
```

服务器从 `stdin` 读取 JSON-RPC 帧，向 `stdout` 写入帧。1.20.0 不接受
任何命令行参数。Windows 下，`stdin` / `stdout` 在内部被切换为二进制
模式，避免 `\r\n` 转换破坏帧结构。

进程退出码：

| 退出码 | 含义 |
|--------|------|
| `0` | 正常关闭 —— 在 `exit` 之前收到了 `shutdown`。 |
| `1` | 未先 `shutdown` 便收到 `exit`（遵循 LSP 规范）。 |

## 线协议

严格 JSON-RPC 2.0 + LSP 分帧（`Content-Length: <n>\r\n\r\n<payload>`）。
所有线上字段均为 camelCase。

## 生命周期

1. **`initialize`**（请求）：必须是首条消息。服务器返回下文列出的
   `ServerCapabilities`。任何在初始化未完成前到达的非 `initialize`
   请求都会被拒绝，错误码 `-32002`（`ServerNotInitialized`）。
2. **`initialized`**（通知）：客户端宣布就绪。
3. **`textDocument/didOpen`** / **`didChange`** / **`didClose`** /
   **`didSave`**：文档存储增删。每次 `didOpen` / `didChange` 同步触发
   一次 `PloyLanguageFrontend::Analyze`，结果以
   **`textDocument/publishDiagnostics`** 推送。
4. **`shutdown`**（请求）：服务器清理状态后回复。此后任何请求都将
   收到 `InvalidRequest`（`-32600`）。
5. **`exit`**（通知）：终止驱动循环。

## 1.20.0 已声明能力

```json
{
  "textDocumentSync": 1,
  "diagnosticProvider": true,
  "hoverProvider": false,
  "completionProvider": false,
  "signatureHelpProvider": false,
  "definitionProvider": false,
  "referencesProvider": false,
  "documentSymbolProvider": false,
  "renameProvider": false,
  "codeActionProvider": false
}
```

`textDocumentSync = 1` 表示**全量**同步：客户端在每次 `didChange` 都需
发送整篇文档。增量同步、Hover、补全、跳转定义等能力推迟至
2026-04-28-21..23。

`InitializeResult.serverInfo` 固定为 `{ "name": "polyls", "version": "1.20.0" }`。

## 诊断

每一次成功的 `didOpen` / `didChange` 都会对该 URI 发出**恰好一条**
`publishDiagnostics`。`didClose` 触发一条 `diagnostics` 为空的
`publishDiagnostics`，便于客户端清空 gutter 标记。

每条 `Diagnostic` 字段：

* `range`：由前端 1-based `core::SourceLoc` 转换为 0-based。
* `severity`：`1`（Error）/ `2`（Warning）/ `3`（Information），来自
  `frontends::DiagnosticSeverity::{kError, kWarning, kNote}`。
* `code`：前端错误码加前缀 `E`，例如 `"E1001"`。
* `source`：恒为 `"polyls"`。
* `message`：前端的可读消息。

## URI 处理

`polyls` 接受 `file://` URI。百分号编码会被解码；Windows 下盘符前的
首个斜杠（`/C:/…`）会被剥离后再交给前端。

## 错误码

| 错误码 | 名称 | 触发 |
|--------|------|------|
| `-32700` | `ParseError` | 客户端发送了非法 JSON |
| `-32600` | `InvalidRequest` | `shutdown` 之后再次调用方法 |
| `-32601` | `MethodNotFound` | 未实现的方法 |
| `-32602` | `InvalidParams` | 参数缺失或类型不符 |
| `-32002` | `ServerNotInitialized` | 初始化前的非 `initialize` 请求 |

## 无 IDE 调用示例

```sh
# 手动构造一条 initialize 请求并送入 polyls：
{
  printf 'Content-Length: 73\r\n\r\n'
  printf '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}}'
} | polyls
```

服务器随即在 stdout 输出一帧 JSON-RPC 响应。

## 参考

* 体系结构：[`docs/specs/lsp_integration_zh.md`](../specs/lsp_integration_zh.md)
* IDE 接入演示：`docs/USER_GUIDE_zh.md` → "语言服务器架构"
