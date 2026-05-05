/**
 * @file     telemetry.h
 * @brief    Opt-in telemetry, local feedback inbox and crash
 *           report store.  All collection is off by default; the
 *           local preview lets the user inspect every event before
 *           it ever leaves the machine.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace polyglot::tools::ui::telemetry {

/// Three-state consent: never asked / opted-out / opted-in.
enum class ConsentState {
  kUnknown,
  kDeclined,
  kGranted,
};

std::string ConsentStateName(ConsentState s);

class ConsentManager {
 public:
  ConsentState state() const { return state_; }
  /// Default is `kUnknown`; flip explicitly with Grant / Decline.
  void Grant();
  void Decline();
  /// Withdraw previous consent.  Pending events stay buffered for
  /// the user to inspect but are never uploaded.
  void Revoke();

  bool may_collect() const { return state_ == ConsentState::kGranted; }
  bool may_upload() const { return state_ == ConsentState::kGranted; }

 private:
  ConsentState state_{ConsentState::kUnknown};
};

struct TelemetryEvent {
  std::string id;            ///< Event id (`editor.opened`, ...).
  std::string component;     ///< Subsystem name.
  long long ts_unix{0};
  std::unordered_map<std::string, std::string> fields;
};

/// Allow-list of fields that the IDE may attach to an event.
/// Anything not in the list is stripped before the event reaches
/// the local preview (and therefore before it can ever upload).
class FieldAllowList {
 public:
  void Allow(const std::string &field);
  void DisallowAll();
  bool Contains(const std::string &field) const;
  /// Returns a copy of `event` with non-allowed fields removed.
  TelemetryEvent Filter(TelemetryEvent event) const;

 private:
  std::unordered_set<std::string> allowed_;
};

/// Bounded buffer the user can browse (the "local preview").
/// Returns nullopt when consent forbids collection.
class TelemetryBuffer {
 public:
  explicit TelemetryBuffer(size_t capacity = 256) : capacity_(capacity) {}

  std::optional<long long> Record(const ConsentManager &consent,
                                   const FieldAllowList &allow,
                                   TelemetryEvent event);
  std::vector<TelemetryEvent> List() const { return items_; }
  void Clear() { items_.clear(); }

  /// Drain every event that may currently be uploaded.  Returns
  /// empty when consent forbids upload (events stay buffered).
  std::vector<TelemetryEvent> DrainForUpload(const ConsentManager &c);

  size_t size() const { return items_.size(); }
  size_t capacity() const { return capacity_; }

 private:
  size_t capacity_;
  long long next_id_{0};
  std::vector<TelemetryEvent> items_;
};

struct CrashReport {
  long long id{0};
  long long ts_unix{0};
  std::string version;
  std::string platform;
  std::string signal_name;     ///< "SIGSEGV", "EXCEPTION_ACCESS_VIOLATION", ...
  std::string stack;           ///< Symbolised back-trace.
  std::string user_comment;
  bool uploaded{false};
};

/// Crash reports always land on disk first.  Upload is a separate
/// gate: the caller invokes `MarkUploaded` only after the user (or
/// an automatic policy honouring `ConsentManager::may_upload`)
/// approves.
class CrashReportStore {
 public:
  long long Capture(CrashReport report);
  bool MarkUploaded(long long id);
  std::vector<CrashReport> All() const;
  std::vector<CrashReport> Pending() const;
  std::optional<CrashReport> Get(long long id) const;
  bool Remove(long long id);

  std::string Serialize() const;
  bool Load(const std::string &json);

 private:
  long long next_id_{0};
  std::vector<CrashReport> items_;
};

}  // namespace polyglot::tools::ui::telemetry
