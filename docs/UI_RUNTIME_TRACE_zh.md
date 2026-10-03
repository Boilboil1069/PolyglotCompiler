# 在 UI 中查看跨语言调用的真实运行记录

UI 使用与 `polyc` 相同的外部签名提取器。它先读取本地外部源码和项目中的 vendored PACKAGE，再把签名注入 Poly 语义分析。编辑器诊断、函数文档里的 **Compiler types** 和数据流图使用同一份类型信息。已经解析到返回类型的 `CALL` 不再因为分析顺序错误而显示未知类型；缺少函数、参数数量不符和外部源码错误仍会报告。

当前原生追踪支持 macOS/Linux 的 ARM64 Poly 可执行文件。Windows 和 x86 的静态工作区、源码文档和图视图仍可用；本轮没有实现这些平台的原生运行追踪。

## 运行订单风险示例

1. 打开 `examples/order_risk_analyzer` 文件夹和 `order_risk.poly`。
2. 工具栏选本机支持的原生目标。开启第二行的 **Trace CALLs**。
3. 参数栏可输入 `data/orders.csv /tmp/order-results.txt`。参数支持引号；相对路径以源码目录为基准。
4. 点击 **Compile & Run**。UI 调用 `polyc --trace-calls=<本次会话路径>`，编译成功后启动实际可执行文件。
5. 图右侧或下方的 **Runtime samples** 页显示运行记录。选中任意行，可在参数表查看实际参数、返回值、ABI 类型和耗时；源码中对应 `CALL` 行高亮，图卡片和输入/输出端口显示该次记录。

![订单风险示例：源码定位、记录卡片和真实参数表](images/native-runtime-2026-10-03.png)

截图来自实际运行：8 个订单执行了 128 次跨语言调用，16 个静态调用点均唯一关联到图卡片。选中实例的参数为 `unit_price=18`、`quantity=7`，返回值为 `126`。

每一行是一回真实调用，`Instance` 是独立调用编号。同一个 `CALL` 执行多次会有多行，不会合并成一个假定值。图上的 `RECORDED #…` 明确表示已记录的样本。

## 两个独立开关

- **Trace CALLs** 决定下一次编译与运行是否插入追踪。关闭后再次 **Compile & Run**，编译器不接收追踪参数。
- **Show sample on source and graph** 决定是否把已选样本叠加到源码和图上。关闭后恢复静态端口类型，记录表仍保留。

一次新的运行会清空上一次 UI 会话，并使用新的临时追踪目录。当前运行生成的临时记录会在 UI 退出或替换会话后清理。需要长期保存时可直接使用编译器参数指定文件，再用 **Open trace…** 打开；`calls.jsonl` 旁边必须同时保留 `calls.jsonl.sites.json`。

```sh
build-release/polyc examples/order_risk_analyzer/order_risk.poly \
  --arch=arm64 -O0 -o /tmp/order-risk \
  --trace-calls=/tmp/order-risk-calls.jsonl
/tmp/order-risk examples/order_risk_analyzer/data/orders.csv /tmp/order-results.txt
```

具体编译目标和可执行文件路径以本机构建为准。

## 数据来源与范围

编译时的 `.sites.json` 保存原始 Poly 文件、行、列、被调用函数和 ABI 参数/返回类型。运行时 JSONL 保存实际参数和返回值的原始位，以及开始与结束时钟。64 位数字用十进制字符串保存，避免 JSON 浮点数损失大整数精度。原始位允许有符号十进制表示，UI 按相同的 64 位模式还原；编号和时钟为无符号。

UI 用文件、行、列和函数名共同定位图卡片。若不能唯一匹配，记录仍可查看，但不会随意贴到另一张卡片上。

- 整数按 ABI 宽度和有符号性还原；浮点数按位解码。
- 指针只采集地址位，不解引用。字符串、对象等指针指向的内容显示不可用，不能由地址推测内容。
- 耗时来自 `(结束时钟 − 开始时钟) / 时钟频率`，以微秒显示。追踪本身会影响运行时间，这些数值用于观察调用，不能替代关闭追踪后的正式性能测量。
- 当前记录范围是单线程中的跨语言 `CALL` 边界。`Parent traced instance` 是最近一层尚未完成的受追踪调用；没有被追踪的 Poly 递归不会被伪造为父记录。
- 记录按完成顺序显示。嵌套调用的子记录可以先于父记录到达。
- 缺少 sidecar、无效 JSON、未知调用编号、重复实例编号、运行文件不可读等错误会直接出现在面板里。UI 不用缺失值补出虚构结果。
- 当前检查上限为 32 MiB 记录文件、50,000 个样本和 8 MiB sidecar。超过上限会明确报错。
- 静态图及展开的内部图仍表达源码关系；运行叠加应用于主图当前匹配的外部调用卡片。编辑源码不会重新执行程序，已有行始终代表该次运行。

外部源码尚未保存时，文档会提示先保存以刷新编译器类型；源码预览仍可展示打开的编辑缓冲区。跨语言内联文档与右键定义导航见 [工作区使用说明](UI_CROSS_LANGUAGE_WORKSPACE_zh.md)，静态边、端口和转换颜色见 [数据流图说明](UI_VALUE_FLOW_zh.md)。

## 可重复的 UI 验证

以下命令使用真实窗口组件完成编译、启动原生产物、读取记录、定位源码与卡片，并检查关闭运行叠加后恢复静态端口。程序必须正常返回 0。

```sh
build-release/polyui.app/Contents/MacOS/polyui \
  --headless --ui-trace-smoke \
  --folder examples/order_risk_analyzer \
  --file examples/order_risk_analyzer/order_risk.poly \
  --run-args 'data/orders.csv /tmp/polyui-order-results.txt' \
  --screenshot /tmp/polyui-runtime.png
```

Linux ARM64 使用对应的 `polyui` 可执行文件路径。加 `--trace-file <已经实际执行生成的JSONL>` 可验证记录导入和显示；报告中的 `compiledAndRan` 会为 `false`，避免把导入验证当作本次重新编译运行。

### 本次验证记录（2026-10-03）

- 追踪模型：5 项测试、52 个断言通过，覆盖有符号原始位、浮点、大整数、重复实例和错误记录。
- Qt 图与交互：40 项测试、561 个断言通过，包含真实项目类型解析、同一行调用区分、叠加开关和布局后拖拽连线。
- 整体 UI 门禁：C++ 文档与导航、Python 文档与导航、订单原生追踪三项通过；程序参数中的空格路径也经过实际运行。
- 额外导入实际负数/浮点记录：8 个样本、6 个调用点均能解析与关联。导入验证的 `compiledAndRan=false`、`exitCode=null`，没有把导入当作重新执行。

本机结构化结果和日志位于 `build-release/ui-validation/2026-10-03/`。正式性能评估应使用项目的独立性能脚本；UI 截图验证不等于性能重复实验。
