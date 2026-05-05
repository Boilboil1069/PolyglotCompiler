# 补全 · 悬浮 · 签名帮助（IDE P0-3）

> v1.22.0 中实现的语言特性三件套说明，与
> `completion_hover_signature.md` 配套阅读。

## 1. 架构

```
┌──────────────┐   textDocument/           ┌──────────────┐
│  CodeEditor  │   completion / hover /    │   polyls     │
│  (Qt 部件)   │ ─signatureHelp──────────▶ │   服务器     │
│              │ ◀─JSON-RPC 回包────────── │ (无 Qt 依赖) │
└──────┬───────┘                           └──────────────┘
       │ IdeLspBridge::Request*
       │ （QPointer 守护回调；通过
       │   QMetaObject::invokeMethod
       │   切回 GUI 主线程）
       ▼
  CompletionPopup / HoverTooltip / SignatureHelpWidget
       │
       ▼
  SnippetExpander（解析 LSP `${1:name}` 模板并支持 Tab 跳转）
```

* 服务器端代码位于 `tools/polyls/polyls_core/polyls_features.cpp`，
  无 Qt 依赖，便于命令行驱动复用。
* Qt 端代码位于 `tools/ui/common/src/code_assist.cpp` 与
  `tools/ui/common/src/completion_ranker.cpp`。
* 桥接方法（`RequestCompletion` / `RequestHover` /
  `RequestSignatureHelp`）负责 JSON-RPC 流量并在调用回调前回到 Qt
  主线程。

## 2. 服务器端点

| 方法                            | 用途                                       |
| ------------------------------- | ------------------------------------------ |
| `textDocument/completion`       | 关键字、文档符号、`LINK <lang>::` 跨语言模板 |
| `completionItem/resolve`        | 原样回送（我们已将完整文本一次性下发）     |
| `textDocument/hover`            | Markdown 签名 + 文档                       |
| `textDocument/signatureHelp`    | 重载列表与当前参数下标                     |

`initialize` 中上报的能力位为
`completionProvider`、`hoverProvider` 与 `signatureHelpProvider`。

### LINK 跨语言触发

当光标位于 `LINK <lang>::`（或随后已键入的前缀）之后时，补全处理器
会发出一项 `Module`（`9`）类型的条目，其插入文本是片段模板
`<lang>::${1:module}`，按 Tab 即可填入模块名。本提交以最小代价完成
demand §1.1 的跨语言流程，模块全索引留待后续。

## 3. 匹配策略

`completion_ranker.{h,cpp}` 中的无 Qt 工具按以下三种策略对候选项打分，
通过设置项 `languageServers.completionMatchStrategy`（`prefix`、
`subsequence`、`fuzzy`）切换：

* **Prefix**：忽略大小写的前缀匹配；命中精确大小写、长度完全一致时
  额外加分。
* **Subsequence**：要求按序出现 needle 的每个字符。相邻命中、词边界
  （`_`、`:`、`.`、`-`、空格以及 CamelCase 拐点）、靠前位置都会加分；
  对长度差有较小惩罚。
* **Fuzzy**：优先走严格子序列；失败时允许 needle 中出现 1 个错字
  （扣 50 分）。仅当 needle ≥ 3 字符时启用，避免短串误匹配。

打分函数返回 `-1` 表示淘汰；否则数值越高越靠前。编辑器在弹出列表前
按降序稳定排序。

## 4. 片段展开器

`SnippetExpander` 解析 LSP 片段模板（`$0`、`$1`、`${1}`、
`${1:default}`、转义 `\$`），输出纯文本与一组 `SnippetStop` 区间。
跳转点按 tab 序号排序，`$0` 始终排在末尾（代表最终光标）。

* `ExpandAtCursor` 在编辑器当前光标处插入文本，将相对偏移提升为绝对
  文档坐标，并选中第一个跳转点。
* 按 **Tab** 进入下一个跳转点并选中其当前内容，便于直接覆写；走完最
  后一个跳转点后展开器自行取消，Tab 恢复默认（插入空格）行为。

## 5. 悬浮提示

`mouseMoveEvent` 记录当前全局坐标并启动单次 `QTimer`（450 ms）。计时
器触发时通过桥接发起 `textDocument/hover`，并将回包中的 Markdown
渲染到无边框的 `HoverTooltip` 中；同一路径也可通过 **Ctrl+K Ctrl+I**
手动触发。

## 6. 签名帮助

输入 `(` 时调用 `RequestSignatureHelp`。服务器在当前行回溯，统计未配
对的 `(` 找到调用现场，并通过顶层逗号数推算当前参数下标。控件渲染
所有重载并高亮当前参数，上下方向键可在重载之间切换。

## 7. 测试

* `tests/unit/polyls/completion_test.cpp`：关键字、用户 FUNC 与跨语言
  LINK 三种补全形态。
* `tests/unit/polyls/hover_test.cpp`：Markdown 内容与空白位置返回
  null。
* `tests/unit/polyls/signature_help_test.cpp`：`add(1, |)` 中的活动
  参数与非调用位置返回 null。
* `tests/unit/polyui/completion_ranker_test.cpp`：三种打分策略的不变
  式。

后续将增加 `integration/lsp_completion_e2e_test`，通过 QProcess 通道
做端到端验证。
