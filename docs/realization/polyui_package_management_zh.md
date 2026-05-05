# 包管理、依赖图、漏洞扫描、REPL 与 Notebook（PolyUI）

## 目标

在 polyui 内统一十二种语言生态的包管理体验，使项目无需离开 IDE
即可安装/升级/卸载与审计依赖；并通过内嵌的 REPL 与 Notebook 让
跨语言项目在多个内核之间共享执行结果。

## 组件

| 组件 | 头文件 | 作用 |
| --- | --- | --- |
| `PackageManagerService` | [`tools/ui/common/packages/package_manager.h`](../../tools/ui/common/packages/package_manager.h) | 发现 + install/upgrade/remove + 锁文件读取 + 与 `.ploy CONFIG` 双向同步。子进程 I/O 通过注入的 `CommandExecutor` 完成。 |
| `PackageManagerRegistry` | 同上 | 持有十二个具体后端，可按 `Ecosystem` 或清单文件名查找。 |
| `PipBackend` / `CondaBackend` / `UvBackend` / `PipenvBackend` / `PoetryBackend` | 同上 | Python 生态：解析 `requirements.txt`、`environment.yml`、`uv.lock`、`Pipfile.lock`、`poetry.lock`。 |
| `CargoBackend` / `NpmBackend` / `MavenBackend` / `GradleBackend` / `NugetBackend` / `GemBackend` / `GoModBackend` | 同上 | Cargo、npm、Maven、Gradle、NuGet、Bundler、Go-Mod 解析。每个后端声明清单、锁文件以及 install/upgrade/remove 命令行。 |
| `DependencyGraph` | [`tools/ui/common/packages/dependency_graph.h`](../../tools/ui/common/packages/dependency_graph.h) | 节点+边模型、冲突检测、确定性 SVG 导出。`TreeView()` 由根节点投影出树视图。 |
| `VulnerabilityScanner` | [`tools/ui/common/packages/vulnerability_scanner.h`](../../tools/ui/common/packages/vulnerability_scanner.h) | 通过 `ParseOsvDocument` / `ParseGitHubAdvisory` 加载告警，使用 `VersionInRange` 匹配版本，支持按 id 抑制。 |
| `ReplSession` | [`tools/ui/common/notebook/repl_session.h`](../../tools/ui/common/notebook/repl_session.h) | 常驻引擎包装。`DefaultSpec` 提供 `.ploy`、Python、IRust、IRB、dotnet-script 的 argv/提示符/退出指令；真实 I/O 由可插拔 `ReplTransport` 实现。 |
| `Notebook` | [`tools/ui/common/notebook/notebook.h`](../../tools/ui/common/notebook/notebook.h) | 代码/Markdown/跨语言链接单元；`Execute` 将单元路由到对应会话；`ToJson`/`LoadJson` 与 `.polynb` 信封互转。 |

## 流程

* **发现与 CONFIG 同步。** `PackageManagerService::Discover` 遍历
  工作区与候选目录，按 `manifest_filename()` 匹配。生成的
  `Environment` 通过 `Activate` 在每个生态内独占激活；
  `SyncWithConfig` 比对解析锁文件与 `.ploy CONFIG` 的需求列表，
  返回双向漂移 `missing_in_lockfile` 与 `missing_in_config`，UI
  据此做高亮。
* **install / upgrade / remove。** 每个后端构造对应的 argv
  （`pip install foo==1.2.3`、`cargo add foo`、`npm install
  foo@1.2.3` 等），服务转交执行器并把 `CommandResult` 透传上层。
* **依赖图与冲突。** 锁文件读取后填充 `DependencyGraph`；
  `Conflicts()` 按名字归并节点，凡解析到多版本即被报告；
  `ExportSvg()` 自根节点 BFS 计算深度，按列布局，根蓝、冲突红。
* **告警匹配。** `VulnerabilityScanner::AddAdvisories` 接收来自
  osv.dev 或 GitHub Advisory GraphQL 的 `Advisory` 列表；`Scan`
  遍历每个 `Package`，按名字（与生态，若两侧都声明）匹配每个
  告警的影响版本范围；按 id 的抑制清单可静默已接受风险的发现。
* **REPL 求值。** `ReplSession::Eval` 记录每次的 input、stdout、
  stderr 与 error 标志。Transport 既可派生引擎子进程，也可对接
  进程内内核；IDE 只看到 `ReplTurn` 记录。
* **Notebook 执行。** `Notebook::Execute` 把代码单元派发给绑定
  `engine` 的会话；`kCrossLanguageLink` 单元先在源端会话求值
  source-side 表达式、再在目标端求值 target-side 表达式，并把双
  方输出拼接，完整保留一次 `LINK` 交换。

## 测试

* [`tests/unit/polyui/package_manager_test.cpp`](../../tests/unit/polyui/package_manager_test.cpp)
  覆盖注册表、各类解析样本（pip、cargo、npm、Maven、go-sum）、
  执行器路由与 `SyncWithConfig`。
* [`tests/unit/polyui/dependency_graph_test.cpp`](../../tests/unit/polyui/dependency_graph_test.cpp)
  覆盖树投影、冲突检测与 SVG 导出。
* [`tests/unit/polyui/vulnerability_scanner_test.cpp`](../../tests/unit/polyui/vulnerability_scanner_test.cpp)
  覆盖范围匹配、两种 feed 的解析、扫描结果与抑制。
* [`tests/unit/polyui/notebook_test.cpp`](../../tests/unit/polyui/notebook_test.cpp)
  覆盖 REPL 默认规格、转录捕获、单元移动/删除、跨语言链接执行
  与 `.polynb` 往返。

四个文件均在 polyui Catch2 二进制内运行；整套合计 128 例 545 条
polyui 断言全部通过。
