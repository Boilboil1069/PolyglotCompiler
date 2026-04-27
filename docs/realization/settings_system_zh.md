# VS Code 风格设置体系 — 实现说明

> 需求 **2026-04-27-4**（JSON 优先的设置系统）的实现文档。
> 覆盖需求条目全部 11 个章节。

---

## 1. 目标

将原有基于 `QSettings` 的偏好存储替换为 **JSON 优先**、三层覆盖的设置体系，
对齐 VS Code 模型：

- `settings.json` 是**唯一事实源**。
- IDE (`polyui`) 和所有命令行工具（`polyc`、`polyld`、`polyrt`、`polytopo`、
  `polybench`）通过同一个共享加载器读取同一份文件。
- 由 JSON Schema 描述每个键，驱动双语提示、校验与自动生成的表单视图。

---

## 2. 三层模型

| 优先级 | 层级       | 路径（Windows）                                            | 路径（Linux/macOS）                                  |
|-------:|------------|------------------------------------------------------------|------------------------------------------------------|
| 1（低） | 默认       | 内嵌资源 `:/polyglot/settings/default_settings.json`        | 同左                                                 |
| 2      | 用户       | `%APPDATA%\PolyglotCompiler\settings.json`                  | `~/.config/PolyglotCompiler/settings.json`（或 `$XDG_CONFIG_HOME`）；macOS 为 `~/Library/Application Support/PolyglotCompiler/` |
| 3（高） | 工程       | `<workspace>/.polyglot/settings.json`                       | 同左                                                 |

`DeepMerge(default, user, workspace)` 产生**生效树**。
规范化存储采用顶层**扁平点分键**（如 `{"editor.tabSize": 4}`），
`GetByDottedKey()` 兼容嵌套对象路径。

---

## 3. 组件

| 组件                       | 位置                                                          | 职责                                                                                 |
|----------------------------|---------------------------------------------------------------|--------------------------------------------------------------------------------------|
| `polyglot_tools_settings`（静态库） | `tools/common/{include,src}/effective_settings_loader.{h,cpp}` | 纯 C++ 加载器、Schema 校验器、点分键 get/set、CLI 标志辅助函数。                       |
| `SettingsService`          | `tools/ui/common/{include,src}/settings_service.{h,cpp}`      | Qt 单例；`QFileSystemWatcher`（200 ms 防抖）；`Get/Set/Reset`；信号 `settingsChanged`、`settingsReloaded`；`MigrateLegacyQSettings()`。 |
| `KeybindingService`        | `tools/ui/common/{include,src}/keybinding_service.{h,cpp}`    | 命令注册表；默认 + 用户键位；和弦解析；`when` 表达式求值（`!`、`&&`、`||`、括号、标识符）。 |
| `CommandPalette`           | `tools/ui/common/{include,src}/command_palette.{h,cpp}`       | `QDialog` + 模糊过滤；绑定 `Ctrl+Shift+P`。                                          |
| `SettingsPage`             | `tools/ui/common/{include,src}/settings_page.{h,cpp}`         | VS Code 双栏编辑器（左命名空间，右 schema 驱动表单）；每行显示来源徽标；提供“打开 用户/工程/默认 JSON”按钮。 |

CLI 加载器与 Qt 服务**共用** `polyglot_tools_settings`，无第二份 JSON 解析器
或 schema 校验器。

---

## 4. 内置命令

由 `MainWindow::RegisterBuiltinCommands()` 注册：

| 命令 id                                            | 默认快捷键        | 行为                                                    |
|----------------------------------------------------|-------------------|---------------------------------------------------------|
| `workbench.action.openSettings`                    | —                 | 打开设置表单（`SettingsPage`）。                        |
| `workbench.action.openSettingsJson`                | —                 | 在编辑器中打开用户 `settings.json`。                    |
| `workbench.action.openWorkspaceSettingsJson`       | —                 | 打开 `<workspace>/.polyglot/settings.json`（不存在则创建）。 |
| `workbench.action.openDefaultSettingsJson`         | —                 | 打开默认设置（只读，物化到临时文件）。                    |
| `workbench.action.openKeybindings`                 | —                 | 打开 `keybindings.json`。                               |
| `workbench.action.files.save` / `saveAs` / `openFile` / `newUntitled` | 同编辑器菜单 | 文件操作命令也暴露给命令面板。                            |
| `workbench.action.showCommands`                    | `Ctrl+Shift+P`    | 打开命令面板。                                          |
| `editor.action.toggleMarkdownPreview`              | —                 | 切换 Markdown 预览（需求 2026-04-27-2）。               |

用户键位保存在 `settings.json` 同级目录的 `keybindings.json`，由
`KeybindingService::LoadUserKeybindings()` 热加载。

---

## 5. CLI 集成

每个 CLI 工具在 `main()` 顶部调用：

```cpp
if (auto rc = polyglot::tools::common::HandleSettingsCliFlags(argc, argv);
    rc.has_value()) {
  return *rc;
}
```

辅助函数识别：

- `--settings <path>` — 用显式路径覆盖用户层。
- `--print-effective-settings` — 把合并后的 JSON 打印到 stdout 并退出 0。

`polyc::ParseArgs()` 已扩展以跳过这两个标志，避免“未知参数”报错。

---

## 6. Schema 驱动的表单

`SettingsPage` 读取 `settings_schema.json`，按 `type` 分发：

| Schema type            | 控件                  |
|------------------------|-----------------------|
| `boolean`              | `QCheckBox`           |
| `integer` (+ min/max)  | `QSpinBox`            |
| `number`               | `QDoubleSpinBox`      |
| `string` + `enum`      | `QComboBox`           |
| `string`               | `QLineEdit`           |
| `array` / `object`     | `QLineEdit`（JSON 字面量） |

每行显示**来源徽标**（`(default)` / `(user)` / `(workspace)`），让用户清楚
当前值来自哪一层。

---

## 7. 从 `QSettings` 迁移

`SettingsService::MigrateLegacyQSettings()` 在首次启动时执行一次：

| 旧 QSettings 键                 | 新 JSON 键                   |
|----------------------------------|------------------------------|
| `appearance/font_family`         | `editor.fontFamily`          |
| `appearance/font_size`           | `editor.fontSize`            |
| `editor/tab_width`               | `editor.tabSize`             |
| `editor/insert_spaces`           | `editor.insertSpaces`        |
| `editor/word_wrap`               | `editor.wordWrap`            |
| `editor/auto_indent`             | `editor.autoIndent`          |
| `editor/line_numbers`            | `editor.lineNumbers`         |
| `topology/layout_mode`           | `topology.layoutAlgorithm`   |
| ...                              | ...                          |

**不修改**原 QSettings 存储；写入标记文件 `settings.json.qsettings.bak`
保证迁移幂等。

---

## 8. 热重载

`SettingsService` 通过 `QFileSystemWatcher` + 200 ms `QTimer` 防抖。
重载流程：

1. 解析并校验新 JSON。
2. 对比新旧生效树。
3. 对每个变更键发射 `settingsChanged(key, oldValue, newValue)`。
4. 最后发射 `settingsReloaded()`，供全局监听者刷新。

`MainWindow::ApplyEditorSettings()` 监听该信号，无需重启即可重绘编辑器。

---

## 9. 测试

`tests/unit/tools/settings_loader_test.cpp`（测试二进制 `test_settings`）覆盖：

- `DeepMerge` 递归对象合并；
- 扁平点分键往返；
- Schema 校验：`type`、`enum`、`minimum`、`maximum`；
- 三层优先级（`workspace` > `user` > `default`）；
- 用户 JSON 损坏时返回诊断、默认值仍生效；
- 路径解析返回平台正确的位置。

Qt 相关服务（`SettingsService`、`KeybindingService`、`CommandPalette`、
`SettingsPage`）通过既有 `test_topology_ui` 的 Qt 条件编译测试模式覆盖；
`KeybindingService::ParseChord` 与 `when` 表达式求值的纯 C++ 逻辑由同一个
`test_settings` 二进制在后续工单中扩展覆盖。

---

## 10. 文件布局

```
tools/
  common/
    include/effective_settings_loader.h         ← 共享 API
    src/effective_settings_loader.cpp
  ui/common/
    resources/
      default_settings.json                     ← 第 1 层
      settings_schema.json                      ← Schema
      settings_resources.qrc
    include/
      settings_service.h
      keybinding_service.h
      command_palette.h
      settings_page.h
    src/
      settings_service.cpp
      keybinding_service.cpp
      command_palette.cpp
      settings_page.cpp
tests/unit/tools/settings_loader_test.cpp       ← `test_settings`
```

---

## 11. 后续工作

- 在 JSON 编辑标签页提供 Schema 感知的悬浮提示与自动补全（当前复用普通
  语法高亮编辑器）。
- 支持配置档案（`workbench.profile`）以一键切换整套 JSON。
- 通过 Git 或云端同步设置（本需求范围外）。
