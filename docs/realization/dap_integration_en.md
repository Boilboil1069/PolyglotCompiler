# DAP integration & generic debug UI (demand 2026-04-28-28)

## Goal

Bring full Debug Adapter Protocol support to PolyUI so any DAP-
speaking adapter (debugpy, lldb-vscode, codelldb, netcoredbg, …) can
drive the IDE's debug surface — and so the bundled `.poly` runtime
debugger can plug into the same UI.

## Components

| Concern              | File                                                                                                 | Responsibility                                                                  |
|----------------------|------------------------------------------------------------------------------------------------------|---------------------------------------------------------------------------------|
| Wire framing + RPC   | [`tools/ui/common/dap/dap_client.{h,cpp}`](../../tools/ui/common/dap/dap_client.h)                   | `MessageFramer` (Content-Length), `DapClient` request/event router.             |
| Launch configs       | [`tools/ui/common/dap/launch_config.{h,cpp}`](../../tools/ui/common/dap/launch_config.h)             | `ParseLaunchJson`, `${...}` substitution, `DefaultLaunchConfigurations`.        |
| Session model        | [`tools/ui/common/dap/debug_session.{h,cpp}`](../../tools/ui/common/dap/debug_session.h)             | Breakpoints, threads, stack, scopes, console, inline values.                    |

## Requests covered

`initialize`, `launch`, `attach`, `setBreakpoints`,
`setExceptionBreakpoints`, `setDataBreakpoints`,
`setFunctionBreakpoints`, `configurationDone`, `threads`,
`stackTrace`, `scopes`, `variables`, `evaluate`, `continue`, `next`,
`stepIn`, `stepOut`, `pause`, `disconnect`, `terminate`.

## Events handled

`stopped`, `continued`, `exited`, `terminated`, `output`,
`breakpoint`, `thread`, `initialized`.

## Pipelines

### Launch a session

```
user picks LaunchConfig (from .polyc/launch.json or DefaultLaunchConfigurations)
           │
           ▼
Substitute(${workspaceFolder}/${file}/${env:NAME}/${command:NAME}/${fileBasename})
           │
           ▼
DebugSession.Initialize → adapter.initialize response → DapClient.SetExceptionBreakpoints …
           │
           ▼
DebugSession.Launch / Attach → adapter signals `initialized`
           │
           ▼
SetBreakpoints (per file) → ConfigurationDone → run
```

### Hit a breakpoint

```
adapter event "stopped" {threadId, reason}
           │
           ▼
DebugSession.last_stop_reason / stopped_thread updated
           │
           ▼
auto-issued requests: threads → stackTrace → scopes
           │
           ▼
UI panels (Threads / Call Stack / Variables / Watch) re-render
```

## Tests

* [`tests/unit/polyui/dap_client_test.cpp`](../../tests/unit/polyui/dap_client_test.cpp)
* [`tests/unit/polyui/launch_config_test.cpp`](../../tests/unit/polyui/launch_config_test.cpp)
* [`tests/unit/polyui/debug_session_test.cpp`](../../tests/unit/polyui/debug_session_test.cpp)
