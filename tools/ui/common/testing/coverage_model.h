/**
 * @file     coverage_model.h
 * @brief    Coverage report parsers + per-file aggregate.
 *
 * `CoverageModel` consumes coverage reports in five formats and
 * exposes them as a uniform per-file `FileCoverage` value:
 *
 *   * lcov tracefile (`SF:` records)
 *   * Cobertura XML  (`<class>` / `<lines><line>`)
 *   * coverage.py XML (also Cobertura-shaped)
 *   * cargo-tarpaulin JSON
 *   * dotnet coverlet JSON
 *
 * The IDE shell renders gutter bars from `LineHits`, the workspace
 * tree from `Files()`, and the threshold-alert badges from
 * `OverallPercent` / `BelowThreshold`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace polyglot::tools::ui::testing {

enum class CoverageFormat {
  kLcov,
  kCobertura,
  kCoveragePy,
  kTarpaulin,
  kCoverlet,
};

struct FileCoverage {
  std::string file;
  std::map<int, int> line_hits;     ///< Line number → hit count (0 = miss).

  std::size_t covered_lines() const;
  std::size_t total_lines() const;
  double percent() const;
};

class CoverageModel {
 public:
  /// Detect format from the report text and load.  Falls back to
  /// `kLcov` for plain-text reports and `kCobertura` for XML when
  /// the schema is ambiguous.
  bool Load(const std::string &text);

  /// Force a specific format.
  bool Load(const std::string &text, CoverageFormat format);

  /// Per-file results, sorted alphabetically by path.
  std::vector<const FileCoverage *> Files() const;
  const FileCoverage *Find(const std::string &file) const;

  /// Workspace-wide percentage averaged across files.
  double OverallPercent() const;

  /// Files whose coverage is strictly below `threshold_percent`.
  std::vector<const FileCoverage *> BelowThreshold(
      double threshold_percent) const;

 private:
  void IngestLcov(const std::string &text);
  void IngestCobertura(const std::string &text);
  void IngestTarpaulin(const std::string &text);
  void IngestCoverlet(const std::string &text);

  std::unordered_map<std::string, FileCoverage> files_;
};

/// Best-effort detection from the leading bytes of the report.
CoverageFormat DetectCoverageFormat(const std::string &text);

}  // namespace polyglot::tools::ui::testing
