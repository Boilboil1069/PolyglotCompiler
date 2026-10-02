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
| `language` | string | `poly`, `cpp`, `python`, `bridge`, ... |
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

## Argument metadata and callsite identity

Each direct call instruction now emits its own edge with `callsite_id`, `block`,
`result`, `result_type`, and `arguments`. Repeated calls to one callee remain
separate. Each argument records its SSA `value`, known `type`, destination
`parameter`, `expected_type`, and `transfer` (`identity`, `conversion_required`,
or `unresolved`). A differing type describes a requirement; it does not claim
that a runtime conversion has been inserted. An unavailable signature or SSA
type is explicitly `unknown`.

Nodes include `signature_known`, `return_type`, and `parameters` (index, name,
type). A direct callee that is absent from the IR function table still appears as
an external node with unknown signature, so every edge endpoint resolves.
Indirect calls cannot be resolved to a function node and are omitted. This graph
is an inventory of the supplied IR, not a runtime reachability proof.

The Poly source topology view is separate: `polyglot.topology.v2` contains
per-call input/output ports, literal and expression nodes, explicit `CONVERT`
nodes, and function input/result boundaries. Edge `relation` distinguishes
`value`, `binding`, and `order`; stage order is never labeled as value transfer.
