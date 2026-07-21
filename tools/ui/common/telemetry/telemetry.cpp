/**
 * @file     telemetry.cpp
 * @brief    Telemetry consent, allow-list, local preview buffer
 *           and crash report store.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/telemetry/telemetry.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace polyglot::tools::ui::telemetry {

using Json = nlohmann::json;

std::string ConsentStateName(ConsentState s) {
  switch (s) {
    case ConsentState::kUnknown:  return "unknown";
    case ConsentState::kDeclined: return "declined";
    case ConsentState::kGranted:  return "granted";
  }
  return "unknown";
}

void ConsentManager::Grant()   { state_ = ConsentState::kGranted; }
void ConsentManager::Decline() { state_ = ConsentState::kDeclined; }
void ConsentManager::Revoke()  { state_ = ConsentState::kDeclined; }

void FieldAllowList::Allow(const std::string &f) { allowed_.insert(f); }
void FieldAllowList::DisallowAll()               { allowed_.clear(); }
bool FieldAllowList::Contains(const std::string &f) const {
  return allowed_.count(f) > 0;
}

TelemetryEvent FieldAllowList::Filter(TelemetryEvent event) const {
  std::unordered_map<std::string, std::string> kept;
  for (auto &kv : event.fields)
    if (allowed_.count(kv.first)) kept.insert(std::move(kv));
  event.fields = std::move(kept);
  return event;
}

std::optional<long long> TelemetryBuffer::Record(
    const ConsentManager &consent, const FieldAllowList &allow,
    TelemetryEvent event) {
  if (!consent.may_collect()) return std::nullopt;
  event = allow.Filter(std::move(event));
  long long id = ++next_id_;
  if (event.ts_unix == 0) event.ts_unix = id;
  items_.push_back(std::move(event));
  if (items_.size() > capacity_)
    items_.erase(items_.begin(),
                 items_.begin() + (items_.size() - capacity_));
  return id;
}

std::vector<TelemetryEvent> TelemetryBuffer::DrainForUpload(
    const ConsentManager &c) {
  if (!c.may_upload()) return {};
  std::vector<TelemetryEvent> out;
  out.swap(items_);
  return out;
}

long long CrashReportStore::Capture(CrashReport r) {
  r.id = ++next_id_;
  if (r.ts_unix == 0) r.ts_unix = r.id;
  items_.push_back(std::move(r));
  return next_id_;
}

bool CrashReportStore::MarkUploaded(long long id) {
  for (auto &r : items_)
    if (r.id == id) { r.uploaded = true; return true; }
  return false;
}

std::vector<CrashReport> CrashReportStore::All() const { return items_; }

std::vector<CrashReport> CrashReportStore::Pending() const {
  std::vector<CrashReport> out;
  for (const auto &r : items_) if (!r.uploaded) out.push_back(r);
  return out;
}

std::optional<CrashReport> CrashReportStore::Get(long long id) const {
  for (const auto &r : items_) if (r.id == id) return r;
  return std::nullopt;
}

bool CrashReportStore::Remove(long long id) {
  auto it = std::find_if(items_.begin(), items_.end(),
                         [&](const auto &r) { return r.id == id; });
  if (it == items_.end()) return false;
  items_.erase(it);
  return true;
}

std::string CrashReportStore::Serialize() const {
  Json arr = Json::array();
  for (const auto &r : items_) {
    arr.push_back({{"id", r.id},
                   {"ts_unix", r.ts_unix},
                   {"version", r.version},
                   {"platform", r.platform},
                   {"signal_name", r.signal_name},
                   {"stack", r.stack},
                   {"user_comment", r.user_comment},
                   {"uploaded", r.uploaded}});
  }
  Json doc;
  doc["next_id"] = next_id_;
  doc["items"] = std::move(arr);
  return doc.dump(2);
}

bool CrashReportStore::Load(const std::string &json) {
  Json doc;
  try {
    doc = Json::parse(json);
  } catch (const Json::parse_error &) {
    return false;
  }
  if (!doc.is_object()) return false;
  next_id_ = doc.value("next_id", 0LL);
  items_.clear();
  if (doc.contains("items") && doc["items"].is_array()) {
    for (const auto &r : doc["items"]) {
      CrashReport c;
      c.id = r.value("id", 0LL);
      c.ts_unix = r.value("ts_unix", 0LL);
      c.version = r.value("version", std::string{});
      c.platform = r.value("platform", std::string{});
      c.signal_name = r.value("signal_name", std::string{});
      c.stack = r.value("stack", std::string{});
      c.user_comment = r.value("user_comment", std::string{});
      c.uploaded = r.value("uploaded", false);
      items_.push_back(std::move(c));
    }
  }
  return true;
}

}  // namespace polyglot::tools::ui::telemetry
