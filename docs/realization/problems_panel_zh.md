# Problems 面板与实时诊断

状态：v1.21.0 起随 IDE 一起发布。

本文档描述 IDE 的 Problems 面板、其背后的诊断聚合器、工作区后台扫描器，
以及作为命令行回退的 `polyc --check`。它们共同实现"编辑即检查"：
用户在键入时即可看到错误、警告与提示，既呈现在编辑器边栏，
也集中显示在可停靠的 Problems 面板中。

## 组件

| 组件                  | 文件                                                                     | 线程模型             |
|-----------------------|--------------------------------------------------------------------------|----------------------|
| `ProblemsAggregator`  | `tools/ui/common/{include,src}/problems_aggregator.{h,cpp}`              | 不依赖 Qt，互斥锁    |
| `ProblemsPanel`       | `tools/ui/common/{include,src}/problems_panel.{h,cpp}`                   | Qt 主线程            |
| `WorkspaceScanner`    | `tools/ui/common/{include,src}/workspace_scanner.{h,cpp}`                | Qt + `QTimer` 节拍   |
| 状态栏计数器          | `MainWindow::SetupStatusBar`（富文本 `QLabel` + `linkActivated`）        | Qt 主线程            |
| `polyc --check` 命令  | `tools/polyc/src/driver.cpp`（pre-stage 处理器）                         | CLI                  |
| LSP 镜像              | `IdeLspBridge::PublishDiagnosticsToEditor`                               | Qt 主线程            |

## 数据模型

`ProblemsAggregator` 刻意**不依赖 Qt**，便于在不启动 `QApplication`
的前提下进行单元测试。它持有一份线程安全的映射：

```
std::map<file_path, std::map<source_label, std::vector<ProblemEntry>>>
```

`ProblemEntry` 中的位置是**1-based**（与
`compiler_service::DiagnosticInfo` 一致）。LSP 协议线上为 0-based，
转换在桥接边界完成。

### 来源标签

三个约定标签为不同提供方做命名空间：

| 标签               | 生产者                                       |
|--------------------|----------------------------------------------|
| `polyls:<lang>`    | LSP 服务器 `polyls`，由 `IdeLspBridge` 镜像。|
| `polyc`            | 前台进程内编译 / 分析。                      |
| `polyc-bg`         | `WorkspaceScanner` 后台批量扫描。            |

各生产方使用 `ClearSource(file, source)` 自行清理；
关闭标签页时调用 `ClearFile(file)`。

### 严重级别

`ClassifySeverity(label)` 把任意文本归一化到 `Severity` 枚举
（`kError | kWarning | kInfo | kHint`）。聚合器同时保留枚举与原始
标签；面板展示原始标签。`SeverityMask` 位图驱动过滤。

## 过滤器

`ProblemFilter` 提供：

* `severity`：位图（默认全选）
* `file_substring`：对**文件名**做大小写不敏感子串匹配
* `source_substring`：对来源标签做大小写不敏感子串匹配
* `message_pattern`：对消息做 ECMAScript 正则匹配；非法
  正则会静默退化为"全部命中"，不会向 UI 线程抛出异常。

每次 `AggregatorChanged` 触发后，面板都会重跑
`Snapshot(filter)`，按 `(file, line, column, severity)` 排序。

## 实时管线

1. 编辑器触发 `textChanged`；`IdeLspBridge` 抖动 200 ms 后向 `polyls`
   发送 `textDocument/didChange`。
2. `polyls` 返回 `textDocument/publishDiagnostics`；桥接将位置转为
   1-based，调用 `editor->SetDiagnostics(...)` 更新边栏，并以
   `polyls:<language_id>` 作为来源镜像入聚合器。
3. 面板收到 `AggregatorChanged`，刷新树视图，状态栏计数器
   （`E:N W:N H:N`）随之更新。

LSP 不可用时，进程内编译路径以 `polyc` 来源发布；
`MainWindow::ShowDiagnostics` 把所有诊断展示集中到一个包装函数中，
以保证镜像入口唯一。

## 工作区扫描器

`WorkspaceScanner` 通过 `QDirIterator` 遍历工作区根，跳过
`.git`、`build*`、`node_modules`、`target`、`.venv` 等目录。它以
**每节拍 50 文件、节拍 50 ms** 的节奏分批，避免阻塞 UI 线程。
当文件数超过 `kLargeWorkspaceFiles = 2000` 时，状态栏会显示
`Scanning N/M…` 进度提示。

`QFileSystemWatcher` 用于在文件或目录变化时使缓存失效；
为不超出操作系统句柄上限，最多监听 1024 个目录。

## 状态栏计数器

`MainWindow::SetupStatusBar` 添加一个绑定到 `AggregatorChanged`
的富文本 `QLabel`。该标签以 HTML `<a>` 渲染 `E:3 W:5 H:1`，
点击任一着色计数即触发 `QLabel::linkActivated`，
调用 `ShowPanel("problems")`，从而避免子类化 `QLabel`。

## `polyc --check`

在没有 LSP 的环境（CI、沙箱编辑器、外部 IDE 插件）作为回退使用：

```
polyc --check <file> [--lang=<id>]
```

处理器会调用 `FrontendRegistry::Get(language)->Analyze(...)`，并向
`stdout` 写出与 LSP `PublishDiagnosticsParams` 同形的单个 JSON：

```json
{
  "uri": "file:///abs/path/to/file.ploy",
  "diagnostics": [
    {
      "range": {
        "start": { "line": 12, "character": 4 },
        "end":   { "line": 12, "character": 18 }
      },
      "severity": 1,
      "code": "E1042",
      "source": "polyc",
      "message": "expected ';' before '}'"
    }
  ]
}
```

退出码：`0` 干净，`1` 至少存在一个 error，`2` 用法 / I/O 失败。
线上位置遵循 LSP 的 0-based 约定。

## 测试

* `tests/unit/polyui/problems_panel_model_test.cpp`：11 个 Catch2
  用例覆盖严重级别归一化、快照排序、按来源/文件清理、四类过滤器、
  `CountAll` 分桶、`KnownSources` 去重、变更回调语义，以及
  `ReplaceFromDiagnosticInfo` 适配器。`test_problems` 目标直接编译
  聚合器源文件，使套件保持 Qt-free，可在任意 CI 节点运行。

与边栏的集成一致性由 `MainWindow::ShowDiagnostics` 单点保证：
所有 `SetDiagnostics` 调用都经过它，再以 `polyc` 来源镜像入聚合器。

## 后续工作

* 从面板直接应用 quick-fix（`textDocument/codeAction`）。
* 按工作区持久化的静音清单。
* 跨语言、按符号重新分组（目前仅按文件）。
