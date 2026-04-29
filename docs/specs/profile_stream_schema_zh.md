# 性能 Profile 流 JSON Schema（`polyglot.profile.v1`）

> 由 `runtime/src/services/profile_sink.cpp` 中的静态助手
> `ProfileSink::SerializeSample()` 生成。`polyrt profile` 根据
> `ProfileSink::Open` 的 `stream_mode` 参数，输出单文档（含 `samples[]`
> 数组）或 NDJSON（每行一个样本）。

## 文档模式

```json
{
  "schema": "polyglot.profile.v1",
  "samples": [
    {
      "function": "main",
      "language": "ploy",
      "thread": "T0",
      "timestamp_ns": 0,
      "window_ns": 200000000,
      "calls": 1,
      "is_bridge": false
    }
  ],
  "frames": [
    { "language": "ploy",  "stack": ["main"],         "inclusive_ns": 5000, "self_ns": 1000, "calls": 1 },
    { "language": "python","stack": ["main", "calc"], "inclusive_ns": 4000, "self_ns": 4000, "calls": 5 }
  ],
  "hotspots": [
    { "function": "calc", "language": "python", "calls": 5, "inclusive_ns": 4000 }
  ]
}
```

| 区块 | 消费方 | 说明 |
| --- | --- | --- |
| `samples[]` | `TimelineModel` | 每个固定时间窗一条样本，作为以 `thread` 为键的泳道条形 |
| `frames[]` | `FlameTreeModel` | 按调用栈前缀聚合；缺失时 flame 视图回退到 `hotspots` |
| `hotspots[]` | `CallGraphModel::ApplyRuntimeCounts` 叠加 | 可选的便捷汇总 |

## 流式模式（NDJSON）

`polyrt profile --stream <path>` 每行写出一个 JSON 对象，对象本身就是单条
`samples[]` 项（不是外层包装文档）。IDE 端
（`ProfileSession::HandleStreamLine`）逐行追加到 TimelineModel，并以 ≥5 Hz
刷新视图。

## 字段参考（`samples[]`）

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `function` | string | 时间窗内主导帧的全限定名 |
| `language` | string | 主导帧语言 |
| `thread` | string | 逻辑线程 / 泳道 id |
| `timestamp_ns` | uint64 | 单调递增的窗口起点 |
| `window_ns` | uint64 | 窗口长度 |
| `calls` | uint64 | 窗口内累积调用次数 |
| `is_bridge` | bool | 跨语言桩事件 |

## 字段参考（`frames[]`）

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `language` | string | 帧所属宿主语言 |
| `stack` | string[] | 自顶向下的全限定名列表（根在前） |
| `inclusive_ns` | uint64 | 叶帧含子调用的耗时 |
| `self_ns` | uint64 | 叶帧本身耗时 |
| `calls` | uint64 | 叶帧进入次数 |

## 版本策略

* 新增字段为非破坏性变更（仍属 `v1`）。
* 删除或重命名字段需升级到 `polyglot.profile.v2`。
