/**
 * @file     problems_aggregator.h
 * @brief    Workspace-wide diagnostic aggregator for the Problems Panel
 *
 * Pure C++ (Qt-free) container that holds the union of all currently
 * reported diagnostics, keyed by (file, source).  Each producer — the
 * LSP client, the in-process @ref CompilerService, or the polyc
 * `--check` fallback — calls @ref Replace to atomically swap its own
 * slice; the aggregator then exposes a filtered, sorted view to the Qt
 * panel and emits a change callback.
 *
 * The split between this Qt-free aggregator and the
 * @ref polyglot::tools::ui::ProblemsPanel widget exists so that the
 * aggregation, severity-counting and filter logic can be unit-tested
 * without dragging in a Qt event loop.  See
 * `tests/unit/polyui/problems_panel_model_test.cpp`.
 *
 * Implements demand 2026-04-28-20 §1 (面板 / 聚合 / 过滤) plus the
 * counts surfaced in §1 status-bar item.
 *
 * @ingroup  Tool / polyui / problems
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include "tools/ui/common/include/compiler_service.h"

namespace polyglot::tools::ui {

/// Diagnostic flavour bitmask used by the panel filter row.
enum class SeverityMask : std::uint32_t {
  kError       = 1u << 0,
  kWarning     = 1u << 1,
  kInformation = 1u << 2,
  kHint        = 1u << 3,
  kAll         = 0xFu,
};

/// Severity classification derived from the upstream string label.
enum class Severity : int {
  kError = 0,
  kWarning = 1,
  kInformation = 2,
  kHint = 3,
};

/// Map a free-form severity string ("error", "warning", "info", "hint",
/// "note") to the typed @ref Severity.  Defaults to error when unknown.
Severity ClassifySeverity(const std::string &label);

/// One displayable problem row.
struct ProblemEntry {
  std::string file;        ///< Absolute filesystem path or LSP URI.
  std::string source;      ///< Producer label, e.g. "polyls" / "polyc" / "compiler".
  Severity severity{Severity::kError};
  std::size_t line{0};     ///< 1-based for display; 0 means unknown.
  std::size_t column{0};   ///< 1-based for display.
  std::size_t end_line{0};
  std::size_t end_column{0};
  std::string code;        ///< Optional diagnostic code (e.g. "E1001").
  std::string message;
  std::string suggestion;
};

/// Filter spec.  Empty strings mean "match any".
struct ProblemFilter {
  std::uint32_t severity_mask{static_cast<std::uint32_t>(SeverityMask::kAll)};
  std::string file_substring;     ///< Case-insensitive substring on @ref ProblemEntry::file.
  std::string source_substring;   ///< Case-insensitive substring on @ref ProblemEntry::source.
  std::string message_regex;      ///< Optional std::regex applied to @ref ProblemEntry::message.
};

/// Aggregator core.  Thread-safe; @ref Replace can be called from any
/// thread, but the change callback is invoked synchronously on the
/// thread that triggered the change — the Qt panel marshals it onto the
/// GUI thread before touching widgets.
class ProblemsAggregator {
 public:
  using ChangeCallback = std::function<void()>;

  ProblemsAggregator() = default;

  /// Replace the slice of diagnostics owned by (@p file, @p source) with
  /// @p entries.  Passing an empty vector clears the slice.
  void Replace(const std::string &file, const std::string &source,
               std::vector<ProblemEntry> entries);

  /// Convenience adapter for @ref DiagnosticInfo (the UI-side type used
  /// by @ref CompilerService and @ref CodeEditor).
  void ReplaceFromDiagnosticInfo(const std::string &file, const std::string &source,
                                 const std::vector<DiagnosticInfo> &diags);

  /// Drop everything reported by @p source (e.g. when an LSP server
  /// disconnects).
  void ClearSource(const std::string &source);

  /// Drop everything attached to @p file (e.g. on file delete).
  void ClearFile(const std::string &file);

  /// Drop every entry.
  void Clear();

  /// Install / replace the change callback.  Pass an empty function to
  /// detach.
  void SetChangeCallback(ChangeCallback cb);

  /// Snapshot every entry in display order: grouped by file (lexicographic
  /// case-insensitive), then by line, then by column.  Severity filtering
  /// and the substring / regex filters are applied here.
  std::vector<ProblemEntry> Snapshot(const ProblemFilter &filter) const;

  /// Counts of each severity across all currently held entries (no filter).
  struct Counts {
    std::size_t errors{0};
    std::size_t warnings{0};
    std::size_t information{0};
    std::size_t hints{0};
    std::size_t Total() const { return errors + warnings + information + hints; }
  };
  Counts CountAll() const;

  /// Distinct list of source labels currently present (used to build the
  /// panel's source-filter dropdown).
  std::vector<std::string> KnownSources() const;

 private:
  using Slice = std::vector<ProblemEntry>;
  /// Outer key: file, inner key: source.  Using std::map gives us
  /// deterministic iteration order, which matters for the snapshot.
  using SliceMap = std::map<std::string, std::map<std::string, Slice>>;

  static bool MatchesFilter(const ProblemEntry &e, const ProblemFilter &f,
                            const std::regex *compiled_regex);
  static bool ContainsCaseInsensitive(const std::string &haystack,
                                      const std::string &needle);

  void NotifyLocked();

  mutable std::mutex mu_;
  SliceMap slices_;
  ChangeCallback on_changed_;
};

}  // namespace polyglot::tools::ui
