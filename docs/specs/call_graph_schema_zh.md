# 调用图 JSON Schema（`polyglot.callgraph.v1`）

> 由 `polyc --emit=call-graph:<path>` 生成；实现位于
> `tools/polyc/src/call_graph_emitter.cpp`。

## 顶层结构

```json
{
  "schema": "polyglot.callgraph.v1",
  "source": "<源文件路径>",
  "nodes": [...],
  "edges": [...]
}
```

## `nodes[]` 字段

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `id` | integer | 按出现顺序的稳定 id，被 edges 引用 |
| `name` | string | 函数全限定名 |
| `language` | string | `poly`, `cpp`, `python`, `bridge`, … |
| `is_external` | bool | 仅声明、未在本 TU 定义 |
| `is_bridge_stub` | bool | 自动生成的跨语言桩 |
| `block_count` | integer | IR 中基本块数量 |

加载器（`ProfileSession::ParseCallGraphDocument`）若发现 `id` 为数值，会
退化使用 `name` 作为 `CallGraphModel::id_to_row_` 的键。未来 v2 可改为字符串
id 而不破坏兼容性。

## `edges[]` 字段

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `from` | integer | 调用者节点 `id` |
| `to` | integer | 被调者节点 `id`，可指向自动生成的外部节点 |
| `callee` | string | 原始 mangled 名（调试用） |
| `from_language` / `to_language` | string | 可选，缺省时由节点表回填 |

## 前向兼容性

* 消费者必须将未知字段视作不透明的扩展。
* 发射器在小版本之间不会调整字段顺序。
* `polyglot.callgraph.v2` 保留给破坏性的布局变更（例如增加每调用点的嵌套元数据）。

## 参数信息与每次调用的身份

每个直接调用指令保留独立的 `callsite_id`，以及 `block`、`result`、
`result_type` 和 `arguments`。参数记录 SSA 值名、实际可得类型、目标参数名、
期望类型，以及 `identity`、`conversion_required` 或 `unresolved` 状态。
类型不同只表示需要转换，不代表运行时已插入转换。缺少签名或类型时明确写
`unknown`。节点增加 `signature_known`、`return_type` 与 `parameters`；
未声明的直接调用目标也有外部节点，所有边端点都能解析。

这是所提供 IR 的静态清单，不是运行时可达性证明。无法确定目标的间接调用
不进入节点图。多次调用同一函数保留多条边；调用关系查询去重。

Poly 源码图使用独立的 `polyglot.topology.v2`：每次调用有自己的输入输出端口，
常量、运算、显式 `CONVERT` 和函数输入/结果边界都有独立节点。边的 `relation`
区分值依赖（`value`）、函数绑定（`binding`）与声明顺序（`order`）。
声明顺序不表示存在值传递。图中展示静态类型与表达式关系，不显示虚构的运行值。
