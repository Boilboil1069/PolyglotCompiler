# 编译流水线 Inspector —— IR Viewer / Diff + Asm Viewer + 源码↔汇编联动

## 目标

为用户提供透明的、类 Compiler Explorer 的编译过程视图：哪些阶段
运行过、各自耗时多少、产出了哪些产物、IR 在优化前后形态如何、最
终反汇编的每一条指令对应哪一行源码。

## 组件

| 组件 | 头文件 | 作用 |
| --- | --- | --- |
| `PipelineRun` | [`tools/ui/common/pipeline/pipeline_inspector.h`](../../tools/ui/common/pipeline/pipeline_inspector.h) | 加载 `aux/pipeline.json`；六个标准阶段，每阶段含产物清单、总耗时与按最长阶段归一化的耗时直方图。 |
| `IrModule` | [`tools/ui/common/pipeline/ir_viewer.h`](../../tools/ui/common/pipeline/ir_viewer.h) | 把 LLVM / MLIR 风格 IR 文本解析为可折叠的函数与基本块树，按行号索引。 |
| `DiffFunctions` | 同上 | 在两个 `IrFunction` 函数体之间执行行级 LCS diff；输出按 `equal/added/removed` 标注，并带左右行号便于 side-by-side 渲染。 |
| `LineBindingTable` | 同上 | 维护源码 ↔ IR ↔ 产物的三向绑定；从任一轴查询都返回匹配元组。 |
| `AsmModule` | [`tools/ui/common/pipeline/asm_viewer.h`](../../tools/ui/common/pipeline/asm_viewer.h) | 解析 x86_64 / arm64 / wasm 的反汇编文本；识别 DWARF `.file`/`.loc` 指令以及 polyasm 内联的 `; src=文件:行` 提示；提供 `AsmForSource` / `SourceForAsm` 实现双向跳转。 |

## 流程

* **阶段时间线。** polyc 每次构建后产出 `aux/pipeline.json`；
  `PipelineRun::LoadAux` 填入六个阶段，`Histogram()` 返回
  `(stage, ratio∈[0,1])` 列表（以最长阶段为基准归一化），面板
  即可一次绘制水平条形图。
* **IR 折叠。** `IrModule::Parse` 逐行扫描转储；`define <ret> @name(...)`
  与 `func @name(...)` 打开新函数；以标识符开头的 `label:` 行
  打开新基本块；`}` 闭合当前块。每个节点都带 `start_line` /
  `end_line`，便于 IDE 把折叠区间锚定到源码。
* **IR diff。** `DiffFunctions(left, right)` 将两侧函数体压成行
  数组、跑经典 O(NM) LCS，再回溯输出 `equal/added/removed` 记
  录及左右行号——直接对接 side-by-side 渲染。
* **Asm 解析。** `AsmModule::Parse` 兼容三种输入：DWARF `.file <id>
  "name"` + `.loc <id> <line>` 指令为后续指令建立滚动的 `(文件,
  行)`；指令上的 `; src=<文件>:<行>` 或 `// src=...` 内联注释会
  覆盖滚动状态；裸标识符 `label:` 行标识函数边界。解析器按目标
  架构区分（x86_64、arm64、wasm），并保留当前目标供下游过滤。
* **双向跳转。** `AsmModule::AsmForSource(文件, 行)` 返回该源行
  对应的所有指令；`AsmModule::SourceForAsm(函数, 行)` 返回某
  asm 行的源位置——二者共同支撑 asm 视图的同步光标与 hover 高亮。

## 测试

* [`tests/unit/polyui/pipeline_inspector_test.cpp`](../../tests/unit/polyui/pipeline_inspector_test.cpp)
  验证阶段名往返、覆盖六阶段的 `aux/pipeline.json` 摄入、总耗时
  累加与直方图归一化。
* [`tests/unit/polyui/ir_viewer_test.cpp`](../../tests/unit/polyui/ir_viewer_test.cpp)
  解析多函数 IR 转储（基本块边界不计入函数体），断言 diff 产出
  三种行类型，并校验三向绑定查询。
* [`tests/unit/polyui/asm_viewer_test.cpp`](../../tests/unit/polyui/asm_viewer_test.cpp)
  覆盖目标名往返、`.file`/`.loc` 驱动的绑定、polyasm 内联 `; src=`
  绑定，以及 wasm 目标路径。

polyui 全套合计 149 例 678 条断言全部通过。
