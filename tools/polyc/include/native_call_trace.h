#pragma once

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "middle/include/ir/ir_builder.h"

namespace polyglot::tools {

inline std::string NativeJsonString(const std::string &text) {
  std::ostringstream out;
  out << '"';
  for (unsigned char c : text) {
    if (c == '"' || c == '\\')
      out << '\\' << static_cast<char>(c);
    else if (c < 32)
      out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(c)
          << std::dec;
    else
      out << static_cast<char>(c);
  }
  out << '"';
  return out.str();
}

inline std::string NativeTraceType(const ir::IRType &type) {
  if (type.kind == ir::IRTypeKind::kPointer || type.kind == ir::IRTypeKind::kReference)
    return "ptr";
  return type.name;
}

// Instrument only source CALL boundaries. Values are captured before the call
// and returned bits after it; observers never re-evaluate source expressions.
// The on-disk protocol keeps every 64-bit number as a decimal JSON string.
inline bool InstrumentNativeCallTrace(ir::IRContext &module, const std::string &requested_path,
                                      std::string *error) {
  using namespace ir;
  try {
    auto *entry = module.FindFunction("__polyc_entry");
    if (!entry || entry->blocks.empty())
      throw std::runtime_error("native call tracing requires an executable entry");
    const auto path = std::filesystem::absolute(requested_path).lexically_normal();
    if (path.has_parent_path())
      std::filesystem::create_directories(path.parent_path());
    const std::string state = "__polyc_native_trace_state";
    for (const auto &global : module.Globals())
      if (global->name == state)
        throw std::runtime_error("reserved native trace state symbol");
    module.CreateGlobal(state, IRType::I64(), false, "0", std::make_shared<LiteralExpression>(0LL));
    const auto pointer = IRType::Pointer(IRType::I64());
    const auto declare = [&](const std::string &name,
                             const std::vector<std::pair<std::string, IRType>> &parameters) {
      if (module.FindFunction(name))
        throw std::runtime_error("reserved native trace hook: " + name);
      module.CreateFunction(name, IRType::Void(), parameters)->is_external = true;
    };
    declare("polyrt_trace_init", {{"state", pointer}, {"path", IRType::Pointer(IRType::I8())}});
    declare("polyrt_trace_begin", {{"record", pointer},
                                   {"state", pointer},
                                   {"site", IRType::I64()},
                                   {"argc", IRType::I64()}});
    declare("polyrt_trace_end",
            {{"record", pointer}, {"state", pointer}, {"result", IRType::I64()}});
    IRBuilder builder(module);
    std::ostringstream metadata;
    metadata
        << "{\"schema\":\"polyglot.native-call-sites.v1\",\"scope\":\"foreign-call-boundaries\","
        << "\"threading\":\"single-thread\",\"sites\":[";
    std::size_t site = 0;
    const auto raw_bits = [&](const std::string &value, const IRType &type) -> std::string {
      if (type.kind == IRTypeKind::kVoid)
        return "0";
      if (type.kind == IRTypeKind::kPointer || type.kind == IRTypeKind::kReference)
        return builder.MakeCast(CastInstruction::CastKind::kPtrToInt, value, IRType::I64())->name;
      if (type.IsFloat()) {
        const auto integer = type.BitWidth() == 32 ? IRType::I32(false) : IRType::I64(false);
        auto bits = builder.MakeCast(CastInstruction::CastKind::kBitcast, value, integer)->name;
        if (type.BitWidth() == 32)
          bits = builder.MakeCast(CastInstruction::CastKind::kZExt, bits, IRType::I64())->name;
        return bits;
      }
      if (!type.IsInteger())
        throw std::runtime_error("native trace cannot capture " + type.name);
      if (type.BitWidth() == 64)
        return value;
      return builder
          .MakeCast(type.IsSigned() ? CastInstruction::CastKind::kSExt
                                    : CastInstruction::CastKind::kZExt,
                    value, IRType::I64())
          ->name;
    };
    for (const auto &function : module.Functions()) {
      if (function->is_external || function->is_bridge_stub)
        continue;
      builder.SetCurrentFunction(function);
      for (const auto &block : function->blocks) {
        auto original = std::move(block->instructions);
        block->instructions.clear();
        builder.SetInsertPoint(block);
        if (function.get() == entry &&
            block.get() == (entry->entry ? entry->entry : entry->blocks.front().get())) {
          auto literal = builder.MakeStringLiteral(path.string(), "trace_path");
          if (literal.ends_with(".ptr"))
            literal.resize(literal.size() - 4);
          builder.MakeCall("polyrt_trace_init", {state, literal}, IRType::Void());
        }
        for (const auto &instruction : original) {
          auto call = std::dynamic_pointer_cast<CallInstruction>(instruction);
          if (!call || call->trace_language.empty()) {
            block->instructions.push_back(instruction);
            continue;
          }
          if (call->operands.size() != call->trace_argument_types.size())
            throw std::runtime_error("native trace argument metadata mismatch");
          if (call->operands.size() > 64)
            throw std::runtime_error("native trace supports at most 64 scalar arguments");
          if (site++)
            metadata << ',';
          metadata << "{\"id\":" << NativeJsonString(std::to_string(site))
                   << ",\"source\":{\"file\":" << NativeJsonString(call->trace_file)
                   << ",\"line\":" << call->trace_line << ",\"column\":" << call->trace_column
                   << "},\"callee\":" << NativeJsonString(call->trace_callee)
                   << ",\"language\":" << NativeJsonString(call->trace_language)
                   << ",\"result_type\":" << NativeJsonString(NativeTraceType(call->type))
                   << ",\"arguments\":[";
          const auto record_type = IRType::Array(IRType::I64(), 8 + call->operands.size());
          const auto storage = builder.MakeAlloca(record_type)->name;
          const auto record = builder.MakeGEP(storage, record_type, {0, 0})->name;
          for (std::size_t i = 0; i < call->operands.size(); ++i) {
            if (i)
              metadata << ',';
            const auto name = i < call->trace_argument_names.size() ? call->trace_argument_names[i]
                                                                    : "arg" + std::to_string(i);
            metadata << "{\"name\":" << NativeJsonString(name) << ",\"type\":"
                     << NativeJsonString(NativeTraceType(call->trace_argument_types[i])) << '}';
            auto slot = builder.MakeGEP(storage, record_type, {0, 8 + i})->name;
            builder.MakeStore(slot, raw_bits(call->operands[i], call->trace_argument_types[i]));
          }
          metadata << "]}";
          builder.MakeCall(
              "polyrt_trace_begin",
              {record, state, std::to_string(site), std::to_string(call->operands.size())},
              IRType::Void());
          block->instructions.push_back(instruction);
          const auto result = raw_bits(call->name, call->type);
          builder.MakeCall("polyrt_trace_end", {record, state, result}, IRType::Void());
        }
      }
    }
    metadata << "]}\n";
    std::ofstream output(path.string() + ".sites.json", std::ios::binary | std::ios::trunc);
    output << metadata.str();
    output.close();
    if (!output)
      throw std::runtime_error("cannot write native trace site metadata");
    return true;
  } catch (const std::exception &exception) {
    if (error)
      *error = exception.what();
    return false;
  }
}
} // namespace polyglot::tools
