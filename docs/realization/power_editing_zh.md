# 多光标 / 折叠 / 格式化 / Snippets / EditorConfig（demand 2026-04-28-26）

## 目标

把高级编辑能力补齐到 VS Code 水平：

* **多光标 + 列选区**：`Alt+Click`、`Ctrl+Alt+↑/↓`、`Ctrl+D`、
  `Ctrl+Shift+L`、`Shift+Alt+拖拽`。
* **折叠**：基于括号平衡 + 多行 `/* … */` 注释 + `// region`/`// endregion`
  显式标记。
* **格式化**：`polyls` 实现 `textDocument/formatting`、`rangeFormatting`、
  `onTypeFormatting`（针对 `.ploy`）；外语种走各自 LSP。
* **Snippets**：VS Code 风格的 tabstop / 选项 / 变量；用户 JSON 与内置库。
* **EditorConfig**：精简 `.editorconfig` 解析（支持完整 glob），结果驱动
  格式化器与状态栏。

## 组件

| 关注点                | 文件                                                                                                 | 职责                                                                            |
|-----------------------|------------------------------------------------------------------------------------------------------|---------------------------------------------------------------------------------|
| 多光标模型            | [`tools/ui/common/editing/multi_cursor.{h,cpp}`](../../tools/ui/common/editing/multi_cursor.h)       | Add/below/above、`SelectNextOccurrence`、列选区。                              |
| 折叠                  | [`tools/ui/common/editing/folding_model.{h,cpp}`](../../tools/ui/common/editing/folding_model.h)     | 括号 + 注释 + region 扫描。                                                    |
| 格式化器              | [`tools/ui/common/editing/format_engine.{h,cpp}`](../../tools/ui/common/editing/format_engine.h)     | `FormatPloy` 按括号嵌套重排缩进。                                              |
| Snippets              | [`tools/ui/common/editing/snippet_engine.{h,cpp}`](../../tools/ui/common/editing/snippet_engine.h)   | `ExpandSnippet`、`SnippetLibrary::LoadJson` / `Match`。                        |
| EditorConfig          | [`tools/ui/common/editing/editor_config.{h,cpp}`](../../tools/ui/common/editing/editor_config.h)     | 章节解析器 + 递归 glob 匹配器。                                                |
| LSP — formatting      | `polyls.HandleFormatting` / `HandleRangeFormatting` / `HandleOnTypeFormatting`                       | 包装 `FormatPloy`，返回 LSP `TextEdit[]`。                                     |

## 主要流水线

### 多光标

```
Alt+Click       → MultiCursor.AddCursorAt
Ctrl+Alt+↓      → MultiCursor.AddCursorBelow
Ctrl+D          → MultiCursor.SelectNextOccurrence
Ctrl+Shift+L    → MultiCursor.SelectAllOccurrences
Shift+Alt+拖拽  → MultiCursor.MakeColumnSelection
        │
        ▼
每个光标独立应用下一次文本变更
```

### Format on save / on paste / on type

```
Ctrl+S / Ctrl+V / 输入 `\n`
        │
        ▼
客户端发送 textDocument/{formatting,rangeFormatting,onTypeFormatting}
        │
        ▼
polyls.HandleFormatting → FormatPloy(text, opts) → TextEdit[]
        │
        ▼
客户端应用整个缓冲区范围的单个 TextEdit
```

### Snippet 展开

```
键入 "fn" → SnippetLibrary.Match("fn") → SnippetEntry.body
                                                │
                                                ▼
                                       ExpandSnippet(body, vars)
                                                │
                                                ▼
                                         SnippetExpansion {
                                           text, tabstops
                                         }
```

### EditorConfig 解析

```
打开文件 → 沿父目录收集 `.editorconfig`，遇到 `root = true` 或文件系统根停下
        │
        ▼
EditorConfigDocument.ResolveFor(relative_path)
        │                       后出现的章节覆盖先前的设置
        ▼
状态栏："space 4 · EOL=lf · utf-8 · final-LF"
```

## 测试

* [`tests/unit/polyui/multi_cursor_test.cpp`](../../tests/unit/polyui/multi_cursor_test.cpp)
* [`tests/unit/polyui/folding_model_test.cpp`](../../tests/unit/polyui/folding_model_test.cpp)
* [`tests/unit/polyui/format_engine_test.cpp`](../../tests/unit/polyui/format_engine_test.cpp)
* [`tests/unit/polyui/snippet_engine_test.cpp`](../../tests/unit/polyui/snippet_engine_test.cpp)
* [`tests/unit/polyui/editor_config_test.cpp`](../../tests/unit/polyui/editor_config_test.cpp)
* [`tests/unit/polyls/formatting_test.cpp`](../../tests/unit/polyls/formatting_test.cpp)
