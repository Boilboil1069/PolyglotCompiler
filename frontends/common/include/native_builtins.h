#pragma once

#include "frontends/common/include/sema_context.h"
#include "middle/include/ir/ir_builder.h"

namespace polyglot::frontends {
// Explicit polyc native APIs. These names are reserved, rather than silently
// pretending to implement a language's standard library or object runtime.
enum class NativeType { kInt, kFloat, kString, kVoid };
struct NativeBuiltin {
  const char *name;
  const char *symbol;
  NativeType result;
  std::vector<NativeType> params;
};
inline const std::vector<NativeBuiltin> &NativeBuiltins() {
  using T = NativeType;
  static const std::vector<NativeBuiltin> api = {
      {"print_i64", "polyrt_print_i64", T::kVoid, {T::kInt}},
      {"print_f64", "polyrt_print_f64", T::kVoid, {T::kFloat}},
      {"print_text", "polyrt_write_text", T::kVoid, {T::kString}},
      {"file_open_ints", "polyrt_open_read", T::kInt, {T::kString}},
      {"file_open_write", "polyrt_open_write", T::kInt, {T::kString}},
      {"file_next_int", "polyrt_read_i64_or", T::kInt, {T::kInt,T::kInt}},
      {"file_write_text", "polyrt_write_text", T::kInt, {T::kInt,T::kString}},
      {"file_write_int", "polyrt_write_i64", T::kInt, {T::kInt,T::kInt,T::kInt}},
      {"file_close", "polyrt_close_read", T::kInt, {T::kInt}},
      {"array_new", "polyrt_array_new", T::kInt, {T::kInt}},
      {"array_len", "polyrt_array_len", T::kInt, {T::kInt}},
      {"array_get", "polyrt_array_get", T::kInt, {T::kInt,T::kInt,T::kInt}},
      {"array_set", "polyrt_array_set", T::kInt, {T::kInt,T::kInt,T::kInt}},
      {"array_free", "polyrt_array_free", T::kInt, {T::kInt}},
      {"args_count", "", T::kInt, {}},
      {"arg_text", "polyrt_arg_text", T::kString, {T::kInt}},
      {"arg_int", "polyrt_arg_int", T::kInt, {T::kInt,T::kInt}},
  };
  return api;
}
inline const NativeBuiltin *FindNativeBuiltin(const std::string &name) {
  for (const auto &api : NativeBuiltins()) if (name == api.name) return &api;
  return nullptr;
}
inline ir::IRType NativeIRType(NativeType type) {
  switch (type) {
  case NativeType::kInt: return ir::IRType::I64(true);
  case NativeType::kFloat: return ir::IRType::F64();
  case NativeType::kString: return ir::IRType::Pointer(ir::IRType::I8());
  default: return ir::IRType::Void();
  }
}
inline core::Type NativeCoreType(NativeType type) {
  switch (type) {
  case NativeType::kInt: return core::Type::Int(64, true);
  case NativeType::kFloat: return core::Type::Float(64);
  case NativeType::kString: return core::Type::String();
  default: return core::Type::Void();
  }
}
inline void RegisterNativeBuiltins(core::SymbolTable &symbols, core::TypeSystem &types,
                                   const std::string &language) {
  for (const auto &api : NativeBuiltins()) {
    std::vector<core::Type> params;
    for (auto t : api.params) params.push_back(NativeCoreType(t));
    symbols.Declare(core::Symbol{api.name, types.FunctionType(api.name, NativeCoreType(api.result), params),
                    core::SourceLoc{"<polyrt>",1,1}, core::SymbolKind::kFunction, language});
  }
}
inline void EnsureNativeArguments(ir::IRContext &ctx) {
  for (const auto *name : {"__polyc_argc", "__polyc_argv"}) {
    bool found = false;
    for (const auto &g : ctx.Globals()) found |= g->name == name;
    if (!found) ctx.CreateGlobal(name, ir::IRType::I64(), false, "0",
                                  std::make_shared<ir::LiteralExpression>(0LL));
  }
}
inline std::shared_ptr<ir::Instruction> EmitNativeBuiltin(
    const NativeBuiltin &api, std::vector<std::string> args,
    const std::vector<ir::IRType> &types, ir::IRBuilder &builder, ir::IRContext &ctx,
    Diagnostics &diagnostics, const core::SourceLoc &loc) {
  auto fail = [&](const std::string &message) -> std::shared_ptr<ir::Instruction> {
    diagnostics.ReportError(loc, ErrorCode::kUnsupportedLowering,
                             std::string(api.name) + ": " + message);
    return nullptr;
  };
  if (args.size() != api.params.size() || types.size() != args.size())
    return fail("native argument count mismatch");
  for (std::size_t i = 0; i < args.size(); ++i) {
    const auto kind = types[i].kind;
    if (api.params[i] == NativeType::kInt) {
      if (kind != ir::IRTypeKind::kI1 && kind != ir::IRTypeKind::kI8 &&
          kind != ir::IRTypeKind::kI16 && kind != ir::IRTypeKind::kI32 && kind != ir::IRTypeKind::kI64)
        return fail("argument " + std::to_string(i+1) + " requires an integer ABI");
    } else if (api.params[i] == NativeType::kFloat) {
      if (!types[i].IsFloat()) return fail("floating argument requires an explicit float type");
      if (kind == ir::IRTypeKind::kF32)
        args[i] = builder.MakeCast(ir::CastInstruction::CastKind::kFpExt,args[i],ir::IRType::F64())->name;
    } else if (api.params[i] == NativeType::kString) {
      if (kind != ir::IRTypeKind::kPointer) return fail("string argument requires a native string pointer");
      for (const auto &g : ctx.Globals()) if (g->name == args[i]) {
        if (auto gep = std::dynamic_pointer_cast<ir::ConstantGEP>(g->initializer))
          if (auto base = std::dynamic_pointer_cast<ir::GlobalValue>(gep->base)) args[i] = base->name;
        break;
      }
    }
  }
  const std::string name = api.name;
  if (name == "args_count" || name == "arg_text" || name == "arg_int") {
    EnsureNativeArguments(ctx);
    auto argc = builder.MakeLoad("__polyc_argc",ir::IRType::I64());
    if (name == "args_count") return argc;
    auto argv = builder.MakeLoad("__polyc_argv",ir::IRType::I64());
    args.insert(args.begin(), {argc->name, argv->name});
  }
  if (name == "print_text") args.insert(args.begin(), "1");
  return builder.MakeCall(api.symbol,args,NativeIRType(api.result));
}
} // namespace polyglot::frontends
