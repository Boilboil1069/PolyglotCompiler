# 本地 Marketplace

## 目标

无须中心化服务即可分发、安装与更新扩展。Marketplace 读取本地或
HTTP 索引，按 id 选取最高 semver 版本，驱动
`ExtensionHost::Install` / `Uninstall`，并按 id 维护安装历史以
便用户随时回滚。

## 组件

| 组件 | 头文件 | 作用 |
| --- | --- | --- |
| `MarketplaceEntry` | [`tools/ui/common/ext/marketplace.h`](../../tools/ui/common/ext/marketplace.h) | 一个可安装制品：id、name、version、publisher、下载 URL、可选签名、sha256、所需能力。 |
| `MarketplaceIndex` | 同上 | 按 id 分桶，按版本由高到低排序。`Find(id)` 返回当前最新；`FindVersion(id, v)` 返回指定版本（用于回滚 / 锁定）。 |
| `ParseIndex(json)` | 同上 | 把索引 JSON（`{ "extensions": [...] }`）解析为 `MarketplaceIndex`。 |
| `SignaturePolicy` | 同上 | 可选安装门；启用后，每次安装必须携带与 id 受信值匹配的非空签名。 |
| `Marketplace` | 同上 | 串联索引、签名策略与按 id 的安装历史；对外暴露 `Install` / `Update` / `Rollback` / `Uninstall`。 |

## 流程

* **安装。** `Install(id)` 查找当前最新条目 → 校验签名 → 由条目
  构造 `ExtensionManifest` 交给 `ExtensionHost::Install`，并把
  版本追加到该 id 的历史。`Install(id, version)` 锁定指定版本
  （供 `Rollback` 使用）。
* **更新。** `Update(id)` 在宿主已是索引最新版本时返回 no-op；
  否则安装最新条目，并把旧版本写入结果，便于 UI 显示
  "updated A.B.C → X.Y.Z"。
* **回滚。** `Rollback(id)` 查询安装历史，选取最近安装之前的版
  本，先卸载当前记录再重装旧条目；历史不足两条时直接返回
  `no rollback target`。
* **签名。** `SignaturePolicy::Verify` 在未开启签名时直接通过；
  开启后，签名为空或与受信值不一致即拒绝安装。

## 测试

[`tests/unit/polyui/marketplace_test.cpp`](../../tests/unit/polyui/marketplace_test.cpp)
覆盖 `ParseIndex` 对多条目文档的往返与选最新；完整的安装 / 更
新 / 回滚流程及历史跟踪；签名策略对拒绝与放行两种路径；id 不
存在的失败路径。

## 工作区与多根

同一版本中
[`Workspace`](../../tools/ui/common/workspace/workspace.h)
解析与序列化 `polyui.code-workspace`，管理多根及每根独立设置
（文件夹值优先，其次工作区值），实现跨根搜索，并通过
`LanguageServerPool` 以 `(folder, language, version)` 隔离
polyls / DAP / 任务实例。具体行为见
[`workspace_test.cpp`](../../tests/unit/polyui/workspace_test.cpp)。
