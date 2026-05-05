/**
 * @file     hot_reload.h
 * @brief    Hot-Reload / Edit-and-Continue dispatcher.
 *
 * The IDE shell forwards file-save events to `HotReloadEngine`,
 * which routes each change to a per-language handler.  Handlers
 * perform the actual incremental rebuild + symbol replacement and
 * report the outcome through `ReloadResult`.  The engine keeps a
 * per-file pending queue so saves issued while a reload is in
 * flight coalesce into a single follow-up reload.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace polyglot::tools::ui::runtime {

enum class ReloadStatus {
  kSuccess,
  kPartial,    ///< Some symbols replaced, others required restart.
  kFailed,
  kUnsupported,
};

struct ReloadResult {
  ReloadStatus status{ReloadStatus::kSuccess};
  std::vector<std::string> replaced_symbols;
  std::string message;
};

struct ReloadRequest {
  std::string language;     ///< "ploy", "python", "cpp", "rust", "java", "dotnet".
  std::string file;
  std::string session_id;   ///< Active DAP session, if any.
  bool debugger_attached{false};
};

using ReloadHandler =
    std::function<ReloadResult(const ReloadRequest &request)>;

class HotReloadEngine {
 public:
  /// Register or replace a per-language handler.  Use empty
  /// language ("") to install a fallback handler.
  void RegisterHandler(std::string language, ReloadHandler handler);

  /// Look up the language for `file` based on the suffix.  Returns
  /// an empty string when unknown.
  static std::string DetectLanguage(const std::string &file);

  /// Trigger a reload.  Coalesces overlapping requests for the same
  /// file: the second call is queued and runs once the first
  /// completes.
  ReloadResult Notify(const ReloadRequest &request);

  /// Returns true if a queued reload was waiting on this file and
  /// has now been executed.
  bool DrainPending(const std::string &file);

  std::size_t pending_count() const noexcept { return pending_.size(); }

 private:
  std::unordered_map<std::string, ReloadHandler> handlers_;
  std::unordered_map<std::string, ReloadRequest> pending_;
  std::unordered_map<std::string, bool> in_flight_;
};

}  // namespace polyglot::tools::ui::runtime
