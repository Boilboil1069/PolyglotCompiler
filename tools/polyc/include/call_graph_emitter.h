/**
 * @file     call_graph_emitter.h
 * @brief    Emit the static cross-language call graph as JSON
 *
 * @ingroup  Tool / polyc
 * @author   Manning Cyrus
 * @date     2026-04-29
 */
#pragma once

#include <string>

#include "middle/include/ir/ir_context.h"

namespace polyglot::tools::polyc {

// Walk the supplied IR context and emit a JSON document describing
// every call site reachable from the program's defined functions.
// The schema is documented in docs/specs/call_graph_schema_en.md and
// docs/specs/call_graph_schema_zh.md; consumers (polyui call analyzer,
// CI gates) treat unknown fields as forward-compatible additions.
//
// The function returns the serialized document directly; the caller is
// expected to write it to disk or hand it to the IDE.
std::string EmitCallGraphJson(const ir::IRContext &context, const std::string &source_path);

// Emit a parallel "profile-symbols" document that contains the stable
// id -> qualified name mapping the runtime call_trace records reference
// at runtime.  Saved alongside the call graph so the IDE can reconcile
// dynamic samples against the static graph without re-parsing IR.
std::string EmitProfileSymbolsJson(const ir::IRContext &context,
                                   const std::string &source_path);

} // namespace polyglot::tools::polyc
