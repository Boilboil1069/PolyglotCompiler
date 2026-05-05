/**
 * @file     debug_session.h
 * @brief    High-level DAP session model (demand 2026-04-28-28 §2).
 *
 * Wraps `DapClient` with editor-friendly views: breakpoint table,
 * thread / stack / scope / variable caches, and the inline-value
 * map used by the gutter.  All state is updated synchronously in
 * response to DAP events.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "tools/ui/common/dap/dap_client.h"

namespace polyglot::tools::ui::dap {

struct SourceBreakpoint {
  std::uint32_t line{1};                ///< 1-based per DAP spec
  std::optional<std::string> condition; ///< conditional breakpoint
  std::optional<std::string> hit_condition;
  std::optional<std::string> log_message; ///< logpoint
};

enum class StopReason {
  kUnknown,
  kBreakpoint,
  kStep,
  kException,
  kPause,
  kEntry,
};

struct ThreadInfo {
  std::int64_t id{0};
  std::string name;
};

struct StackFrame {
  std::int64_t id{0};
  std::string name;
  std::string source_path;
  std::uint32_t line{0};
  std::uint32_t column{0};
};

struct Scope {
  std::string name;
  std::int64_t variables_reference{0};
  bool expensive{false};
};

struct Variable {
  std::string name;
  std::string value;
  std::string type;
  std::int64_t variables_reference{0};
};

/// Tracks the state of a single DAP debug session.  All callbacks
/// run synchronously when `Receive` (inherited from the underlying
/// `DapClient`) is invoked.
class DebugSession {
 public:
  explicit DebugSession(DapClient *client);

  // ── Breakpoints ────────────────────────────────────────────
  void SetBreakpoints(const std::string &source_path,
                      std::vector<SourceBreakpoint> bps);
  void SetExceptionBreakpoints(std::vector<std::string> filters);
  void SetFunctionBreakpoints(std::vector<std::string> names);

  const std::map<std::string, std::vector<SourceBreakpoint>>
      &breakpoints() const { return breakpoints_; }

  // ── Lifecycle ──────────────────────────────────────────────
  void Initialize(const std::string &client_id);
  void Launch(Json arguments);
  void Attach(Json arguments);
  void ConfigurationDone();
  void Disconnect(bool terminate_debuggee);

  // ── Execution control ──────────────────────────────────────
  void Continue(std::int64_t thread_id);
  void Next(std::int64_t thread_id);
  void StepIn(std::int64_t thread_id);
  void StepOut(std::int64_t thread_id);
  void Pause(std::int64_t thread_id);

  // ── State views ────────────────────────────────────────────
  const std::vector<ThreadInfo> &threads() const { return threads_; }
  const std::vector<StackFrame> &stack_frames() const { return frames_; }
  const std::vector<Scope> &scopes() const { return scopes_; }
  const std::map<std::int64_t, std::vector<Variable>>
      &variables() const { return variables_; }
  StopReason last_stop_reason() const { return last_stop_; }
  std::int64_t stopped_thread() const { return stopped_thread_; }
  bool initialized() const { return initialized_; }
  bool terminated() const { return terminated_; }

  /// Inline values map for the active stack frame (variable name →
  /// rendered value).  Cleared on continue / step.
  const std::map<std::string, std::string> &inline_values() const {
    return inline_values_;
  }

  /// Recorded `output` events (debug console buffer).
  struct OutputEntry {
    std::string category;
    std::string output;
  };
  const std::vector<OutputEntry> &console() const { return console_; }

 private:
  void HandleEvent(const std::string &name, const Json &body);

  DapClient *client_;
  std::map<std::string, std::vector<SourceBreakpoint>> breakpoints_;
  std::vector<ThreadInfo> threads_;
  std::vector<StackFrame> frames_;
  std::vector<Scope> scopes_;
  std::map<std::int64_t, std::vector<Variable>> variables_;
  std::map<std::string, std::string> inline_values_;
  std::vector<OutputEntry> console_;
  StopReason last_stop_{StopReason::kUnknown};
  std::int64_t stopped_thread_{0};
  bool initialized_{false};
  bool terminated_{false};
};

}  // namespace polyglot::tools::ui::dap
