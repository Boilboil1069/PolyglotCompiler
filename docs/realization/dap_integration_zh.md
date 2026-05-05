# DAP 集成与通用调试 UI（demand 2026-04-28-28）

## 目标

为 PolyUI 提供完整的 Debug Adapter Protocol 支持，使任何 DAP 适配器
（debugpy、lldb-vscode、codelldb、netcoredbg…）都能驱动 IDE 调试界面；同
时让自带的 `.ploy` 运行时调试器接入同一套 UI。

## 组件

| 关注点          | 文件                                                                                                 | 职责                                                                          |
|-----------------|------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------|
| 帧 + RPC        | [`tools/ui/common/dap/dap_client.{h,cpp}`](../../tools/ui/common/dap/dap_client.h)                   | `MessageFramer`（Content-Length）、`DapClient` 请求/事件路由。                |
| 启动配置        | [`tools/ui/common/dap/launch_config.{h,cpp}`](../../tools/ui/common/dap/launch_config.h)             | `ParseLaunchJson`、`${...}` 变量替换、`DefaultLaunchConfigurations`。        |
| 会话模型        | [`tools/ui/common/dap/debug_session.{h,cpp}`](../../tools/ui/common/dap/debug_session.h)             | 断点、线程、栈、Scope、Console、行内变量值。                                  |

## 覆盖的请求

`initialize`、`launch`、`attach`、`setBreakpoints`、
`setExceptionBreakpoints`、`setDataBreakpoints`、
`setFunctionBreakpoints`、`configurationDone`、`threads`、
`stackTrace`、`scopes`、`variables`、`evaluate`、`continue`、`next`、
`stepIn`、`stepOut`、`pause`、`disconnect`、`terminate`。

## 处理的事件

`stopped`、`continued`、`exited`、`terminated`、`output`、
`breakpoint`、`thread`、`initialized`。

## 主要流水线

### 启动会话

```
用户选择 LaunchConfig（来自 .polyc/launch.json 或 DefaultLaunchConfigurations）
           │
           ▼
Substitute(${workspaceFolder}/${file}/${env:NAME}/${command:NAME}/${fileBasename})
           │
           ▼
DebugSession.Initialize → 适配器返回 initialize → DapClient.SetExceptionBreakpoints …
           │
           ▼
DebugSession.Launch / Attach → 适配器发送 `initialized` 事件
           │
           ▼
SetBreakpoints（按文件）→ ConfigurationDone → 运行
```

### 命中断点

```
适配器事件 "stopped" {threadId, reason}
           │
           ▼
更新 DebugSession.last_stop_reason / stopped_thread
           │
           ▼
自动发起：threads → stackTrace → scopes
           │
           ▼
UI 面板（Threads / Call Stack / Variables / Watch）重绘
```

## 测试

* [`tests/unit/polyui/dap_client_test.cpp`](../../tests/unit/polyui/dap_client_test.cpp)
* [`tests/unit/polyui/launch_config_test.cpp`](../../tests/unit/polyui/launch_config_test.cpp)
* [`tests/unit/polyui/debug_session_test.cpp`](../../tests/unit/polyui/debug_session_test.cpp)
