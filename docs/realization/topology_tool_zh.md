# 拓扑分析工具 — 实现细节

## 1. 架构概述

拓扑分析工具（`polytopo`）为 `.ploy` 跨语言程序提供类似 Simulink 的函数输入/输出识别、链接检查和验证功能。它将每个函数、构造函数、方法、管线和跨语言调用建模为带有类型化输入/输出**端口（Port）**的**拓扑节点（TopologyNode）**。端口之间的连接表示为带有类型兼容性状态的**拓扑边（TopologyEdge）**。

系统由四个核心库模块、一个命令行可执行文件和一个集成到 PolyglotCompiler IDE（`polyui`）中的基于 Qt 的图形面板组成。

### 1.1 组件映射

```
tools/polytopo/
├── include/
│   ├── topology_graph.h       # 核心数据结构：Port、TopologyNode、TopologyEdge、TopologyGraph
│   ├── topology_analyzer.h    # 使用 PloySema 进行类型解析的 AST 到图构建器
│   ├── topology_validator.h   # 边类型验证、环路检测、未连接端口警告
│   └── topology_printer.h     # 输出格式化器：文本、DOT（Graphviz）、JSON、摘要
└── src/
    ├── topology_graph.cpp     # 图操作：增删查节点和边、拓扑排序、环路检测
    ├── topology_analyzer.cpp  # 两遍 AST 遍历：注册节点，然后遍历函数体寻找边
    ├── topology_validator.cpp # 五遍验证：类型、端口、参数、环路、语言兼容性
    ├── topology_printer.cpp   # 彩色文本、带记录节点的 DOT、JSON、带统计的摘要
    └── polytopo.cpp           # CLI 入口点，参数解析和 5 阶段流水线

tools/ui/common/
├── include/
│   └── topology_panel.h      # 基于 QGraphicsView 的拓扑可视化面板
└── src/
    └── topology_panel.cpp     # 交互式节点渲染、边绘制、验证覆盖、导出

tests/unit/tools/
└── topology_test.cpp          # Catch2 单元测试：图、分析器、验证器、打印器
```

### 1.2 库目标

`topo_lib` CMake 库（`tools/CMakeLists.txt`）链接 `polyglot_common` 和 `frontend_ploy`，提供：

- `TopologyGraph` — 图数据结构，含拓扑排序（Kahn 算法）和 DFS 环路检测。
- `TopologyAnalyzer` — 从已解析并经过语义分析的 ploy AST 构建图。
- `TopologyValidator` — 验证类型兼容性、端口连通性、参数数量和语言互操作约束。
- `TopologyPrinter` — 以多种格式渲染图。

## 2. 核心数据结构

### 2.1 端口（Port）

每个端口表示函数节点上的一个类型化参数（输入）或返回值（输出）：

| 字段        | 类型            | 描述                                   |
|-------------|-----------------|----------------------------------------|
| `name`      | `std::string`   | 参数或返回值名称                       |
| `direction` | `Direction`     | `kInput` 或 `kOutput`                  |
| `type`      | `core::Type`    | ploy 类型系统中的语义类型              |
| `language`  | `std::string`   | 所属语言（如 `"cpp"`、`"python"`）     |
| `index`     | `int`           | 在节点中的位置索引                     |
| `id`        | `uint64_t`      | 在图构建时分配的唯一标识符             |

### 2.2 拓扑节点（TopologyNode）

表示拓扑图中的一个可调用单元：

| 类型             | 描述                                       |
|------------------|--------------------------------------------|
| `kFunction`      | 独立 FUNC 声明或 LINK 目标                 |
| `kConstructor`   | NEW(...) 类实例化                          |
| `kMethod`        | METHOD(...) 对象方法调用                   |
| `kPipeline`      | PIPELINE 块                                |
| `kMapFunc`       | MAP_FUNC 类型转换辅助器                    |
| `kExternalCall`  | 跨语言 CALL(lang, func, ...)               |

### 2.3 拓扑边（TopologyEdge）

将输出端口连接到输入端口，包含兼容性状态：

| 状态               | 含义                                     |
|--------------------|------------------------------------------|
| `kValid`           | 类型直接兼容                             |
| `kImplicitConvert` | 需要隐式类型转换（拓宽/数值提升）        |
| `kExplicitConvert` | 需要 MAP_TYPE 或显式 CONVERT             |
| `kIncompatible`    | 类型根本不兼容 — 报错                    |
| `kUnknown`         | 一个或两个类型为 `Any` / 未解析          |

## 3. 分析流水线

### 3.1 拓扑分析器（两遍遍历）

**第一遍 — 注册顶层节点：**

遍历模块 AST 中的每个顶层声明：
- `FuncDecl` → 创建 `kFunction` 节点，包含输入端口（参数）和输出端口（返回类型）。
- `LinkDecl` → 创建调用方节点和被调用方节点（标记为 `is_linked`），加上它们之间的边。
- `PipelineDecl` → 创建 `kPipeline` 节点；递归进入管线体注册包含的函数。
- `MapFuncDecl` → 创建 `kMapFunc` 节点。
- `ExtendDecl` → 为扩展中的每个方法创建 `kMethod` 节点。

**第二遍 — 遍历函数体寻找边：**

在每个函数体内，分析器解析：
- `CrossLangCallExpr` → 创建 `kExternalCall` 节点（如果尚不存在）和从调用方到被调用方的边。
- `NewExpr` → 创建 `kConstructor` 节点和相应的边。
- `MethodCallExpr` → 解析接收者并创建边。
- `CallExpr` → 解析被调用函数并创建边。
- `VarDecl` → 跟踪通过变量的数据流绑定（变量 → 生产者节点 + 端口）。
- `Identifier` → 查找变量绑定以传播生产者信息。

### 3.2 拓扑验证器（五遍验证）

1. **边类型验证** — 检查每条边的源端口和目标端口类型的兼容性。
2. **未连接端口检测** — 对没有传入边的输入端口和没有传出边的输出端口发出警告。
3. **参数数量验证** — 确保调用方提供了正确数量的参数。
4. **环路检测** — 使用 DFS 检测并报告循环依赖。
5. **语言兼容性** — 标记特定语言对之间的已知互操作问题。

### 3.3 类型兼容性规则

验证器使用多级类型兼容性检查：

| 源类型 → 目标类型              | 状态               |
|--------------------------------|--------------------|
| 相同类型                       | `kValid`           |
| 任一为 `Any`                   | `kUnknown`         |
| Int → Float                    | `kImplicitConvert` |
| Bool → Int                     | `kImplicitConvert` |
| 相同容器、相同元素             | `kValid`           |
| 名称匹配的类                   | `kValid`           |
| 其他                           | `kIncompatible`    |

## 4. CLI 工具 — `polytopo`

### 4.1 使用方法

```
polytopo [选项] <文件.ploy>

选项：
  --format <text|dot|json|summary>   输出格式（默认：text）
  --validate                         运行验证检查
  --strict                           将 Any 类型视为错误
  --no-color                         禁用 ANSI 颜色代码
  --show-locations                   在输出中包含源位置
  --output <文件>                    写入文件而非标准输出
  --compact                          紧凑输出格式
  --allow-cycles                     不将环路报告为错误
  --dot-horizontal                   使用从左到右的 DOT 布局
  --help                             显示用法
```

### 4.2 流水线

CLI 遵循 5 阶段流水线：

```
[1/5] 词法分析    ─ PloyLexer 对源代码进行分词
[2/5] 语法分析    ─ PloyParser 构建 AST
[3/5] 语义分析    ─ PloySema 执行语义分析
[4/5] 构建拓扑    ─ TopologyAnalyzer 构建图
[5/5] 验证        ─ TopologyValidator 检查图（可选）
```

### 4.3 输出格式

- **text** — 带有彩色端口名称和类型的 ASCII 框绘拓扑。
- **dot** — Graphviz DOT 格式，带有记录形节点和端口级边。
- **json** — 包含完整节点/边元数据的机器可读 JSON。
- **summary** — 紧凑的统计概要（节点数量、边状态分布、语言分布）。

## 5. GUI 面板 — TopologyPanel

`TopologyPanel`（通过 `PanelManager` 集成到 `polyui`）提供：

- **交互式图可视化**，使用 `QGraphicsView` / `QGraphicsScene`。
- **节点渲染**，带有颜色编码的语言标签、输入/输出端口圆点和框绘标签。
- **边渲染**，带有按兼容性状态着色的三次贝塞尔曲线。
- **验证覆盖** — 有错误的节点以红色边框高亮显示。
- **导出** — 从工具栏导出 DOT、JSON 和 PNG。
- **缩放/平移/适配** — 标准视口控制。
- **布局模式** — 自上而下和从左到右的网格布局。
- **详情面板** — 选择节点时显示属性的 `QTreeWidget`。
- **诊断面板** — 显示验证消息的 `QPlainTextEdit`。

### 5.1 集成

面板在 `MainWindow::SetupDockWidgets()` 中通过 `PanelManager` 注册：

```cpp
panel_manager_->RegisterPanel("topology", topology_panel_, "Topology");
```

切换：**视图 → 切换拓扑面板**（`Ctrl+Shift+T`）。

## 6. 测试

单元测试位于 `tests/unit/tools/topology_test.cpp`，注册为 `test_topology` CTest 目标：

| 测试套件              | 覆盖范围                                               |
|-----------------------|--------------------------------------------------------|
| `[topology][graph]`   | 节点/边增删查、按名查找、根/叶、拓扑排序、环路检测、语言分布 |
| `[topology][validator]`| 有效边通过、环路检测报错                               |
| `[topology][printer]` | 文本/DOT/JSON/摘要输出格式验证                         |
| `[topology][analyzer]`| FUNC、LINK、PIPELINE、CALL 端到端从 .ploy 源代码       |

## 7. 后续工作

- 力导向自动布局（替换简单网格）。
- 通过拖拽进行交互式边创建/删除。
- 源 `.ploy` 文件变更时实时重新加载。
- 断点集成：在调试会话期间高亮当前执行的节点。
- 悬停在端口上时基于工具提示的类型详情检查。
