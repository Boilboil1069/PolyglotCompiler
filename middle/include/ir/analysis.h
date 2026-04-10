/**
 * @file     analysis.h
 * @brief    Intermediate Representation infrastructure
 *
 * @ingroup  Middle / IR
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "middle/include/ir/cfg.h"

namespace polyglot::ir {

/** @brief LoopInfo data structure. */
struct LoopInfo {
  BasicBlock *header{nullptr};
  std::vector<BasicBlock *> blocks;
  std::vector<BasicBlock *> backedges;
};

/** @brief LivenessInfo data structure. */
struct LivenessInfo {
  std::unordered_map<BasicBlock *, std::unordered_set<std::string>> live_in;
  std::unordered_map<BasicBlock *, std::unordered_set<std::string>> live_out;
};

/** @brief AliasClass enumeration. */
enum class AliasClass { kLocalStack, kGlobalOrArg, kUnknown };

/** @brief AliasInfo data structure. */
struct AliasInfo {
  std::unordered_map<std::string, AliasClass> classes;
  std::unordered_set<std::string> addr_taken;

  AliasClass ClassOf(const std::string &name) const;
  bool IsLocalStack(const std::string &name) const;
  bool IsAddrTaken(const std::string &name) const;
};

/** @brief AnalysisCache class. */
class AnalysisCache {
 public:
  explicit AnalysisCache(Function &func);

  void InvalidateAll();
  void InvalidateCFG();

  const ControlFlowGraph &GetCFG();
  const DominatorTree &GetDomTree();
  const DominanceFrontier &GetDomFrontier();
  const std::vector<BasicBlock *> &GetPostOrder();
  const std::vector<BasicBlock *> &GetReversePostOrder();
  const std::vector<LoopInfo> &GetLoops();
  const LivenessInfo &GetLiveness();
  const AliasInfo &GetAliasInfo();

 private:
  Function &func_;
  ControlFlowGraph cfg_;
  DominatorTree dom_;
  DominanceFrontier df_;
  std::vector<BasicBlock *> postorder_;
  std::vector<BasicBlock *> rpo_;
  std::vector<LoopInfo> loops_;
  LivenessInfo liveness_;
  AliasInfo alias_;

  bool cfg_dirty_{true};
  bool dom_dirty_{true};
  bool df_dirty_{true};
  bool order_dirty_{true};
  bool loops_dirty_{true};
  bool liveness_dirty_{true};
  bool alias_dirty_{true};

  void EnsureCFG();
  void EnsureDom();
  void EnsureDF();
  void EnsureOrders();
  void EnsureLoops();
  void EnsureLiveness();
  void EnsureAlias();
};

}  // namespace polyglot::ir
