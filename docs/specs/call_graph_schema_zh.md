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
| `language` | string | `ploy`, `cpp`, `python`, `bridge`, … |
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
