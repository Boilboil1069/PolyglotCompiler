# Tasks, Run/Debug picker, and Hot Reload

## Goal

Bring VS-Code-grade build / test / lint / format orchestration to
PolyUI, fuse it with the launch-config picker delivered alongside
the DAP integration, and route file-save events through a
language-aware Hot Reload engine that can swap symbols inside a
running `.ploy` / Python / C++ / Rust / Java / .NET process.

## Components

| Concern               | File                                                                                                       | Responsibility                                                              |
|-----------------------|------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------|
| Task value model      | [`tools/ui/common/tasks/task_config.{h,cpp}`](../../tools/ui/common/tasks/task_config.h)                   | `TaskDefinition`, `ParseTasksJson`, `DefaultTasks`.                         |
| Output → diagnostics  | [`tools/ui/common/tasks/problem_matcher.{h,cpp}`](../../tools/ui/common/tasks/problem_matcher.h)           | `$gcc` / `$clang` / `$msbuild` / `$tsc` / `$rustc` matchers + watch markers. |
| DAG scheduling        | [`tools/ui/common/tasks/task_runner.{h,cpp}`](../../tools/ui/common/tasks/task_runner.h)                   | Topological batched plan; status table; per-task output channel.            |
| Run/Debug picker      | [`tools/ui/common/runtime/run_debug_picker.{h,cpp}`](../../tools/ui/common/runtime/run_debug_picker.h)     | Status-bar quick-pick fusing tasks + launches.                              |
| Hot Reload dispatcher | [`tools/ui/common/runtime/hot_reload.{h,cpp}`](../../tools/ui/common/runtime/hot_reload.h)                 | Per-language handlers, request coalescing, queued re-runs.                  |

## Pipelines

### Run a compound task

```
.polyc/tasks.json
        │  ParseTasksJson
        ▼
TaskRunner.Register × N
        │  Plan("pipeline: lint+build+test")
        ▼
[ {clang-tidy} , {polyc: build} , {ctest} ]   # one batch per sequenced task
        │  shell driver invokes the IDE
        ▼
output_panel subchannel "task:<label>"  ←  problem_matcher.Match → Diagnostic
```

### Status-bar Run / Debug

```
RunDebugPicker.SetTasks(tasks) + SetLaunches(launches)
        │
        ▼
Items()  ⇒  default-group first, alphabetised
        │
        ▼
Select(label) → Active() → IDE wires Cmd-R / Cmd-Shift-R
```

### Hot Reload on save

```
Editor save → DetectLanguage(file) → ReloadRequest{language, file, session_id}
        │
        ▼
HotReloadEngine.Notify
   ├── handler available?  →  ReloadResult{status, replaced_symbols}
   ├── reload in flight?   →  enqueue → DrainPending after current finishes
   └── unknown language    →  ReloadStatus::kUnsupported
```

## Tests

* [`tests/unit/polyui/task_config_test.cpp`](../../tests/unit/polyui/task_config_test.cpp)
* [`tests/unit/polyui/problem_matcher_test.cpp`](../../tests/unit/polyui/problem_matcher_test.cpp)
* [`tests/unit/polyui/task_runner_test.cpp`](../../tests/unit/polyui/task_runner_test.cpp)
* [`tests/unit/polyui/run_debug_picker_test.cpp`](../../tests/unit/polyui/run_debug_picker_test.cpp)
* [`tests/unit/polyui/hot_reload_test.cpp`](../../tests/unit/polyui/hot_reload_test.cpp)
