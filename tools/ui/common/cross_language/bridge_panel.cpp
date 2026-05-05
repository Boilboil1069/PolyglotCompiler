/**
 * @file     bridge_panel.cpp
 * @brief    Implementation of `bridge_panel.h`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/cross_language/bridge_panel.h"

#include <algorithm>

#include <nlohmann/json.hpp>

namespace polyglot::tools::ui::cross_language {
namespace {

using Json = nlohmann::json;

}  // namespace

std::string MarshallingStrategyName(MarshallingStrategy s) {
  switch (s) {
    case MarshallingStrategy::kZeroCopy:           return "zero-copy";
    case MarshallingStrategy::kCopyByValue:        return "copy-by-value";
    case MarshallingStrategy::kPodSerialisation:   return "pod-serialisation";
    case MarshallingStrategy::kJsonRoundtrip:      return "json-roundtrip";
    case MarshallingStrategy::kProtobufRoundtrip:  return "protobuf-roundtrip";
    case MarshallingStrategy::kHandleTable:        return "handle-table";
  }
  return "unknown";
}

std::optional<MarshallingStrategy> MarshallingStrategyFromName(
    const std::string &name) {
  if (name == "zero-copy")          return MarshallingStrategy::kZeroCopy;
  if (name == "copy-by-value")      return MarshallingStrategy::kCopyByValue;
  if (name == "pod-serialisation")  return MarshallingStrategy::kPodSerialisation;
  if (name == "json-roundtrip")     return MarshallingStrategy::kJsonRoundtrip;
  if (name == "protobuf-roundtrip") return MarshallingStrategy::kProtobufRoundtrip;
  if (name == "handle-table")       return MarshallingStrategy::kHandleTable;
  return std::nullopt;
}

void BridgePanelModel::AddBridge(Bridge b) {
  for (auto &existing : bridges_) {
    if (existing.id == b.id) {
      // Preserve runtime count when re-importing static metadata.
      long long carry = existing.call_count;
      existing = std::move(b);
      if (existing.call_count == 0) existing.call_count = carry;
      return;
    }
  }
  bridges_.push_back(std::move(b));
}

bool BridgePanelModel::ImportFromAux(const std::string &json) {
  auto j = Json::parse(json, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return false;
  if (!j.contains("bridges") || !j["bridges"].is_array()) return false;

  for (const auto &b : j["bridges"]) {
    Bridge br;
    br.id = b.value("id", std::string{});
    if (br.id.empty()) continue;
    br.name = b.value("name", std::string{});
    br.stub_name = b.value("stub", std::string{});
    if (auto h = HostLanguageFromName(b.value("host", std::string{}))) {
      br.host_language = *h;
    }
    if (auto t = HostLanguageFromName(b.value("target", std::string{}))) {
      br.target_language = *t;
    }
    if (auto s = MarshallingStrategyFromName(
            b.value("strategy", std::string{"copy-by-value"}))) {
      br.strategy = *s;
    }
    if (b.contains("source") && b["source"].is_object()) {
      br.source.file = b["source"].value("file", std::string{});
      br.source.line = b["source"].value("line", 0);
      br.source.column = b["source"].value("column", 0);
    }
    if (b.contains("target_location") && b["target_location"].is_object()) {
      br.target.file = b["target_location"].value("file", std::string{});
      br.target.line = b["target_location"].value("line", 0);
      br.target.column = b["target_location"].value("column", 0);
    }
    br.call_count = b.value("call_count", 0LL);
    AddBridge(std::move(br));
  }
  return true;
}

bool BridgePanelModel::UpdateCallCount(const std::string &bridge_id,
                                       long long count) {
  for (auto &b : bridges_) {
    if (b.id == bridge_id) { b.call_count = count; return true; }
  }
  return false;
}

bool BridgePanelModel::IncrementCallCount(const std::string &bridge_id,
                                          long long delta) {
  for (auto &b : bridges_) {
    if (b.id == bridge_id) { b.call_count += delta; return true; }
  }
  return false;
}

const Bridge *BridgePanelModel::FindByStub(
    const std::string &stub_name) const {
  for (const auto &b : bridges_)
    if (b.stub_name == stub_name) return &b;
  return nullptr;
}

const Bridge *BridgePanelModel::FindById(const std::string &id) const {
  for (const auto &b : bridges_)
    if (b.id == id) return &b;
  return nullptr;
}

std::vector<const Bridge *> BridgePanelModel::FilterByLanguagePair(
    HostLanguage host, HostLanguage target) const {
  std::vector<const Bridge *> out;
  for (const auto &b : bridges_) {
    if (b.host_language == host && b.target_language == target)
      out.push_back(&b);
  }
  return out;
}

}  // namespace polyglot::tools::ui::cross_language
