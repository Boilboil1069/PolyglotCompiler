# 编辑器与工具链功能扩张 — 实现说明

> 对应需求 **2026-04-14-1**（功能扩张）的实现文档。
> 涵盖需求条目的全部六个部分。

---

## 1. polyui 编辑器增强

### 1.1 增强的 .ploy 补全

`CompilerService::GetPloyCompletions()` 现在返回更丰富的补全项：

- 内置关键字：`FUNC`、`CALL`、`LINK`、`PIPELINE`、`EXPORT`、`IMPORT`、`VAR`、`RETURN`、`IF`、`ELSE`、`WHILE`、`FOR`、`STRUCT`、`ENUM`、`MATCH`，以及类型名（`INT`、`FLOAT`、`STRING`、`BOOL`、`VOID`、`ANY`、`ARRAY`、`MAP`）。
- 通过 `IndexWorkspaceFile()` 从工作区已打开文件提取的符号。

### 1.2 工作区符号索引

`CompilerService::IndexWorkspaceFile()` 基于正则表达式进行多语言符号提取：

| 语言 | 匹配模式 |
|------|----------|
| ploy | `FUNC`、`STRUCT`、`ENUM`、`VAR`、`PIPELINE` 声明 |
| cpp | 函数定义、`class`、`struct`、`enum`、`namespace` |
| python | `def`、`class` |
| rust | `fn`、`struct`、`enum`、`impl`、`mod` |
| java | `class`、`interface`、`enum` + 方法 |
| csharp | `class`、`struct`、`interface`、`enum` + 方法 |

### 1.3 跨文件跳转定义

`CompilerService::FindDefinition()` 使用三级搜索将标识符解析到声明位置：

1. 当前文件符号
2. 工作区索引符号（所有已索引文件）
3. 对所有已索引源文件的正则回退搜索

返回包含文件、行、列的 `DefinitionLocation`。

### 1.4 "从模板新建" 上下文菜单

在文件浏览器中右键点击目录，显示 **"New From Template..."**。该操作展示可用模板列表（语言特定的样板代码），询问文件名，将模板写入所选目录，并在编辑器中打开新文件。

**能力边界：** 模板内容为静态。插件贡献的模板（通过 `CAP_TEMPLATE`）由 `PluginManager::GetTemplateProviders()` 解析，但尚未集成到模板对话框中。

---

## 2. 拓扑工具扩展

### 2.1 节点分组

拓扑面板工具栏包含四种分组模式的组合框：

| 模式 | 分组键 |
|------|--------|
| 无 | 不分组 |
| 语言 | `TopologyNode::language` |
| 管道 | `TopologyNode::kind == "pipeline"` 成员关系 |
| 模块 | `TopologyNode::kind` |

分组以半透明边界矩形和标签的形式在视口中可视化。

### 2.2 批量操作

三个批量按钮作用于当前选中项：

- **删除**：从场景移除选中节点及相连边，同时通过 `RemoveNodesIf` / `RemoveEdgesIf` 从底层 `TopologyGraph` 中移除。
- **高亮**：在选中节点上启动脉冲动画。
- **导出**：为选中节点构建 DOT 子图并保存到文件。

### 2.3 生成 .ploy 中的源码位置注释

`GeneratePloySrc()` 在 `SourceLoc` 数据可用时，为 LINK 指令、PIPELINE 节点头和 FUNC 声明生成 `@source file:line` 注释。

### 2.4 polytopo CLI 过滤

| 选项 | 说明 |
|------|------|
| `--view-mode=<link\|call\|pipeline>` | 按类型过滤边 |
| `--filter-language=<lang>` | 按语言过滤节点 |

---

## 3. 编译体验与可观测性

### 3.1 进度 JSON 事件

`--progress=json` 向标准输出发送 NDJSON 事件：

```json
{"event":"stage_start","stage":"frontend","index":0,"total":6}
{"event":"stage_end","stage":"frontend","index":0,"total":6,"ms":1.8}
{"event":"complete","success":true,"total_ms":27.7}
```

### 3.2 构建性能分析

编译成功后，`polyc` 在 aux 目录写入 `build_profile.bin`。该二进制文件包含 6 个连续的 `double` 值，对应各阶段毫秒计时（前端、语义、编排、桥接、后端、发射）。

### 3.3 错误摘要

当任一阶段失败时，`PrintErrorSummary()` 按 `ErrorCode` 聚合所有诊断信息并打印摘要。在 JSON 模式下，摘要为按错误码键值组织的 JSON 对象。

### 3.4 增量编译缓存

`CompilationCache`（`compilation_cache.h`）基于 FNV-1a 哈希提供文件缓存。缓存键为 `ComputeHash(source_text, settings_key)` 生成的 64 位哈希。缓存制品存储在 `.polyc_cache/` 目录下，文件名为 `<hash>.cache`。

| 命令 | 效果 |
|------|------|
| `polyc --clean-cache` | 清除所有缓存条目并退出 |

**失败场景：** 如果缓存目录为只读，`Store()` 静默失败。`HasCached()` 返回 false，编译正常进行。

---

## 4. 插件与扩展生态

### 4.1 新增 API 扩展点

四个新增能力标志和提供器结构体：

| 标志 | 提供器结构体 | 导出符号 |
|------|-------------|----------|
| `CAP_COMPLETION` | `PolyglotCompletionProvider` | `polyglot_plugin_get_completion` |
| `CAP_DIAGNOSTIC` | `PolyglotDiagnosticProvider` | `polyglot_plugin_get_diagnostic` |
| `CAP_TEMPLATE` | `PolyglotTemplateProvider` | `polyglot_plugin_get_template` |
| `CAP_TOPOLOGY_PROC` | `PolyglotTopologyProcessor` | `polyglot_plugin_get_topology_proc` |

### 4.2 版本约束

`PolyglotPluginInfo::min_host_version` 在加载时检查。`PluginManager::CheckHostVersionConstraint()` 解析 SemVer 字符串并与 `POLYGLOT_VERSION_{MAJOR,MINOR,PATCH}` 进行比较。

### 4.3 冲突检测

`PluginManager::DetectConflicts()` 返回以下情况的 `PluginConflict` 记录：

- 同一语言的重复格式化器
- 同一 `language_name` 的重复语言提供器

### 4.4 沙箱与熔断器

`SandboxPolicy` 控制：

- `call_timeout_ms`（默认：5000 毫秒）
- `memory_limit_bytes`（默认：0 / 无限制）
- `max_consecutive_failures`（默认：3）

`RecordPluginFailure()` 递增插件计数器；达到阈值时，`circuit_open` 设为 `true`。`RecordPluginSuccess()` 重置计数器。`ResetCircuitBreaker()` 清除状态和错误日志。

### 4.5 插件管理 UI

设置 → 插件页面现包括：

- 插件树中的 **加载顺序** 列
- **卸载** 按钮（从进程中完全移除插件）
- **重置熔断器** 按钮（清除熔断器状态）
- **熔断器状态** 显示（状态列中显示红色 "Breaker Open"）
- 详情面板中显示 **最后错误** 和熔断器状态
- 插件详情中显示 **最低宿主版本**

---

## 5. 测试

新增端到端测试位于 `tests/unit/e2e/expansion_features_test.cpp`：

| 测试 | 断言内容 |
|------|----------|
| 缓存哈希确定性 | 相同输入 → 相同哈希 |
| 缓存哈希差异性 | 不同输入 → 不同哈希 |
| 缓存存取/清除 | 往返正确性 |
| 新能力标志 | 验证位值和非重叠 |
| 提供器结构体零初始化 | `{}` 初始化后所有字段为 nullptr |
| 空聚合查询 | 无提供器 → 空结果 |
| 冲突检测（空） | 无插件 → 无冲突 |
| 沙箱策略默认值 | 验证默认值 |
| 沙箱策略往返 | 设置/获取保持一致 |
| 版本头一致性 | `POLYGLOT_VERSION_STRING` 与组件匹配 |

所有测试使用行为断言（REQUIRE / REQUIRE_FALSE），无 `REQUIRE(true)` 占位。

---

## 6. 常见失败场景与排查

| 场景 | 症状 | 解决方法 |
|------|------|----------|
| 插件要求更新的宿主 | 加载被拒绝，提示 "requires host >= X.Y.Z" | 升级 PolyglotCompiler 或使用旧版本插件 |
| 同一语言两个格式化器 | `DetectConflicts()` 返回冲突 | 禁用其中一个冲突插件 |
| 插件回调持续崩溃 | 3 次失败后熔断器触发 | 检查 `GetPluginLastError()`，修复插件后"重置熔断器" |
| 缓存目录只读 | 缓存存储静默失败，无性能提升 | 修复目录权限或设置其他缓存目录 |
| `--progress=json` 输出与 stderr 混杂 | JSON 事件发送到 stdout，进度文本发送到 stderr | 分别重定向 stdout/stderr：`polyc ... 2>/dev/null` |
