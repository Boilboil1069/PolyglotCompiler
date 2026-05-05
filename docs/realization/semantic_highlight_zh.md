# 语义级语法高亮（demand 2026-04-28-24）

## 目标

将 `tools/ui/common/src/syntax_highlighter.cpp` 中基于正则的高亮替换为
真正的语义流水线：

1. 用 tree-sitter 形态的运行时解析每个编辑器缓冲区。
2. 折叠区间、文档大纲、Smart Select 共享同一棵解析树，避免编辑器
   不同子系统之间结果不一致。
3. 通过 LSP 3.16 语义 token 接口暴露结果，任何遵循该协议的第三方
   LSP 客户端均可正确着色。

旧的正则版高亮器作为离线场景与未注册语言的 fallback 保留。

## 组件

| 层次          | 文件                                                                                                | 职责                                                                            |
|---------------|-----------------------------------------------------------------------------------------------------|---------------------------------------------------------------------------------|
| Grammar 表    | [`tools/polyls/grammar/grammar_descriptor.{h,cpp}`](../../tools/polyls/grammar/grammar_descriptor.h) | 声明关键字 / 类型 / 内建函数集合，固化共享的语义 token legend。                  |
| 运行时        | [`tools/ui/common/syntax/tree_sitter_runtime.{h,cpp}`](../../tools/ui/common/syntax/tree_sitter_runtime.h) | 纯 C++ 解析器，提供 `Parse`/`Edit`/`Tokens`/`Folds`/`Outline`/`SmartSelect`。  |
| LSP handler   | [`tools/polyls/polyls_core/polyls_semantic.cpp`](../../tools/polyls/polyls_core/polyls_semantic.cpp) | 实现 `textDocument/semanticTokens/full` 与 `/range`。                          |
| 编辑器消费端  | [`tools/ui/common/syntax/semantic_tokens_client.{h,cpp}`](../../tools/ui/common/syntax/semantic_tokens_client.h) | 解码 wire payload 并将 `QTextCharFormat` 应用到 QTextDocument。                |

## 流水线

```
编辑器缓冲区 (didOpen / didChange)
        │
        ▼
polyls_server  ─►  PolylsServer::HandleSemanticTokensFull
        │                       │
        ▼                       ▼
   document text     polyls::ts::Parse(language, source)
                              │
                              ▼
                      Tree::Tokens()      ◄── grammar descriptor
                              │
                              ▼
                  EncodeSemanticTokens()
                              │
                              ▼
                JSON-RPC `SemanticTokens.data`
                              │
                              ▼
              SemanticTokensClient::Apply()
                              │
                              ▼
                   编辑器渲染着色后的 token
```

## Token legend

Legend 在整个代码库内保持一致（见
`grammar_descriptor.h::kTokenTypes`）：

| 索引 | 类型      | 默认颜色（Dark 主题）        |
|-----:|-----------|------------------------------|
| 0    | namespace | `editor_text`                |
| 1    | type      | `#4ec9b0`                    |
| 2    | struct    | `#4ec9b0`                    |
| 3    | function  | `#dcdcaa`                    |
| 4    | variable  | `#9cdcfe`                    |
| 5    | parameter | `#9cdcfe`                    |
| 6    | keyword   | `#569cd6`（粗体）            |
| 7    | comment   | `#6a9955`（斜体）            |
| 8    | string    | `#ce9178`                    |
| 9    | number    | `#b5cea8`                    |
| 10   | operator  | `editor_text`                |

修饰符：`declaration`、`readonly`、`static`、`deprecated`、
`definition`。Ploy 中的 `LINK` / `IMPORT` / `EXPORT` 等指令关键字会
带上 `definition` 修饰，主题可以为其单独配色。

## Wire 格式

`textDocument/semanticTokens/full` 返回：

```json
{ "data": [delta_line, delta_char, length, type_index, modifier_mask, …] }
```

运行时保证按文档顺序输出 token，因此 `DecodeSemanticTokens()` 反解
后能恢复原始绝对坐标。`textDocument/semanticTokens/range` 复用同一
encoder，先按行号过滤后再编码。

## 编辑器集成

* 每个编辑器实例持有一个 `SemanticTokensClient`，记住 `initialize`
  阶段服务端通告的 legend。
* 开关：`editor/useLspSemanticTokens`，默认 `true`。关闭后正则版
  高亮器为唯一着色源。
* `SemanticTokensClient::Apply()` 会把按 kind 聚合的
  `QTextCharFormat` 重新发布到现有 `SyntaxHighlighter`，从而保证
  正则流水线与 LSP 流水线在用户切换开关时使用同一调色板。

## tree-sitter grammar 源

运行时与上游 tree-sitter API 二进制兼容。当 `<repo>/.cache/deps/`
中存在 `tree-sitter-<lang>.{a,so}` 预编译产物时，可通过
`-DPOLYGLOT_USE_LIBTREE_SITTER=ON` 切换到真正的 tree-sitter，
调用点不需要改动。默认仍使用内置 C++ 实现，因为它随源码一同发布
且不依赖网络。

## 测试

* [`tests/unit/polyls/tree_sitter_runtime_test.cpp`](../../tests/unit/polyls/tree_sitter_runtime_test.cpp)
  — grammar 表、解析器、折叠、大纲、smart-select、编解码可逆性。
* [`tests/unit/polyls/semantic_tokens_test.cpp`](../../tests/unit/polyls/semantic_tokens_test.cpp)
  — full / range 的 LSP 边界契约。
* [`tests/integration/lsp_semantic_tokens_e2e_test.cpp`](../../tests/integration/lsp_semantic_tokens_e2e_test.cpp)
  — 走 loopback transport 的端到端往返。
