# IDE 外壳教程——欢迎页、通知、状态栏、书签、TODO

本教程逐项走查 IDE 外壳：欢迎页、通知中心、可定制状态栏、最
近列表、会话恢复、书签与 TODO 索引。

## 1. 欢迎页

PolyUI 启动时，欢迎页展示最近工作区（按路径去重、最新在前）、
教程与样例入口，以及当前版本的新特性提示。取消 *启动时显示*
即隐藏；点击别针图标可把它常驻为标签页。

页面通过 `WelcomePage::Serialize` 序列化为 JSON，所选教程、样例
与提示在重启后保留。

## 2. 通知中心

所有长期存活的消息都进入通知中心。通知带分级（`info` /
`warning` / `error` / `progress`）、标题、正文、来源子系统及若干
action 按钮。状态栏铃铛显示未读计数。

* 点击条目标记为已读。
* *忽略* 把它移出活动列表，但 `List(include_dismissed=true)` 仍
  能审计到。
* *不打扰* 屏蔽 `info` 与 `progress`；警告与错误始终透传。

## 3. 可定制状态栏

外壳内建九个槽位：`branch`、`problems`、`language`、
`language_server`、`encoding`、`eol`、`indent`、`package_manager`、
`profiler`。右键任一槽位可显隐；拖动可在左右两侧切换（`priority`
决定同侧顺序）。扩展通过 `StatusBar::Register` 注册自己的槽位。

## 4. 最近文件 / 工作区

`Ctrl+R` 打开最近工作区面板，`Ctrl+E` 打开最近文件面板。两者共
享 `RecentList` 行为：重开即提顶；固定项浮于未固定项之上；列表
按可配置容量裁剪，并保留全部固定项。

## 5. 会话恢复

会话恢复默认开启时，PolyUI 保存分屏布局、每个打开的标签（含
光标 / 滚动 / 折叠）、面板大小与显隐、当前调试配置及其 watch /
打开的子视图，以及其他特性塞入的字符串 extras。重启后一切原样
回来。可在 *设置 → Workbench → Session* 关闭。

## 6. 书签

`Ctrl+Alt+K` 切换当前行的书签。书签面板列出工作区内所有书签，
每条可改名、改色。`BookmarkStore::Toggle` 在空行返回新书签、
在已有书签处返回 `nullopt`，因此同一个快捷键即可加与去。

## 7. TODO / FIXME 索引

后台扫描器在保存时按可配置关键字集合重扫文件（默认 `TODO`、
`FIXME`；可扩展 `XXX`、`HACK` 等）。扫描器强制单词边界，
`TODOMARKER` 不会被误中。汇总面板按关键字分组，并通过
`CountsByKeyword` 给出聚合计数。
