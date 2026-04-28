# Polyglot UI 主题系统

`polyui` 内置一套 VS Code 风格的外部主题系统：每个主题都是一个普通的
`.polytheme.json` 文件（可附带同名 `.qss` 兜底），按 3 层目录契约发现，
按内嵌 JSON Schema 校验，运行时由 `ThemeService` 应用（在既有
`ThemeManager` 之上做组合）。

## 文件格式 — `.polytheme.json`

```jsonc
{
  "$schema": "polyglot://themes/theme_schema.json",
  "id":      "polyglot.dark",          // 反向 DNS，全局唯一
  "name":    "Polyglot Dark",          // 展示名
  "type":    "dark",                   // "dark" | "light" | "high-contrast"
  "version": "1.0.0",
  "author":  "Polyglot Team",
  "description": "默认深色主题。",
  "extends": "polyglot.base",          // 可选父主题 id
  "qss":     "polyglot.dark.qss",      // 可选同目录 QSS 文件
  "colors": {                          // VS Code 风格点号键
    "editor.background":   "#1e1e1e",
    "editor.foreground":   "#d4d4d4",
    "statusBar.background":"#007acc"
    /* …  约 60 个键，详见 api_reference.md … */
  },
  "tokenColors": [                     // TextMate 风格 scope 规则
    { "scope": "comment",  "settings": { "foreground": "#6a9955" } },
    { "scope": "keyword",  "settings": { "foreground": "#569cd6" } },
    { "scope": ["string", "string.quoted"],
                          "settings": { "foreground": "#ce9178" } }
  ]
}
```

通过 `extends` 可以继承另一个主题；解析顺序为父→子，子键覆盖父键，
循环引用会被检测并截断。

## 3 层发现机制

主题以下列优先级解析（高层覆盖低层）：

| 层级        | 路径                                                            |
| ----------- | --------------------------------------------------------------- |
| `workspace` | `<workspace_root>/.polyglot/themes/*.polytheme.json`            |
| `user`      | `<config>/PolyglotCompiler/themes/*.polytheme.json`             |
| `builtin`   | 嵌入在 `:/polyglot/themes/`（随可执行文件分发）                |

当两个主题 `id` 相同时，更高层级胜出。内置主题不可卸载。

## 内置主题（5 个）

| id                          | type            | 风格参考             |
| --------------------------- | --------------- | -------------------- |
| `polyglot.dark`             | `dark`          | VS Code Dark+        |
| `polyglot.light`            | `light`         | VS Code Light+       |
| `polyglot.high-contrast`    | `high-contrast` | WCAG-AAA 高对比      |
| `polyglot.solarized-dark`   | `dark`          | Ethan Schoonover     |
| `polyglot.solarized-light`  | `light`         |                      |

## 运行时 API — `ThemeService`

```cpp
auto &svc = polyglot::tools::ui::ThemeService::Instance();
svc.SetWorkspaceRoot("/path/to/project");   // 可选
svc.Scan();                                  // 扫描全部 3 层
svc.Activate("polyglot.dark");               // 应用到 QApplication
QString fg = svc.ResolveColor("editor.background");
QString kw = svc.ResolveTokenColor("keyword.control");
QObject::connect(&svc, &ThemeService::themeChanged,
                 widget, &MyWidget::OnThemeChanged);
```

每当激活主题改变（API 调用、磁盘文件被外部修改触发 `QFileSystemWatcher`
重扫——500 ms 防抖、或用户在 Theme Manager 中切换），都会发出
`themeChanged(id)`，并把所选 id 持久化到用户的 `workbench.colorTheme` 设置。

既有的 `ThemeManager::Instance().XStylesheet()` 调用点保持原样：
`ThemeService` 把每个加载到的主题以其 `name` 注册进
`ThemeManager` 的 map，并调用 `SetActiveTheme`，因此现有控件自动重置样式。

## Theme Manager 对话框

入口：**View → Theme Manager…**、命令面板（**Preferences: Color Theme**
或 **Preferences: Manage Themes**），或快捷键 `Ctrl+K Ctrl+T`。
3 栏对话框允许：

- 按名称和类型过滤；
- 把选中主题渲染为模拟工作台预览（菜单、编辑区、状态栏）；
- 在树形视图里逐键查看所有点号颜色，附带颜色色块；
- **Activate** 激活、**Install** 从磁盘安装、**Uninstall** 卸载用户主题；
- **Duplicate** 把所选主题以 `extends` 链接复制到 user 层，便于编辑；
- **Export** 把当前主题导出为已展开 `extends` 链的自包含
  `.polytheme.json`。

## 开发者命令（命令面板）

| 命令 id                                       | 标题                                              |
| --------------------------------------------- | ------------------------------------------------- |
| `workbench.action.selectTheme`                | Preferences: Color Theme                          |
| `workbench.action.openColorTheme`             | Preferences: Manage Themes                        |
| `workbench.action.generateColorTheme`         | Developer: Generate Color Theme From Current Settings |
| `editor.action.inspectTMScopes`               | Developer: Inspect Editor Token                   |
| `workbench.action.inspectColorTheme`          | Developer: Inspect UI Color Key                   |

## polyui 命令行参数

```
polyui --theme <id|path>          按 id 或 .polytheme.json 路径激活主题
polyui --list-themes              打印发现到的全部主题到 stdout
polyui --validate-theme <path>    校验文件，输出 JSON 诊断
polyui --headless                 使用 offscreen QPA 平台
polyui --headless --screenshot <out.png>
                                  渲染一次后保存 PNG 退出（CI 友好）
```

## 热重载

`ThemeService` 对加载过的每个目录与文件都安装了
`QFileSystemWatcher`。当任意 `.polytheme.json` 或同目录 `.qss` 改动时，
500 ms 防抖定时器触发 `RescanNow()`，重新加载并重激活先前选定的主题。
作者可以直接在外部编辑器中保存，`polyui` 会在半秒内反映出改动。

## 作者工作流

1. 运行 **Developer: Generate Color Theme From Current Settings**
   将当前主题派生到 `~/.config/PolyglotCompiler/themes/`。
2. 用任意编辑器打开生成的 `.polytheme.json`。
3. 保存——`polyui` 自动热重载。
4. 用 **Developer: Inspect UI Color Key** / **Inspect Editor Token**
   找到要覆盖的键名。
5. 满意后直接分发该文件，或打包为 `.polythemepack`（一个 ZIP，
   含一个或多个 `.polytheme.json` + 清单文件）。

## 校验

内嵌 schema (`:/polyglot/themes/theme_schema.json`) 要求：

- `id`、`name`、`type`（取值 `dark`/`light`/`high-contrast`）
- `colors`：值必须是 `#rrggbb` 或 `#rrggbbaa` 形式
- `tokenColors`：可选，元素必须是 `{scope, settings.foreground}` 形式

`ThemeService::ValidateString` 做结构校验；CLI 参数
`--validate-theme` 打印 JSON 诊断报告，校验失败时以非 0 码退出。

## 关于硬编码样式

本系统是 `ThemeManager` 的补充而非替代。既有控件继续调用
`ThemeManager::Instance().XStylesheet()` 系列访问器；这些访问器
现在由激活主题的 color 映射驱动。新写控件时请使用这些访问器，
不要再写裸的 QSS 字符串。