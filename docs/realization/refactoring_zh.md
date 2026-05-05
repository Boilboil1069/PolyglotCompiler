# 工作区重构（`polyls`）

`polyls` 在 v1.23.0 引入的工作区 `SymbolIndex` 之上，应答三类 LSP 重构请求：

* `textDocument/prepareRename`
* `textDocument/rename`
* `textDocument/codeAction`

实现位于
[`tools/polyls/polyls_core/refactor.{h,cpp}`](../../tools/polyls/polyls_core/refactor.h)
以及 LSP 接线 [`polyls_refactor.cpp`](../../tools/polyls/polyls_core/polyls_refactor.cpp)。

## 设计目标

* 每次重命名生成单一原子 `WorkspaceEdit`，编辑器单步 undo 即可整体回滚。
* 跨语言重命名：宿主语言端重命名同步更新所有引用该符号的 `.ploy` `LINK` /
  `EXPORT` 站点，反向亦然。
* 词法感知的文本改写：跳过字符串字面量与 `//` / `#` 注释中的标识符子串，
  确保不会破坏字符串负载。
* `codeAction` 暴露固定的重构菜单，编辑器灯泡始终展示同一组目录项。

## Rename 流水线

1. **词元解析**（`ResolveIdentifierAt`）——定位光标下的标识符，拒绝空白、
   标点与保留字（`FUNC`、`class`、`let`…）。
2. **校验**（`IsValidIdentifier`）——新名称必须匹配
   `[A-Za-z_][A-Za-z0-9_]*` 且不与保留字集合冲突。
3. **打开缓冲区改写**——逐行扫描每个打开的文档；不在字符串/注释中的命中
   作为 `TextEdit` 写入 `WorkspaceEdit::changes[uri]`。
4. **索引兜底**——对于索引已知但编辑器未打开的文件，使用
   `SymbolIndex::References` 记录的位置生成编辑项，由编辑器自行通过文件 IO
   施加。
5. **跨语言跳转**——当重命名从宿主语言文件发起时，额外调用
   `SymbolIndex::CrossLanguageBackrefs`，使相同的编辑同时改写所有 `.ploy`
   LINK 限定名。

## CodeAction 目录

| `kind`                          | 行为                                                   |
|---------------------------------|--------------------------------------------------------|
| `refactor.extract.function`     | 将选中行包装为新的 `FUNC` 并替换为函数调用。           |
| `refactor.inline.variable`      | 识别 `LET name = …;` 绑定并发出注释化编辑。             |
| `refactor.inline.function`      | 灯泡项；调用站点改写交由编辑器向导。                   |
| `refactor.changeSignature`      | 灯泡项；触发编辑器签名向导。                           |
| `refactor.move.file`            | 灯泡项；触发编辑器文件迁移向导。                       |

前两项携带完整的 `WorkspaceEdit`；其余三项为信息性条目，使编辑器能在不依赖
polyls 静态分析深度的前提下展示相同菜单。

## 编辑器联动

`tools/ui/common/src/code_editor.{h,cpp}` 暴露
`RenameRequested(symbol, line, column)` 信号并绑定标准 `F2` 快捷键；
`Ctrl+Shift+R` 组合则发起 extract-function `codeAction`。
`mainwindow.cpp` 弹出按文件分组的确认窗口——用户可在按 **Apply** 前逐项
勾选；整个 `WorkspaceEdit` 在编辑器文档上以单步 undo 的形式应用。

## 测试

* [tests/unit/polyls/refactor_test.cpp](../../tests/unit/polyls/refactor_test.cpp)
  ——标识符校验、prepareRename、单文件 rename、字符串隔离、跨语言 LINK
  传播、codeAction 目录。
* [tests/integration/lsp_refactor_e2e_test.cpp](../../tests/integration/lsp_refactor_e2e_test.cpp)
  ——在 `09_mixed_pipeline` 上完整 client ↔ server 往返：从
  `image_processor.cpp` 发起的重命名同时改写宿主文件与
  `mixed_pipeline.ploy`。
