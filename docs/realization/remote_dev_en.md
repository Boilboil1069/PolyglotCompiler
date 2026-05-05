# Remote Development — SSH / WSL / Container / Dev Container

## Goal

Let polyglot development run anywhere — local workstation, an SSH
host, a WSL distro, a docker/podman container or a fully-described
Dev Container — through a single abstraction that polyls, the
DAP, the task runner and the integrated terminal all consume.

## Components

| Component | Header | Purpose |
| --- | --- | --- |
| `RemoteSession` | [`tools/ui/common/remote/remote_session.h`](../../tools/ui/common/remote/remote_session.h) | Abstract base: filesystem (`WriteFile/ReadFile/ListDir/RemoveFile`), processes (`Spawn/Kill/Wait`), port forwarding (`Forward/Unforward/ActiveForwards`) and terminal channel (`OpenTerminal`). |
| `LocalRemote` / `SshRemote` / `WslRemote` / `ContainerRemote` | same | Concrete backends. They share an in-memory bookkeeping layer and customise process launch by wrapping commands in `ssh`, `wsl` or `<runtime> exec` invocations. |
| `ParseConnectionString` | same | Parses `local:`, `ssh://[user@]host[:port]/path`, `wsl://distro/path`, `container://[runtime/]image-or-id/path` into a `RemoteDescriptor`. |
| `DevContainer` | [`tools/ui/common/remote/dev_container.h`](../../tools/ui/common/remote/dev_container.h) | Parses `.devcontainer/devcontainer.json`, produces the matching `RemoteDescriptor`, and emits a `ProvisionPlan` that installs polyls plus the LSP for every recognised feature. |
| `PlanSync` | [`tools/ui/common/remote/file_sync.h`](../../tools/ui/common/remote/file_sync.h) | Diffs a local and a remote file index into upload / download / delete operations; supports bidirectional, push-only and pull-only modes. |

## Pipelines

* **Connection string → session.** The command palette, the
  *Reopen In…* menu, and CLI arguments all go through
  `ParseConnectionString`. The resulting `RemoteDescriptor` is
  handed to `CreateSession`, which dispatches to the correct
  backend.
* **Unified subsystem layer.** polyls, the DAP and the task runner
  hold a `RemoteSession*` and never branch on the underlying
  transport. Local execution is just `LocalRemote`. Spawning
  `polyc build` on an SSH host produces
  `ssh -p <port> user@host -- polyc build`; on WSL, `wsl -d
  <distro> -- polyc build`; in a container, `docker exec -u <user>
  <container> polyc build`.
* **Dev Container provisioning.** When the user invokes *Reopen In
  Container* on a workspace with `.devcontainer/devcontainer.json`,
  the IDE calls `DevContainer::Parse`, hands the spec to
  `MakeDescriptor` (defaulting `workspaceFolder` to
  `/workspaces/<name>` and the runtime to `docker`), and applies
  `MakeProvisionPlan` once the container is up. The plan always
  installs polyls and adds the per-feature command + tool tag for
  Python, Node, Java, Go, Rust, .NET, Ruby and C++. Explicit
  `postCreateCommand` entries are appended verbatim.
* **Port forwarding / file sync / terminal.** `Forward(local,
  remote, host)` installs a forwarding rule on the active session;
  `ActiveForwards()` powers the *Forwarded Ports* view.
  `PlanSync(local_index, remote_index, direction)` produces an
  ordered batch of operations the orchestrator applies through
  `RemoteSession::WriteFile / ReadFile / RemoveFile`.
  `OpenTerminal(command)` returns the spawned process descriptor —
  empty `command` opens the backend default shell (`/bin/bash`
  for WSL, `/bin/sh` everywhere else).

## Tests

* [`tests/unit/polyui/remote_session_test.cpp`](../../tests/unit/polyui/remote_session_test.cpp)
  validates `RemoteKind` round-trips, every connection-string
  scheme (including `container://podman/...` and the
  runtime-omitted form), backend dispatch, the filesystem CRUD
  surface across all four backends, the SSH/WSL/container command
  wrapping, the process lifecycle (spawn/kill/wait/missing-id),
  the port-forward registry and `OpenTerminal` defaulting.
* [`tests/unit/polyui/dev_container_test.cpp`](../../tests/unit/polyui/dev_container_test.cpp)
  parses a realistic `devcontainer.json` (image, workspaceFolder,
  remoteUser, mixed numeric/string forwardPorts, array
  postCreateCommand, multiple features, remoteEnv), checks the
  descriptor defaults and the runtime/container override path, and
  asserts that the provision plan installs polyls plus the LSPs
  matching the requested features.
* [`tests/unit/polyui/file_sync_test.cpp`](../../tests/unit/polyui/file_sync_test.cpp)
  exercises action-name coverage, bidirectional newer-side-wins
  semantics, push-only and pull-only suppression, and the empty
  plan for identical indexes.

The full polyui suite runs at 875 assertions across 178 cases.
