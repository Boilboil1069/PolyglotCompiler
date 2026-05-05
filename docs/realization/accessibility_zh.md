# 无障碍

PolyUI 的无障碍模型面向纯键盘操作、主流屏幕阅读器（NVDA、
JAWS、VoiceOver、Orca）以及视觉辅助（高对比度、大字体、减少
动效）。

## 焦点顺序

[`FocusOrder`](../../tools/ui/common/a11y/accessibility.h) 维护
线性键盘 Tab 链。控件按 `tab_index` 升序排序，相同时以 id 为
次要键，确保跨次运行顺序稳定。`Next` 与 `Previous` 循环回绕，
并跳过 `focusable=false` 的控件，因此禁用某项不会打断链条。

每个可聚焦控件都有 `role`（`button`、`textbox`、`list` …）与
可访问 `label`。Qt 层将其映射到平台无障碍桥——Windows 的
UIA、Linux 的 AT-SPI、macOS 的 NSAccessibility。

## 屏幕阅读器播报

`ScreenReaderQueue::Post` 接受 polite 或 assertive 播报。
Drain 时先按 FIFO 取出 assertive，再取 polite，对应 WAI-ARIA
live-region 模型。Qt 层把 drain 出的内容投递给平台桥，
NVDA / JAWS / VoiceOver / Orca 即可读出。

## 视觉配置

`AccessibilityProfile` 汇总视觉开关：

* `high_contrast`——选择高对比度主题。
* `large_font` + `font_scale_percent`——夹紧到 80–300 %。
* `reduce_motion`——关闭过渡与动画。
* `preferred_theme`——可选的显式主题覆盖。

配置通过 `SerializeProfile` / `DeserializeProfile` 往返，因此
用户的选择跨重启保留。

## 手工测试清单

* 仅用键盘穿过每个对话框。
* 验证 NVDA / JAWS（Windows）、VoiceOver（macOS）、Orca
  （Linux）能播报当前焦点控件的 role 与 label。
* 在设置里切高对比度与大字体；检查所有面板仍可用。
* 切换减少动效；确认 IDE 不再有面板过渡动画。

单元覆盖见
[`tests/unit/polyui/localization_test.cpp`](../../tests/unit/polyui/localization_test.cpp)。
