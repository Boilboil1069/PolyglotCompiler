# 工作区符号索引（导航功能）

`polyls` 通过 `tools/polyls/polyls_core/symbol_index.{h,cpp}` 中实现的
工作区级符号索引应答五个 LSP 导航请求
（`textDocument/definition`、`declaration`、`implementation`、
`typeDefinition`、`references`）。索引在每次 `didOpen` /
`didChange` / `didSave` 通知到达时增量重建，并持久化到
`.polyc-cache/symbol_index.json`，使服务器冷启动无需重新解析整个工作区即可
应答查询。

## 设计目标

* 击键级响应：索引采用无正则、单趟扫描的解析路径，仅识别跨语言
  `LINK` / `IMPORT` / `EXPORT` 词汇，避免运行完整解析器。
* 支持 `.ploy` ↔ 宿主语言双向跳转，覆盖 `cpp`、`python`、`rust`、
  `java`、`dotnet`。
* 自描述的 JSON 缓存，保证编辑器重启即可立即获得已就绪的工作区。

## 索引实体

| 来源             | 捕获的实体                                                |
|------------------|-----------------------------------------------------------|
| `.ploy`          | `FUNC`、`PIPELINE`、`STRUCT`、`LET`/`VAR` 绑定            |
| `.ploy`          | `IMPORT lang::module` 与 `IMPORT lang PACKAGE pkg`        |
| `.ploy`          | `LINK target_lang::… AS …` 及元组形式 `LINK(target,…)`    |
| `.ploy`          | `EXPORT name AS lang::func`                               |
| C++              | 命名空间限定的 class/struct 与自由函数                    |
| Python           | `def name`、`class Name`                                  |
| Rust             | `fn`、`struct`、`enum`、`trait`、`impl`（含 `pub`）       |
| Java / .NET      | `class`、`interface`、`enum`、`record`                    |

每个条目都保存一个 `SymbolLocation`（LSP 风格的 `(uri, line,
character, end_line, end_character)`）、`kind`、裸名与全限定名，对于
`LINK` 条目还保存跨语言导航需要的宿主语言目标描述。

## 引用收集

完成各语言扫描后，索引会再走一遍每行文本，把任何匹配已知条目名的
标识符记录为 `ReferenceSite`。定义站点会标记 `is_definition=true`，使
`textDocument/references` 能够依据请求中的 `context.includeDeclaration`
决定是否包含。同时排除一组关键字集合（FUNC、PIPELINE、STRUCT、LET 等），
避免语言关键字被错误地计入引用。

## 跨语言 LINK 解析

`SymbolIndex::CrossLanguageTarget(lang, qualified)` 会在宿主语言侧
按全限定名或裸名匹配 `kForeignFunction` / `kForeignClass` 条目，因此
即便 C++ 文件没有命名空间，`LINK cpp::image_processor::enhance` 仍能
找到顶层的 `void enhance(...)`。

`CrossLanguageBackrefs(lang, qualified)` 是其反向：给定宿主语言符号，
返回所有 `link_target_language` 与 `link_target_qualified` 匹配的 `.ploy`
`LINK` 站点。这正是宿主文件中发起 `references` 时，响应里同时包含每个
引用它的 `.ploy` LINK 位置的根据。

## 服务端联动

* `initialize` 记录 `rootUri`，派生 `<root>/.polyc-cache` 缓存路径，
  调用 `SymbolIndex::LoadFromCache()`，并广告五项新能力
  （`definitionProvider`、`declarationProvider`、
  `implementationProvider`、`typeDefinitionProvider`、`referencesProvider`）。
* `didOpen` / `didChange` / `didSave` 对受影响的文档重新建索引并尽力
  调用 `SaveToCache()`。
* `didClose` 把文档从内存索引中移除，但保留磁盘快照，使历史引用在
  编辑器关闭后仍能被查询。
* `shutdown` 在关闭前再写一次快照。

## 编辑器快捷键

`tools/ui/common/src/code_editor.cpp` 中的 Qt 编辑器把 LSP 端点绑定到
标准导航快捷键：

| 快捷键        | LSP 端点                            |
|---------------|-------------------------------------|
| `F12`         | `textDocument/definition`           |
| `Shift+F12`   | `textDocument/references`           |
| `Ctrl+F12`    | `textDocument/implementation`       |
| `Ctrl+K F12`  | 内联 Peek 视图（定义）              |
| `Ctrl+Click`  | `textDocument/definition`           |

## 缓存格式

`<workspace>/.polyc-cache/symbol_index.json`：

```json
{
  "version": 1,
  "generator": "polyls.symbol_index",
  "documents": [
    {
      "uri": "file:///path/main.ploy",
      "entries": [ { "name": "compute", "kind": "function", … } ],
      "references": [ { "name": "compute", "isDefinition": true, … } ]
    }
  ]
}
```

`version` 字段保证向前兼容：版本不匹配时静默丢弃缓存，下一次启动从内存
中的文档重新建索引。

## 测试

* `tests/unit/polyls/symbol_index_test.cpp` — 持久化往返、增量重建、
  跨语言目标/反向查询。
* `tests/unit/polyls/navigation_test.cpp` — 通过 JSON-RPC 接口直接驱动
  五个 LSP 处理器。
* `tests/integration/lsp_navigation_e2e_test.cpp` — 使用
  `09_mixed_pipeline` 样例完成完整的客户端 ↔ 服务器往返，验证正向
  （.ploy → C++）与反向（C++ → .ploy）跳转。
