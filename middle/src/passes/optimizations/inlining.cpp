/**
 * @file     inlining.cpp
 * @brief    Middle-end implementation
 *
 * @ingroup  Middle
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "middle/include/passes/transform/inlining.h"

#include <unordered_map>
#include <unordered_set>

#include "middle/include/ir/passes/opt.h"

namespace polyglot::passes::transform {

void RunInlining(ir::IRContext &context) {
  static int inline_counter = 0;
  auto functions = context.Functions();

  std::unordered_map<std::string, std::shared_ptr<ir::Function>> fn_by_name;
  for (auto &fn : functions) fn_by_name[fn->name] = fn;

  auto apply_subst = [](ir::Function &fn, const std::unordered_map<std::string, std::string> &subst) {
    auto replace = [&](std::string &s) {
      auto it = subst.find(s);
      if (it != subst.end()) s = it->second;
    };
    for (auto &bb_ptr : fn.blocks) {
      for (auto &phi : bb_ptr->phis) {
        replace(phi->name);
        for (auto &inc : phi->incomings) replace(inc.second);
      }
      for (auto &inst : bb_ptr->instructions) {
        replace(inst->name);
        for (auto &op : inst->operands) replace(op);
      }
      if (bb_ptr->terminator) {
        replace(bb_ptr->terminator->name);
        for (auto &op : bb_ptr->terminator->operands) replace(op);
      }
    }
  };

  auto clone_inst = [](const ir::Instruction &inst) -> std::shared_ptr<ir::Instruction> {
    if (auto *bin = dynamic_cast<const ir::BinaryInstruction *>(&inst)) return std::make_shared<ir::BinaryInstruction>(*bin);
    if (auto *phi = dynamic_cast<const ir::PhiInstruction *>(&inst)) return std::make_shared<ir::PhiInstruction>(*phi);
    if (auto *as = dynamic_cast<const ir::AssignInstruction *>(&inst)) return std::make_shared<ir::AssignInstruction>(*as);
    if (auto *call = dynamic_cast<const ir::CallInstruction *>(&inst)) return std::make_shared<ir::CallInstruction>(*call);
    if (auto *alloca_inst = dynamic_cast<const ir::AllocaInstruction *>(&inst)) return std::make_shared<ir::AllocaInstruction>(*alloca_inst);
    if (auto *load = dynamic_cast<const ir::LoadInstruction *>(&inst)) return std::make_shared<ir::LoadInstruction>(*load);
    if (auto *store = dynamic_cast<const ir::StoreInstruction *>(&inst)) return std::make_shared<ir::StoreInstruction>(*store);
    if (auto *cast = dynamic_cast<const ir::CastInstruction *>(&inst)) return std::make_shared<ir::CastInstruction>(*cast);
    if (auto *gep = dynamic_cast<const ir::GetElementPtrInstruction *>(&inst)) return std::make_shared<ir::GetElementPtrInstruction>(*gep);
    if (auto *ret = dynamic_cast<const ir::ReturnStatement *>(&inst)) return std::make_shared<ir::ReturnStatement>(*ret);
    if (auto *br = dynamic_cast<const ir::BranchStatement *>(&inst)) return std::make_shared<ir::BranchStatement>(*br);
    if (auto *cbr = dynamic_cast<const ir::CondBranchStatement *>(&inst)) return std::make_shared<ir::CondBranchStatement>(*cbr);
    if (auto *sw = dynamic_cast<const ir::SwitchStatement *>(&inst)) return std::make_shared<ir::SwitchStatement>(*sw);
    return nullptr;
  };

  for (auto &fn : functions) {
    for (auto &bb_ptr : fn->blocks) {
      auto &insts = bb_ptr->instructions;
      for (auto it = insts.begin(); it != insts.end(); ++it) {
        auto *call = dynamic_cast<ir::CallInstruction *>(it->get());
        if (!call || call->is_indirect || call->callee.empty()) continue;
        auto callee_it = fn_by_name.find(call->callee);
        if (callee_it == fn_by_name.end()) continue;
        auto callee = callee_it->second;
        // simple heuristic: single block, no phi, no calls, ends with return
        if (callee->blocks.size() != 1) continue;
        auto *cbb = callee->blocks[0].get();
        if (!cbb->phis.empty()) continue;
        auto *ret = dynamic_cast<ir::ReturnStatement *>(cbb->terminator.get());
        if (!ret) continue;
        bool has_call = false;
        for (auto &cinst : cbb->instructions) {
          if (dynamic_cast<ir::CallInstruction *>(cinst.get())) {
            has_call = true;
            break;
          }
        }
        if (has_call) continue;

        // map parameters to arguments
        if (callee->params.size() != call->operands.size()) continue;
        std::unordered_map<std::string, std::string> subst_params;
        for (size_t i = 0; i < callee->params.size(); ++i) {
          subst_params[callee->params[i]] = call->operands[i];
        }

        // clone callee instructions
        std::vector<std::shared_ptr<ir::Instruction>> cloned;
        for (auto &cinst : cbb->instructions) {
          auto clone = clone_inst(*cinst);
          if (!clone) continue;
          clone->parent = bb_ptr.get();
          cloned.push_back(std::move(clone));
        }

        std::unordered_map<std::string, std::string> local_names;
        auto rename = [&](std::string &s) {
          auto it_sub = subst_params.find(s);
          if (it_sub != subst_params.end()) s = it_sub->second;
          auto it_local = local_names.find(s);
          if (it_local != local_names.end()) s = it_local->second;
        };

        for (auto &cinst : cloned) {
          for (auto &op : cinst->operands) rename(op);
          if (cinst->HasResult()) {
            std::string fresh = cinst->name + ".inl" + std::to_string(inline_counter);
            local_names[cinst->name] = fresh;
            cinst->name = fresh;
          }
        }

        // splice cloned instructions before call
        it = insts.erase(it);
        it = insts.insert(it, cloned.begin(), cloned.end());

        // handle return value substitution
        if (ret && !ret->operands.empty() && call->HasResult()) {
          std::string ret_name = ret->operands[0];
          rename(ret_name);
          apply_subst(*fn, {{call->name, ret_name}});
        }

        if (it != insts.begin()) --it;  // position iterator
        ++inline_counter;
      }
    }
  }
}

}  // namespace polyglot::passes::transform
