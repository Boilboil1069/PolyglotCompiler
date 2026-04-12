/**
 * @file     ir_context.h
 * @brief    Intermediate Representation infrastructure
 *
 * @ingroup  Middle / IR
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <typeinfo>

#include "middle/include/ir/cfg.h"
#include "middle/include/ir/data_layout.h"
#include "middle/include/ir/nodes/expressions.h"
#include "middle/include/ir/nodes/statements.h"
#include "middle/include/ir/dialects/high_level.h"
#include "middle/include/ir/dialects/mid_level.h"
#include "middle/include/ir/dialects/low_level.h"

namespace polyglot::ir {

/** @brief IRContext class. */
class IRContext {
 public:
  explicit IRContext(DataLayout::Arch arch = DataLayout::Arch::kX86_64);

  std::shared_ptr<Function> CreateFunction(const std::string &name);
  std::shared_ptr<Function> CreateFunction(const std::string &name, const IRType &ret,
                                          const std::vector<std::pair<std::string, IRType>> &params);
  std::shared_ptr<GlobalValue> CreateGlobal(const std::string &name, const IRType &type,
                                           bool is_const = false, const std::string &init = "",
                                           std::shared_ptr<Value> initializer = nullptr);

  // Convenience: ensure a default function/block exist for simple builders.
  std::shared_ptr<Function> DefaultFunction();
  std::shared_ptr<BasicBlock> DefaultBlock();

  void AddStatement(const std::shared_ptr<Statement> &stmt);

  const std::vector<std::shared_ptr<Function>> &Functions() const { return functions_; }
  std::vector<std::shared_ptr<Function>> &Functions() { return functions_; }
  const std::vector<std::shared_ptr<GlobalValue>> &Globals() const { return globals_; }

  // Look up a function by name (returns nullptr if not found)
  Function *FindFunction(const std::string &name) {
    for (auto &fn : functions_)
      if (fn && fn->name == name) return fn.get();
    return nullptr;
  }
  const Function *FindFunction(const std::string &name) const {
    for (const auto &fn : functions_)
      if (fn && fn->name == name) return fn.get();
    return nullptr;
  }

  const DataLayout &Layout() const { return layout_; }
  DataLayout &Layout() { return layout_; }

  // Dialect registration
  void RegisterDialectByName(const std::string &name);
  template <typename Dialect>
  void RegisterDialect() {
    RegisterDialectByName(typeid(Dialect).name());
  }
  const std::vector<std::string> &Dialects() const { return dialects_; }

 private:
  void RegisterBuiltInDialects();
  std::vector<std::shared_ptr<Function>> functions_{};
  std::shared_ptr<Function> default_function_{};
  std::shared_ptr<BasicBlock> default_block_{};
  std::vector<std::shared_ptr<GlobalValue>> globals_{};
  std::vector<std::string> dialects_{};
  DataLayout layout_;
};

}  // namespace polyglot::ir
