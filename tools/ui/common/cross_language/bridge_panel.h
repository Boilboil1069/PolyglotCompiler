/**
 * @file     bridge_panel.h
 * @brief    Cross-language bridge inventory + runtime call counts.
 *
 * The Bridge panel lists every cross-language bridge generated for
 * the current workspace.  Each `Bridge` carries its host- and
 * target-language identity, the generated stub name, the
 * marshalling strategy in effect and the runtime call count fed
 * from polyrt's calltrace stream.  The IDE renders this list with
 * "double-click to source" rows.
 *
 * The model loads from the JSON document `aux/bridges.json` that
 * polyc emits; runtime counts arrive separately and merge by id.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "tools/ui/common/cross_language/cross_language_navigator.h"

namespace polyglot::tools::ui::cross_language {

enum class MarshallingStrategy {
  kZeroCopy,
  kCopyByValue,
  kPodSerialisation,
  kJsonRoundtrip,
  kProtobufRoundtrip,
  kHandleTable,
};

std::string MarshallingStrategyName(MarshallingStrategy s);
std::optional<MarshallingStrategy> MarshallingStrategyFromName(
    const std::string &name);

struct Bridge {
  std::string id;            ///< Stable id (e.g. "br-0001").
  std::string name;          ///< Display name.
  HostLanguage host_language{HostLanguage::kCpp};
  HostLanguage target_language{HostLanguage::kCpp};
  std::string stub_name;     ///< Generated stub symbol.
  MarshallingStrategy strategy{MarshallingStrategy::kCopyByValue};
  SourceLocation source;     ///< Source declaration site (`.ploy`).
  SourceLocation target;     ///< Resolved host-language site.
  long long call_count{0};   ///< Live counter from polyrt calltrace.
};

class BridgePanelModel {
 public:
  void AddBridge(Bridge b);

  /// Import the bridge inventory dumped by polyc into
  /// `aux/bridges.json`.  Existing bridges with the same id are
  /// updated in place.  Returns false on parse error.
  bool ImportFromAux(const std::string &json);

  /// Replace the call count for `bridge_id`; returns true when the
  /// id existed.
  bool UpdateCallCount(const std::string &bridge_id, long long count);

  /// Increment the call count for `bridge_id` (calltrace event).
  bool IncrementCallCount(const std::string &bridge_id, long long delta = 1);

  const std::vector<Bridge> &bridges() const { return bridges_; }
  const Bridge *FindByStub(const std::string &stub_name) const;
  const Bridge *FindById(const std::string &id) const;

  /// Filter helper used by the panel's column filters.
  std::vector<const Bridge *> FilterByLanguagePair(HostLanguage host,
                                                   HostLanguage target) const;

 private:
  std::vector<Bridge> bridges_;
};

}  // namespace polyglot::tools::ui::cross_language
