#pragma once

#include <algorithm>
#include "frontends/common/include/diagnostics.h"
#include "frontends/common/include/native_builtins.h"
#include "middle/include/ir/ir_builder.h"

namespace polyglot::tools {

// Executables use a checked source entry, independently of declaration order.
// Object-only compilation intentionally does not require an entry point.
inline bool PrepareNativeEntry(ir::IRContext &ctx, const std::string &language,
                               const std::string &requested,
                               frontends::Diagnostics &diagnostics) {
  auto fail = [&](const std::string &message) {
    diagnostics.Report(core::SourceLoc{"<entry>", 1, 1}, message);
    return false;
  };
  std::vector<ir::Function *> candidates;
  for (const auto &fn : ctx.Functions()) {
    if (!fn || fn->is_external || fn->blocks.empty()) continue;
    if (fn->name == "__polyc_entry")
      return fail("reserved executable entry name: __polyc_entry");
    bool match = fn->name == requested;
    if (requested.empty()) {
      match = fn->name == "main" || fn->name == "__ploy_main";
      const std::string suffix = language == "dotnet" ? "::Main" : "::main";
      if (language == "java" || language == "dotnet")
        match = fn->name.ends_with(suffix);
    }
    if (match) candidates.push_back(fn.get());
  }
  if (candidates.size() != 1)
    return fail(candidates.empty()
                    ? "no executable entry found; define main (C#: static Main), use --entry=<symbol>, or compile with -c"
                    : "ambiguous executable entry; select one with --entry=<qualified-symbol>");
  const auto *entry = candidates.front();
  const bool c_args = entry->param_types.size() == 2 &&
      (entry->param_types[0].kind == ir::IRTypeKind::kI32 || entry->param_types[0].kind == ir::IRTypeKind::kI64) &&
      entry->param_types[1].kind == ir::IRTypeKind::kPointer;
  const bool managed_args = (language == "java" || language == "dotnet") &&
      entry->param_types.size() == 1 && entry->param_types[0].kind == ir::IRTypeKind::kPointer;
  if (!entry->params.empty() && !c_args && !managed_args)
    return fail("native entry '" + entry->name + "' must have no parameters or a supported argc/argv signature");
  bool uses_args = c_args || managed_args;
  for (const auto &g : ctx.Globals()) uses_args |= g->name == "__polyc_argc";
  if (uses_args) frontends::EnsureNativeArguments(ctx);
  const auto kind = entry->ret_type.kind;
  if (kind != ir::IRTypeKind::kVoid && kind != ir::IRTypeKind::kI1 &&
      kind != ir::IRTypeKind::kI8 && kind != ir::IRTypeKind::kI16 &&
      kind != ir::IRTypeKind::kI32 && kind != ir::IRTypeKind::kI64)
    return fail("native entry '" + entry->name + "' must return an integer, boolean, or void");
  std::vector<std::pair<std::string, ir::IRType>> parameters;
  if (uses_args) parameters = {{"native_argc", ir::IRType::I64()}, {"native_argv", ir::IRType::I64()}};
  auto wrapper = ctx.CreateFunction("__polyc_entry", ir::IRType::I64(), parameters);
  ir::IRBuilder builder(ctx);
  builder.SetCurrentFunction(wrapper);
  builder.SetInsertPoint(builder.CreateBlock("entry"));
  std::vector<std::string> arguments;
  if (uses_args) {
    builder.MakeStore("__polyc_argc", "native_argc");
    builder.MakeStore("__polyc_argv", "native_argv");
    if (c_args) arguments = {"native_argc", "native_argv"};
    if (managed_args) arguments = {builder.MakeBinary(ir::BinaryInstruction::Op::kAdd, "native_argv", "8", "")->name};
  }
  auto result = builder.MakeCall(entry->name, arguments, entry->ret_type, "exit_status");
  builder.MakeReturn(kind == ir::IRTypeKind::kVoid ? "0" : result->name);
  auto &functions = ctx.Functions();
  std::rotate(functions.begin(), functions.end() - 1, functions.end());
  return true;
}
} // namespace polyglot::tools
