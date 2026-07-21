/**
 * @file     problem_matcher.h
 * @brief    Compiler-output → diagnostic parser (demand 2026-04-28-29 §1).
 *
 * Recognises the canonical `$gcc` / `$tsc` / `$rustc` / `$msbuild`
 * line shapes plus a watch-mode meta matcher that trips on
 * `beginsPattern` / `endsPattern` lines.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace polyglot::tools::ui::tasks {

enum class DiagnosticSeverity { kInfo, kWarning, kError };

struct Diagnostic {
  std::string file;
  int line{0};
  int column{0};
  DiagnosticSeverity severity{DiagnosticSeverity::kError};
  std::string code;
  std::string message;
};

enum class WatchSignal { kNone, kBegin, kEnd };

class ProblemMatcher {
 public:
  /// Construct one of the well-known matchers.  Unknown names fall
  /// back to `$gcc`.
  explicit ProblemMatcher(std::string name = "$gcc");

  /// Attempt to parse one output line into a diagnostic.
  std::optional<Diagnostic> Match(const std::string &line) const;

  /// In background tasks, detect `make: entering` / `make: leaving`
  /// style watch boundaries.
  WatchSignal DetectWatch(const std::string &line) const;

  const std::string &name() const { return name_; }

 private:
  std::string name_;
};

}  // namespace polyglot::tools::ui::tasks
