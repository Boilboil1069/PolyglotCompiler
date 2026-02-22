#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "middle/include/ir/analysis.h"

namespace polyglot::passes::analysis {

// Public analysis API wrapping the internal ir::AliasInfo.
// Consumers can query pointer alias relationships without depending on
// internal IR details.
struct AliasAnalysisResult {
  // Alias query between two pointer names
  enum class AliasKind { kNoAlias, kMayAlias, kMustAlias };

  // Populate the result from a function via AnalysisCache
  void ComputeFrom(ir::Function &func) {
    ir::AnalysisCache cache(func);
    info_ = cache.GetAliasInfo();
    valid_ = true;
  }

  // Return the alias class for a given value name
  ir::AliasClass ClassOf(const std::string &name) const {
    if (!valid_) return ir::AliasClass::kUnknown;
    return info_.ClassOf(name);
  }

  // Check whether two pointers may alias
  AliasKind Query(const std::string &a, const std::string &b) const {
    if (!valid_) return AliasKind::kMayAlias;
    auto ca = info_.ClassOf(a);
    auto cb = info_.ClassOf(b);
    // Two distinct local-stack pointers that are not address-taken cannot
    // alias each other (they are separate allocas).
    if (ca == ir::AliasClass::kLocalStack &&
        cb == ir::AliasClass::kLocalStack &&
        !info_.IsAddrTaken(a) && !info_.IsAddrTaken(b) &&
        a != b) {
      return AliasKind::kNoAlias;
    }
    if (a == b) return AliasKind::kMustAlias;
    return AliasKind::kMayAlias;
  }

  bool IsAddrTaken(const std::string &name) const {
    if (!valid_) return true;
    return info_.IsAddrTaken(name);
  }

  bool IsValid() const { return valid_; }

private:
  ir::AliasInfo info_;
  bool valid_{false};
};

}  // namespace polyglot::passes::analysis
