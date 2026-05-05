# 多标签 / 分屏 / Quick Open / 全局搜索 / Outline（demand 2026-04-28-25）

## 目标

把编辑器面交付到现代 IDE 的水平：

* **多标签 + 最多 4×4 分屏**，支持 pin、批量关闭、跨分组拖拽。
* **Quick Open（Ctrl+P）**：VS Code 风格的模糊文件名 ranker，含最近
  文件优先。
* **符号搜索**：`Ctrl+T`（跨工程）与 `Ctrl+Shift+O`（当前文件），由
  `polyls` 提供数据。
* **全局查找 / 替换**：正则 / 大小写 / 全词、glob include / exclude，
  支持捕获组替换，结果流式渲染。
* **Outline / Breadcrumbs / Minimap**：符号树与面包屑共用同一份数据。

旧命令面板（`Ctrl+Shift+P`）保留。

## 组件

| 关注点                | 文件                                                                                                | 职责                                                                            |
|-----------------------|-----------------------------------------------------------------------------------------------------|---------------------------------------------------------------------------------|
| 标签 / 分组模型       | [`tools/ui/common/editor/editor_group.{h,cpp}`](../../tools/ui/common/editor/editor_group.h)        | Pin、重排、关闭、关闭其他、关闭右侧。                                          |
| 分屏网格              | [`tools/ui/common/editor/editor_grid.{h,cpp}`](../../tools/ui/common/editor/editor_grid.h)          | 最多 4×4 个 `EditorGroup` 单元；split / merge / move-tab。                     |
| Quick Open ranker     | [`tools/ui/common/quickopen/quick_open_ranker.{h,cpp}`](../../tools/ui/common/quickopen/quick_open_ranker.h) | 模糊路径打分 + 最近文件 tie-break。                                            |
| 搜索引擎              | [`tools/ui/common/search/global_search_engine.{h,cpp}`](../../tools/ui/common/search/global_search_engine.h) | 正则 / glob / 流式 sink / 捕获组替换。                                         |
| Outline 树            | [`tools/ui/common/outline/outline_model.{h,cpp}`](../../tools/ui/common/outline/outline_model.h)    | 可过滤的符号树，供 Outline 面板与 Breadcrumbs 共享。                          |
| LSP — 文件符号        | `polyls.HandleDocumentSymbol`                                                                       | 为 `.ploy` 缓冲返回 LSP `DocumentSymbol[]`。                                   |
| LSP — 工程符号        | `polyls.HandleWorkspaceSymbol`                                                                      | 遍历 `SymbolIndex::Entries`，按子串过滤。                                      |

## 主要流水线

### Quick Open

```
按下 Ctrl+P
   │
   ▼
收集候选（工程文件 ∪ MRU 列表 + recency 时间戳）
   │
   ▼
RankQuickOpen(needle, candidates, limit)
   │   1. 逐项打分，剔除不匹配
   │   2. stable_sort: 分数降序 → recency 降序 → 路径升序
   ▼
渲染面板
```

### 全局搜索

```
GlobalSearchOptions { pattern, regex, whole_word, case_sensitive,
                      include_globs, exclude_globs, max_results }
   │
   ▼
遍历工程 ──► MatchesGlobFilter ──► 打开文件 ──► SearchInBuffer
                                                         │
                                                         ▼
                                                    GlobalSearchSink
                                                         │
                                                         ▼
                                                  结果面板（流式）
```

### Outline / Breadcrumbs

```
polyls textDocument/documentSymbol
   │
   ▼
OutlineModel.SetRoots( … )
   │                          ◄── Breadcrumbs(line, col) 返回光标所在的
   ▼                              root → leaf 链
Outline 面板（SetFilter 进行过滤）
```

## Pin 与批量关闭语义

Pinned 标签固定在标签条前部，对 `CloseOthers` / `CloseToRight` 免疫。
`TogglePin` 会把标签搬到 pinned / unpinned 边界，使两区内的相对顺序
保持稳定。分屏网格的标签移动只追加不插入，使键盘导航行为可预测。

## 与 `polyls` 的对接

服务端在 `initialize.result.capabilities` 中通告 `documentSymbolProvider`
与 `workspaceSymbolProvider`。编辑器握手阶段读取该 legend，仅当能力
存在时才把 `Ctrl+T` / `Ctrl+Shift+O` 走 LSP。

## 测试

* [`tests/unit/polyui/editor_group_test.cpp`](../../tests/unit/polyui/editor_group_test.cpp)
* [`tests/unit/polyui/quick_open_ranker_test.cpp`](../../tests/unit/polyui/quick_open_ranker_test.cpp)
* [`tests/unit/polyui/global_search_engine_test.cpp`](../../tests/unit/polyui/global_search_engine_test.cpp)
* [`tests/unit/polyui/outline_model_test.cpp`](../../tests/unit/polyui/outline_model_test.cpp)
* [`tests/unit/polyls/workspace_symbol_test.cpp`](../../tests/unit/polyls/workspace_symbol_test.cpp)
