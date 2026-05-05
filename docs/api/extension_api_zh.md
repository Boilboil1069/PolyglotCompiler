# 扩展 API 参考

本文档是 PolyUI 扩展系统的权威参考，描述 `extension.json`
清单、激活模型、能力沙箱、所有贡献点以及面向扩展作者的宿主
生命周期接口。

## 1. 清单 (`extension.json`)

每个扩展都是自描述单元：一份 `extension.json` 加载荷（原生
动态库或 JavaScript / TypeScript bundle）。宿主调用
`ParseManifest` 解析清单；任一必填字段缺失即被拒绝。

| 字段                   | 必填 | 说明 |
| ---------------------- | ---- | --- |
| `id`                   | 是   | 扩展唯一 id，约定为 `publisher.name`。 |
| `version`              | 是   | 语义化版本（`MAJOR.MINOR.PATCH`）。 |
| `entry_point` / `main` | 是   | 载荷路径（相对清单）。 |
| `name`                 | 否   | UI 中展示的名称。 |
| `publisher`            | 否   | 发布者 id。 |
| `description`          | 否   | 一行描述。 |
| `loader`               | 否   | `native`（默认）或 `javascript`。 |
| `activation`           | 否   | 激活触发器数组（§2）。 |
| `capabilities`         | 否   | 能力名数组（§3）。 |
| `contributes`          | 否   | 贡献点映射（§4）。 |

载荷类型：

* **`native`**——导出宿主 ABI 的共享库；C / C++ 编写的扩展使用
  此类型。
* **`javascript`**——打包后的 JS/TS，由内嵌引擎执行（默认
  QuickJS；构建时 `POLYUI_V8=ON` 时切换为 V8）。

### 1.1 版本比较

`CompareVersion("1.10.0", "1.2.3")` 返回正值；
`CompareVersion("1.0", "1.0.0")` 返回 0；缺省段视为 0。安装时
新版本替换旧版本，宿主不会保留旧清单的任何贡献点。

## 2. 激活事件

任一激活触发器命中即激活扩展。触发器要么是字符串（视为不带参
数的 `event`），要么是带 `event` 与可选 `argument` 的对象。

| 事件         | 参数               | 触发语义 |
| ------------ | ------------------ | --- |
| `onStartup`  | —                  | IDE 启动即触发。 |
| `onLanguage` | 语言 id            | 首次打开该语言的缓冲区时触发。 |
| `onCommand`  | 命令 id            | 命令被调用时触发。 |
| `onView`     | 视图 id            | 视图首次渲染时触发。 |
| `onDebug`    | debug type         | 启动匹配的调试会话时触发。 |
| `onFileOpen` | glob 或扩展名      | 首次匹配文件被打开时触发。 |

`MatchesActivationEvent(id, event, argument)` 在任一触发器命中
时返回 true；触发器 `argument` 为空时匹配所有具体参数。

## 3. 能力沙箱

扩展所需的每一项能力必须出现在 `capabilities` 中；
`CapabilityGate` 在用户未授予任一所需能力时阻止激活。

| 能力          | 允许的操作 |
| ------------- | --- |
| `filesystem`  | 读写扩展自身目录之外的文件。 |
| `network`     | 主动发起出站网络请求。 |
| `process`     | 启动子进程。 |
| `clipboard`   | 读系统剪贴板（写入始终允许）。 |
| `secrets`     | 读写用户密钥存储。 |

能力按 (扩展, 能力) 粒度授权；为某一扩展授予 `network` 不会
波及其他扩展。

## 4. 贡献点

`contributes` 是贡献点映射；宿主把每条记录归一化为
`Contribution`（`kind` / `id` / 自由 `properties`）。两条共享
`(kind, id)` 的贡献会被合并为一条——最近激活的扩展生效。

支持键：`commands`、`keybindings`、`menus`、`panels`、`views`、
`statusBarItems`、`themes`、`languageClients`、`debugAdapters`、
`fileIconThemes`、`formatters`、`snippets`、`tasks`、
`refactorProviders`。

每条必须含 `id`；`title` 用于菜单与命令面板；其余字符串字段
原样保存到 `properties`，以便特定贡献点的元数据可往返序列化。

## 5. 宿主生命周期

| 调用                              | 效果 |
| --------------------------------- | --- |
| `Install(manifest)`               | 记录扩展；同 id 已存在更新或相同版本时拒绝。 |
| `Uninstall(id)`                   | 移除记录及其全部贡献。 |
| `Activate(id)`                    | 校验能力 → 注册贡献 → 状态转为 `kActivated`。 |
| `Deactivate(id)`                  | 移除贡献，状态回到 `kInstalled`。 |
| `Reload(id)`                      | `Deactivate` → `Activate`；设置或授权变更后调用。 |
| `MatchesActivationEvent(...)`     | IDE shell 借此判断何时 `Activate`。 |
| `Contributions()` / `ContributionsOfKind(kind)` | 已去重的注册表。 |

## 6. 完整示例

```json
{
  "id": "polyglot.sample",
  "name": "Sample",
  "version": "0.2.1",
  "publisher": "polyglot",
  "main": "out/extension.js",
  "loader": "javascript",
  "activation": [
    "onStartup",
    { "event": "onLanguage", "argument": "ploy" }
  ],
  "capabilities": ["filesystem", "network"],
  "contributes": {
    "commands": [
      { "id": "sample.hello", "title": "Hello" }
    ],
    "keybindings": [
      { "id": "sample.hello.key", "key": "Ctrl+H" }
    ]
  }
}
```

## 7. 测试

参考行为由
[`tests/unit/polyui/extension_api_test.cpp`](../../tests/unit/polyui/extension_api_test.cpp)
锁定：loader / activation / capability 名往返、不等长 semver
比较、完整清单解析、能力门、安装 / 激活 / 贡献注册表、两扩展
间贡献去重、激活事件的通配与具体参数匹配。

polyui 全套合计 216 例 1096 条断言全部通过。
