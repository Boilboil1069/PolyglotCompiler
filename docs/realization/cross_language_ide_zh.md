# 跨语言 IDE —— 导航、Bridge 面板与 Marshalling 视图

## 目标

把跨语言能力提升为 IDE 一等体验：用户可在 `.ploy` 与各宿主语言
源码之间跳转，并以一次协调的编辑完成跨语言重命名；可在面板中
浏览每个生成的 bridge 及其实时调用次数；并能直观查看连接两侧的
转换流水线。

## 组件

| 组件 | 头文件 | 作用 |
| --- | --- | --- |
| `LinkRegistry` | [`tools/ui/common/cross_language/cross_language_navigator.h`](../../tools/ui/common/cross_language/cross_language_navigator.h) | `.ploy` LINK 点位与宿主语言定义的目录；提供 goto-def、反向引用、CodeLens 引用计数。 |
| `RenamePlanner` | 同上 | 生成贯穿宿主语言定义、所有 `.ploy` LINK 点位与宿主 LSP 报告的引用的协调 `WorkspaceEdit` 方案。 |
| `BridgePanelModel` | [`tools/ui/common/cross_language/bridge_panel.h`](../../tools/ui/common/cross_language/bridge_panel.h) | 从 `aux/bridges.json` 加载 bridge 清单；记录 marshalling 策略、源码位置以及来自 polyrt calltrace 的实时调用计数；重新导入时保留运行期计数。 |
| `MarshallingViewBuilder` | [`tools/ui/common/cross_language/marshalling_view.h`](../../tools/ui/common/cross_language/marshalling_view.h) | 每个 bridge 的 IR 下降 → helper → ABI 适配器链路；可加载 `aux/marshalling.json`，或基于 bridge 元数据合成标准三阶段流水线。 |

## 流程

* **跳转定义。** polyls 同时把 `.ploy` 中解析到的 LINK 点位以及
  各宿主语言 LSP 解析出的定义灌入注册表；`GotoDefinition(site)`
  在目录上以 O(N) 返回匹配定义。
* **反向引用 / CodeLens。** `FindLinkReferences(def)` 扫描 LINK
  点位；`CodeLensFor(file)` 返回每个定义对应的 lens，锚点位于
  定义源位置，带 `.ploy` 引用计数。
* **协调重命名。** `RenamePlanner::Plan(language, symbol,
  new_name, extra_references)` 遍历所有目录中的定义、LINK 点位
  以及 LSP 报告的引用，为每个改写位置发出一条 `WorkspaceEdit`；
  polyls 以一次 `workspace/applyEdit` 提交给各活动 LSP。
* **Bridge 清单。** polyc 每次构建后产出 `aux/bridges.json`，
  `BridgePanelModel::ImportFromAux` 摄入并填充面板；
  `IncrementCallCount(id)` 接入 polyrt calltrace 流，面板实时
  更新；`FilterByLanguagePair` 支撑列筛选。
* **Marshalling 链。** `MarshallingViewBuilder::LoadAux` 解析
  polyc 输出的逐符号参数/返回链；若某 bridge 暂无对应记录，
  `Synthesize(bridge)` 会基于 strategy 合成标准三阶段流水线
  （IR 下降、以策略命名的 marshalling helper、目标语言 ABI
  适配器），保证面板始终可呈现内容。

## 测试

* [`tests/unit/polyui/cross_language_navigator_test.cpp`](../../tests/unit/polyui/cross_language_navigator_test.cpp)
  覆盖 HostLanguage 名称往返、goto-def、反向引用、CodeLens 计数
  与协调重命名（1 个定义 + 2 个点位 + 1 个 LSP 引用 = 4 处编辑）。
* [`tests/unit/polyui/bridge_panel_test.cpp`](../../tests/unit/polyui/bridge_panel_test.cpp)
  解析覆盖 5 种宿主语言与各 marshalling 策略的 aux 文档，断言
  重新导入会保留运行期计数，并验证按语言对筛选的辅助方法。
* [`tests/unit/polyui/marshalling_view_test.cpp`](../../tests/unit/polyui/marshalling_view_test.cpp)
  解析真实链路文档，并为 5 种宿主语言分别合成链路，验证三阶段
  布局与 ABI 适配器标签。

polyui 全套合计 139 例 626 条断言全部通过。
