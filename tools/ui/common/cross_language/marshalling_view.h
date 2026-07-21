/**
 * @file     marshalling_view.h
 * @brief    Render the parameter / return-value conversion chain.
 *
 * When the user selects a `LINK`, `CALL` or `METHOD` in the
 * editor, the marshalling side panel renders the conversion path
 * for every parameter and the return value:
 *
 *     IR lowering  →  marshalling helper  →  target ABI adapter
 *
 * Each step carries a code snippet (extracted from polyc's
 * lowering output) and a click-through source location, so the
 * user can drill into the helper that performs the conversion.
 *
 * The model is populated from the `aux/marshalling.json` document
 * polyc emits per compilation unit.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <string>
#include <vector>

#include "tools/ui/common/cross_language/bridge_panel.h"

namespace polyglot::tools::ui::cross_language {

enum class MarshallingStage {
  kIRLowering,
  kHelper,
  kAbiAdapter,
};

std::string MarshallingStageName(MarshallingStage s);

struct MarshallingStep {
  MarshallingStage stage{MarshallingStage::kIRLowering};
  std::string description;   ///< Short label for the panel row.
  std::string code_snippet;  ///< Lowering output excerpt.
  SourceLocation source;     ///< File/line of the helper / IR.
};

struct MarshallingArgument {
  std::string name;
  std::string source_type;   ///< Host-language type.
  std::string target_type;   ///< Target-language type.
  std::vector<MarshallingStep> steps;
};

struct MarshallingChain {
  std::string bridge_id;
  std::string symbol;
  std::vector<MarshallingArgument> parameters;
  MarshallingArgument return_value;
};

class MarshallingViewBuilder {
 public:
  /// Load every chain from a polyc-emitted aux JSON document.
  bool LoadAux(const std::string &json);

  /// Insert / replace one chain.
  void AddChain(MarshallingChain chain);

  const std::vector<MarshallingChain> &chains() const { return chains_; }
  const MarshallingChain *FindByBridge(const std::string &bridge_id) const;

  /// Synthesize a chain for `bridge` when no aux record exists yet.
  /// The synthesized chain has a single argument and the canonical
  /// three-stage pipeline keyed off the bridge's strategy.
  static MarshallingChain Synthesize(const Bridge &bridge);

 private:
  std::vector<MarshallingChain> chains_;
};

}  // namespace polyglot::tools::ui::cross_language
