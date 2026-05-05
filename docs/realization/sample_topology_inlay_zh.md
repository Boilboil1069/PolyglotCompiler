# Sample / Tutorial Browser + Topology Live + 类型推断浮层

## 目标

为 IDE 补齐三项可发现性能力：内置样例与教程的精选目录（可一键
克隆为工作区副本）、跟随编辑器焦点并增量更新的拓扑视图，以及
直接在编辑器内呈现推断类型与参数名的 inlay hint。

## 组件

| 组件 | 头文件 | 作用 |
| --- | --- | --- |
| `SampleCatalogue` | [`tools/ui/common/samples/sample_browser.h`](../../tools/ui/common/samples/sample_browser.h) | 从 JSON 加载 `tests/samples/` 与 `docs/tutorial/` 目录条目，按语言 / 主题 / 难度 / 全文筛选，并规划将某条目以事务方式复制到目标目录。 |
| `TopologyGraph` | [`tools/ui/common/topology_live/topology_live.h`](../../tools/ui/common/topology_live/topology_live.h) | 内存图（节点 + 边）；`Neighbourhood(id, radius)` BFS 同时沿入/出边扩张。 |
| `LiveTopologyTracker` | 同上 | 跟随焦点视图：`FocusOn(文件, 符号, radius)` 返回该符号的邻域（符号未知时退回到该文件锚定的节点），`NotifyEdit/ShouldRebuild` 提供带 debounce 的重建触发，`NodeSource` 把节点点击解析回源位置。 |
| `InlayHintProvider` | [`tools/ui/common/inlay_hints/inlay_hints.h`](../../tools/ui/common/inlay_hints/inlay_hints.h) | 基于分析器送入的声明与调用实参生成类型 hint（`: HANDLE<...>`）和参数名 hint（`x:`）；两类 hint 可独立开关。 |

## 流程

* **样例目录。** polyc 与 IDE 共享一份 `samples.json`，列出每个
  条目的 id、标题、kind（`sample` / `tutorial`）、难度、语言
  标签、主题标签、源根目录与文件清单。`SampleCatalogue::LoadIndex`
  摄入文档；`Filter` 对每个已设置的条件求值（语言 / 主题成员、
  难度相等、kind 相等、对 title/id/summary 的大小写无关全文匹
  配），返回命中条目。`PlanCopy(id, dst)` 为条目根下每个文件
  生成 `(src, dst)` 列表，IDE 据此事务式复制。
* **Topology Live。** 静态拓扑面板保留完整图；
  `LiveTopologyTracker::FocusOn` 在编辑器报告新焦点时抽取相关
  邻域，符号未知时退回到该文件锚定的节点（例如光标位于非符号
  注释中）。编辑被批量化：每次键入编辑器调用 `NotifyEdit(now)`；
  当距上次编辑超过 debounce 阈值时下一次 `ShouldRebuild(now)`
  返回 true，编排器随即调用 `ReplaceGraph(new_graph)` 完成增量
  重建。`NodeSource(id)` 为 拓扑→编辑器 跳转提供位置。
* **Inlay hint。** polyls 分析器向 provider 喂入两个数组：声明
  （含标识符末位的位置与推断类型字符串）与调用实参（含形参名与
  实参表达式起始位置）。`Produce` 遍历两者，按声明在前、实参在
  后的顺序产出；类型 hint 设 `padding_left`，参数名 hint 设
  `padding_right`；类型字符串或形参名为空时跳过。设置可独立开关
  两类；同时关闭则无 hint 产出。

## 测试

* [`tests/unit/polyui/sample_browser_test.cpp`](../../tests/unit/polyui/sample_browser_test.cpp)
  验证 kind / 难度名往返、混合样例 / 教程的 JSON 摄入、所有筛
  选轴（语言、主题、难度、kind、全文）以及逐文件 copy plan。
* [`tests/unit/polyui/topology_live_test.cpp`](../../tests/unit/polyui/topology_live_test.cpp)
  断言 `Neighbourhood` 同时跟随两个方向的边，`FocusOn` 在符号
  未知时退回到文件锚定节点，debounce 仅在静默间隔过后触发并消
  耗待重建状态，`NodeSource` 对未知 id 返回 nullopt。
* [`tests/unit/polyui/inlay_hints_test.cpp`](../../tests/unit/polyui/inlay_hints_test.cpp)
  覆盖 kind 名往返、默认产出双类 hint、四种设置组合，以及空输
  入跳过规则。

polyui 全套合计 162 例 737 条断言全部通过。
