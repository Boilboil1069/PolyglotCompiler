/**
 * @file     inline_test_lens.h
 * @brief    Per-line CodeLens model for ▶ Run / 🐞 Debug actions.
 *
 * The IDE shell scans the open document, asks `InlineTestLens` for
 * a list of `Lens` entries (one per discovered test function), and
 * renders them above the corresponding line.  Failures captured by
 * the test runner re-enter the lens via `RecordFailure` so the
 * gutter can show the diagnostic in line.
 *
 * Detection is regex-based and intentionally conservative: only
 * canonical declarations match.  Custom matchers can be plugged in
 * via `RegisterDetector` for project-specific frameworks.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace polyglot::tools::ui::testing {

enum class LensAction {
  kRun,
  kDebug,
};

struct Lens {
  int line{0};                 ///< 1-based.
  std::string symbol;          ///< Test function / method name.
  std::string framework;       ///< "catch2", "pytest", "cargo", "junit", "xunit", "nunit".
  std::vector<LensAction> actions;
  std::optional<std::string> failure_message;
};

using LensDetector =
    std::function<std::vector<Lens>(const std::string &content)>;

class InlineTestLens {
 public:
  InlineTestLens();

  /// Register or replace a detector for one language extension
  /// (without the dot, lower-case): "cpp", "py", "rs", "java", "cs".
  void RegisterDetector(std::string extension, LensDetector detector);

  /// Compute the lens list for a file.  Built-in detectors recognise
  /// Catch2 (`TEST_CASE` / `TEST`), pytest (`def test_*`), cargo
  /// (`#[test]`), JUnit (`@Test`), xUnit (`[Fact]` / `[Theory]`)
  /// and NUnit (`[Test]`).
  std::vector<Lens> ComputeForFile(const std::string &file_path,
                                   const std::string &content) const;

  /// Stamp a failure message onto the lens for `(file, line)` so
  /// the IDE renders it inline.  Returns true when a matching lens
  /// is found in the most recent `ComputeForFile` cache.
  bool RecordFailure(const std::string &file_path, int line,
                     const std::string &message);

  /// The most recently computed lens list for `file_path`, used by
  /// the IDE to refresh inline diagnostics without re-scanning.
  std::vector<Lens> Cached(const std::string &file_path) const;

 private:
  static std::string Extension(const std::string &path);

  std::unordered_map<std::string, LensDetector> detectors_;
  mutable std::unordered_map<std::string, std::vector<Lens>> cache_;
};

}  // namespace polyglot::tools::ui::testing
