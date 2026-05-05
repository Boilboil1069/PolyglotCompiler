# 测试浏览器 / Inline Run-Test / 覆盖率视图

## 目标

把 CTest、pytest、cargo test、JUnit、xUnit、NUnit 各类套件统一聚合到
polyui 中的「测试浏览器」树视图；编辑器侧栏直接显示 ▶ 运行 / 🐞 调试
CodeLens；从五种主流报告格式加载覆盖率，在行号槽位上以颜色条与百分比
呈现。

## 组件

| 模块 | 职责 |
| --- | --- |
| [`tools/ui/common/testing/test_model.h`](../../tools/ui/common/testing/test_model.h) | `TestNode` 层级结构、状态记录、失败优先排序、汇总统计。 |
| [`tools/ui/common/testing/test_model.cpp`](../../tools/ui/common/testing/test_model.cpp) | 五种报告解析器：CTest CDash XML、JUnit/pytest XML、cargo libtest JSON 行流、xUnit v2、NUnit 3。 |
| [`tools/ui/common/testing/inline_test_lens.h`](../../tools/ui/common/testing/inline_test_lens.h) | 行内 CodeLens 描述符（`Lens`），含 Run+Debug 动作、按语言扩展的检测器注册表、失败信息回填接口。 |
| [`tools/ui/common/testing/inline_test_lens.cpp`](../../tools/ui/common/testing/inline_test_lens.cpp) | Catch2、pytest、cargo（Rust `#[test]`）、JUnit（`@Test`）、xUnit（`[Fact]`/`[Theory]`）、NUnit（`[Test]`）的内建检测器。 |
| [`tools/ui/common/testing/coverage_model.h`](../../tools/ui/common/testing/coverage_model.h) | `FileCoverage` 值类型、工作区汇总、阈值过滤。 |
| [`tools/ui/common/testing/coverage_model.cpp`](../../tools/ui/common/testing/coverage_model.cpp) | lcov、Cobertura/coverage.py、cargo-tarpaulin JSON、dotnet coverlet JSON 解析器。 |

## 流水线

### 发现

1. 运行器产出报告（CTest XML / JUnit XML / cargo libtest JSON / xUnit XML / NUnit XML）。
2. 调用对应的 `Parse*Report` 得到扁平的 `TestNode` 向量。
3. 通过 `Upsert` 注入 `TestModel`，按插入顺序保存以保证渲染稳定。

### 行内运行

1. 文档打开或变更时，IDE 调用 `InlineTestLens::ComputeForFile`。
2. 命中文件扩展名的检测器扫描标准测试声明，每条匹配产出一个带有
   `kRun` 与 `kDebug` 动作的 `Lens`。
3. 运行结束后调用 `RecordFailure(file, line, message)`，把诊断信息写入
   缓存的 lens 以供行内渲染。

### 覆盖率

1. `CoverageModel::Load(text)` 默认嗅探前 2 KB 选择解析器；调用方亦可
   强制指定格式。
2. 每个文件得到一份 `FileCoverage`，内部维护「行号 → 命中次数」映射。
3. UI 据 `line_hits` 绘制行号槽位条，由 `Files()` 列出文件树，由
   `BelowThreshold(threshold)` 触发阈值告警。

## 测试

* [`tests/unit/polyui/test_model_test.cpp`](../../tests/unit/polyui/test_model_test.cpp) — 模型语义与五种报告解析器。
* [`tests/unit/polyui/inline_test_lens_test.cpp`](../../tests/unit/polyui/inline_test_lens_test.cpp) — 五种框架的检测器覆盖与失败信息回填。
* [`tests/unit/polyui/coverage_model_test.cpp`](../../tests/unit/polyui/coverage_model_test.cpp) — 格式嗅探、五种解析器、阈值过滤、总体百分比。
