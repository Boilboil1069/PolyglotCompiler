/**
 * @file     instrument_call_trace.cpp
 * @brief    Inject call-trace runtime hooks
 *
 * @ingroup  Middle / Transform
 * @author   Manning Cyrus
 * @date     2026-04-29
 */
#include "middle/include/passes/transform/instrument_call_trace.h"

#include <memory>

#include "middle/include/ir/cfg.h"
#include "middle/include/ir/nodes/statements.h"

namespace polyglot::passes::transform {

namespace {

constexpr const char *kEnterHook = "__ploy_rt_call_enter";
constexpr const char *kExitHook = "__ploy_rt_call_exit";

bool BlockAlreadyHasEnter(const ir::BasicBlock &block) {
  for (const auto &inst : block.instructions) {
    if (auto *call = dynamic_cast<ir::CallInstruction *>(inst.get())) {
      if (call->callee == kEnterHook) {
        return true;
      }
    }
  }
  return false;
}

std::shared_ptr<ir::CallInstruction>
MakeHookCall(const std::string &callee, const std::vector<std::string> &operands,
             ir::BasicBlock *parent) {
  auto call = std::make_shared<ir::CallInstruction>();
  call->callee = callee;
  call->operands = operands;
  call->type = ir::IRType::Void();
  call->parent = parent;
  return call;
}

} // namespace

CallTraceInstrumentationStats RunInstrumentCallTrace(ir::IRContext &context,
                                                     const std::string &default_language) {
  CallTraceInstrumentationStats stats;
  for (auto &fn_ptr : context.Functions()) {
    if (!fn_ptr) {
      continue;
    }
    ir::Function &fn = *fn_ptr;
    ++stats.functions_visited;

    // Skip external declarations and pre-compiled bridge stubs.  Bridge
    // stubs are byte arrays that the backend emits verbatim, so we
    // cannot insert IR-level call instructions there.
    if (fn.is_external || fn.is_bridge_stub) {
      continue;
    }
    if (fn.blocks.empty() || fn.entry == nullptr) {
      continue;
    }
    if (BlockAlreadyHasEnter(*fn.entry)) {
      continue;
    }

    const std::string lang_tag = default_language;

    // Insert the enter hook at the very top of the entry block so it
    // dominates every other instruction in the function.
    auto enter_call = MakeHookCall(
        kEnterHook,
        {"@" + fn.name + ".__name__", "@" + lang_tag + ".__lang__"},
        fn.entry);
    fn.entry->instructions.insert(fn.entry->instructions.begin(), enter_call);
    ++stats.enter_calls_inserted;

    // Insert an exit hook immediately before every return.  Other
    // terminators (branch, unreachable, throw) intentionally do not
    // receive an exit because the runtime side handles the dropped
    // event count gracefully.
    for (auto &bb : fn.blocks) {
      if (!bb || !bb->terminator) {
        continue;
      }
      if (dynamic_cast<ir::ReturnStatement *>(bb->terminator.get()) == nullptr) {
        continue;
      }
      auto exit_call = MakeHookCall(kExitHook, {"@" + fn.name + ".__name__"}, bb.get());
      bb->instructions.push_back(exit_call);
      ++stats.exit_calls_inserted;
    }

    ++stats.functions_instrumented;
    stats.instrumented_names.push_back(fn.name);
  }
  return stats;
}

} // namespace polyglot::passes::transform
