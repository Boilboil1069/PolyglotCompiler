# 任务系统 / Run-Debug 选择器 / Hot Reload

## 目标

为 PolyUI 提供 VS Code 级别的 build / test / lint / format 编排，与
DAP 集成同时交付的启动配置选择器融合，并通过语言感知的 Hot Reload
引擎将文件保存事件路由到 `.ploy` / Python / C++ / Rust / Java / .NET
运行进程，实现符号替换。

## 组件

| 关注点                 | 文件                                                                                                       | 职责                                                                          |
|------------------------|------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------|
| 任务模型               | [`tools/ui/common/tasks/task_config.{h,cpp}`](../../tools/ui/common/tasks/task_config.h)                   | `TaskDefinition`、`ParseTasksJson`、`DefaultTasks`。                          |
| 输出 → 诊断            | [`tools/ui/common/tasks/problem_matcher.{h,cpp}`](../../tools/ui/common/tasks/problem_matcher.h)           | `$gcc` / `$clang` / `$msbuild` / `$tsc` / `$rustc` 等匹配器 + watch 标记。     |
| DAG 调度               | [`tools/ui/common/tasks/task_runner.{h,cpp}`](../../tools/ui/common/tasks/task_runner.h)                   | 拓扑分批执行计划；状态表；每任务独立输出子频道。                              |
| Run/Debug 选择器       | [`tools/ui/common/runtime/run_debug_picker.{h,cpp}`](../../tools/ui/common/runtime/run_debug_picker.h)     | 状态栏快捷选择，合并 tasks 与 launches。                                       |
| Hot Reload 分发器      | [`tools/ui/common/runtime/hot_reload.{h,cpp}`](../../tools/ui/common/runtime/hot_reload.h)                 | 按语言注册处理器、请求合并、排队重跑。                                        |

## 流水线

### 运行复合任务

```
.polyc/tasks.json
        │  ParseTasksJson
        ▼
TaskRunner.Register × N
        │  Plan("pipeline: lint+build+test")
        ▼
[ {clang-tidy} , {polyc: build} , {ctest} ]   # 顺序任务被拆为单任务批次
        │  shell 驱动由 IDE 负责
        ▼
output_panel 子频道 "task:<label>"  ←  problem_matcher.Match → Diagnostic
```

### 状态栏 Run / Debug

```
RunDebugPicker.SetTasks(tasks) + SetLaunches(launches)
        │
        ▼
Items()  ⇒  默认组在前，按字母序
        │
        ▼
Select(label) → Active() → IDE 绑定 Cmd-R / Cmd-Shift-R
```

### 保存时 Hot Reload

```
编辑器保存 → DetectLanguage(file) → ReloadRequest{language, file, session_id}
        │
        ▼
HotReloadEngine.Notify
   ├── 已注册处理器？  →  ReloadResult{status, replaced_symbols}
   ├── 正在重载？      →  入队 → 当前完成后 DrainPending
   └── 未知语言        →  ReloadStatus::kUnsupported
```

## 测试

* [`tests/unit/polyui/task_config_test.cpp`](../../tests/unit/polyui/task_config_test.cpp)
* [`tests/unit/polyui/problem_matcher_test.cpp`](../../tests/unit/polyui/problem_matcher_test.cpp)
* [`tests/unit/polyui/task_runner_test.cpp`](../../tests/unit/polyui/task_runner_test.cpp)
* [`tests/unit/polyui/run_debug_picker_test.cpp`](../../tests/unit/polyui/run_debug_picker_test.cpp)
* [`tests/unit/polyui/hot_reload_test.cpp`](../../tests/unit/polyui/hot_reload_test.cpp)
