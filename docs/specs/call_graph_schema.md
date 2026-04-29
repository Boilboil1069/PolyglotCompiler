# Call-Graph JSON Schema (`polyglot.callgraph.v1`)

> Emitted by `polyc --emit=call-graph:<path>`.  Implementation lives in
> `tools/polyc/src/call_graph_emitter.cpp`.

## Top-level structure

```json
{
  "schema": "polyglot.callgraph.v1",
  "source": "<source file path>",
  "nodes": [...],
  "edges": [...]
}
```

## `nodes[]` entries

| Field | Type | Description |
| --- | --- | --- |
| `id` | integer | Stable encounter-order id used by edges. |
| `name` | string | Fully qualified function name. |
| `language` | string | `ploy`, `cpp`, `python`, `bridge`, ... |
| `is_external` | bool | Function is declared but not defined in this TU. |
| `is_bridge_stub` | bool | Generated cross-language marshalling stub. |
| `block_count` | integer | Number of basic blocks in the IR function. |

When the loader (`ProfileSession::ParseCallGraphDocument`) encounters
`id` as a numeric value, it falls back to `name` for the
`CallGraphModel::id_to_row_` lookup.  Future v2 may switch to string ids
without breaking compatibility.

## `edges[]` entries

| Field | Type | Description |
| --- | --- | --- |
| `from` | integer | Caller node `id`. |
| `to` | integer | Callee node `id`. May reference a synthesised external node. |
| `callee` | string | Original mangled callee name (debug aid). |
| `from_language` / `to_language` | string | Optional; back-filled from the node table when absent. |

## Forward compatibility

* Consumers must treat unknown fields as opaque additions.
* The emitter never reorders fields between minor versions.
* Bumping `polyglot.callgraph` to `v2` is reserved for a breaking layout
  change (e.g. nested per-call-site metadata).
