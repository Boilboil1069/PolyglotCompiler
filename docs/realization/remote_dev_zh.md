# 远程开发 —— SSH / WSL / Container / Dev Container

## 目标

让 polyglot 开发可以在任意环境运行——本地工作站、SSH 主机、WSL
发行版、docker / podman 容器，或由 `devcontainer.json` 完整描述
的 Dev Container——并通过同一套抽象供 polyls、DAP、任务系统与
集成终端共同消费。

## 组件

| 组件 | 头文件 | 作用 |
| --- | --- | --- |
| `RemoteSession` | [`tools/ui/common/remote/remote_session.h`](../../tools/ui/common/remote/remote_session.h) | 抽象基类：文件系统（`WriteFile/ReadFile/ListDir/RemoveFile`）、进程（`Spawn/Kill/Wait`）、端口转发（`Forward/Unforward/ActiveForwards`）与终端通道（`OpenTerminal`）。 |
| `LocalRemote` / `SshRemote` / `WslRemote` / `ContainerRemote` | 同上 | 具体后端；共享一层内存中的元数据，按目标传输包装进程命令。 |
| `ParseConnectionString` | 同上 | 解析 `local:`、`ssh://[用户@]主机[:端口]/路径`、`wsl://发行版/路径`、`container://[runtime/]镜像或 id/路径` 为 `RemoteDescriptor`。 |
| `DevContainer` | [`tools/ui/common/remote/dev_container.h`](../../tools/ui/common/remote/dev_container.h) | 解析 `.devcontainer/devcontainer.json`，产出对应 `RemoteDescriptor` 与 `ProvisionPlan`（始终提供 polyls 以及识别出的容器 feature 所需 LSP）。 |
| `PlanSync` | [`tools/ui/common/remote/file_sync.h`](../../tools/ui/common/remote/file_sync.h) | 对本地与远端文件索引求差，输出 upload / download / delete 计划；支持双向、push-only、pull-only。 |

## 流程

* **连接串 → 会话。** 命令面板、*在……中重新打开* 菜单与 CLI
  参数都先经过 `ParseConnectionString`；得到的 `RemoteDescriptor`
  交给 `CreateSession`，由其分派到对应后端。
* **统一子系统层。** polyls、DAP、任务系统持有一个
  `RemoteSession*`，永远不在传输层面分支；本地执行就是
  `LocalRemote`。在 SSH 主机上启动 `polyc build` 会生成
  `ssh -p <端口> 用户@主机 -- polyc build`；WSL 上是
  `wsl -d <发行版> -- polyc build`；容器内是
  `docker exec -u <用户> <容器> polyc build`。
* **Dev Container 配置。** 用户在含 `.devcontainer/devcontainer.json`
  的工作区调用 *在容器中重新打开* 时，IDE 调用
  `DevContainer::Parse`，把 spec 交给 `MakeDescriptor`（默认
  `workspaceFolder=/workspaces/<name>`、runtime=`docker`），容器
  起来后应用 `MakeProvisionPlan`。该计划始终安装 polyls，并按
  Python、Node、Java、Go、Rust、.NET、Ruby、C++ 等已识别 feature
  追加各自的安装命令与工具标签；显式的 `postCreateCommand` 原样
  追加。
* **端口转发 / 文件同步 / 终端。** `Forward(本地, 远端, 主机)`
  在当前会话注册转发规则；`ActiveForwards()` 支撑 *已转发端口*
  视图。`PlanSync(本地索引, 远端索引, 方向)` 输出有序操作批次，
  由编排层通过 `RemoteSession::WriteFile / ReadFile / RemoveFile`
  执行。`OpenTerminal(命令)` 返回所启动的进程描述符——命令为空
  时打开后端默认 shell（WSL 为 `/bin/bash`，其余为 `/bin/sh`）。

## 测试

* [`tests/unit/polyui/remote_session_test.cpp`](../../tests/unit/polyui/remote_session_test.cpp)
  验证 `RemoteKind` 名往返、四种连接串方案（含
  `container://podman/...` 与省略 runtime 的形式）、后端分派、
  四种后端的文件 CRUD、SSH/WSL/容器命令包装、进程生命周期
  （spawn/kill/wait/未知 id）、端口转发注册表与 `OpenTerminal`
  默认 shell。
* [`tests/unit/polyui/dev_container_test.cpp`](../../tests/unit/polyui/dev_container_test.cpp)
  解析典型 `devcontainer.json`（image、workspaceFolder、
  remoteUser、数字/字符串混合 forwardPorts、数组形式
  postCreateCommand、多 feature、remoteEnv），核查描述符默认值与
  runtime / 容器覆盖路径，并断言 provision plan 安装 polyls 以及
  请求 feature 对应的 LSP。
* [`tests/unit/polyui/file_sync_test.cpp`](../../tests/unit/polyui/file_sync_test.cpp)
  覆盖 action 名字、双向 newer-side-wins、push-only / pull-only
  抑制方向，以及索引相同的空计划。

polyui 全套合计 178 例 875 条断言全部通过。
