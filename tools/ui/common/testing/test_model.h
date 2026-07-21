/**
 * @file     test_model.h
 * @brief    Hierarchical test-tree value model + report parsers.
 *
 * `TestNode` represents one entry in the tree (project → suite →
 * case).  `TestModel` indexes nodes by stable id, tracks status and
 * diagnostics, and exposes traversal helpers used by the IDE shell
 * to render the Test Explorer panel.
 *
 * Five report formats are parsed in this translation unit:
 * CTest (CDash-style XML), JUnit, pytest (JUnit superset),
 * cargo test (libtest JSON line stream), and xUnit/NUnit
 * (xUnit v2 XML, NUnit 3 XML).
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace polyglot::tools::ui::testing {

enum class TestStatus {
  kPending,
  kRunning,
  kPassed,
  kFailed,
  kSkipped,
  kErrored,
};

enum class TestKind {
  kProject,
  kSuite,
  kCase,
};

struct TestLocation {
  std::string file;
  int line{0};
};

struct TestNode {
  std::string id;             ///< Stable, unique within the model.
  std::string parent_id;      ///< Empty for project roots.
  std::string label;
  TestKind kind{TestKind::kCase};
  TestStatus status{TestStatus::kPending};
  TestLocation location;
  std::chrono::milliseconds duration{0};
  std::string failure_message;
  std::string framework;      ///< "ctest", "pytest", "cargo", "xunit", …
};

class TestModel {
 public:
  /// Insert or overwrite a node.  Caller-owned ids must be unique.
  void Upsert(TestNode node);

  /// Total node count (all kinds).
  std::size_t size() const noexcept { return nodes_.size(); }

  const TestNode *Find(const std::string &id) const;

  /// Children of `parent_id` in insertion order.  Pass an empty
  /// string to enumerate the project roots.
  std::vector<const TestNode *> Children(const std::string &parent_id) const;

  /// Update only the status / duration / failure message of a node.
  void MarkStatus(const std::string &id, TestStatus status,
                  std::chrono::milliseconds duration = {},
                  std::string failure_message = "");

  /// Failure-first ordering for the "failed only" Test Explorer
  /// filter: failures first, then errors, then everything else;
  /// stable across reruns.
  std::vector<const TestNode *> FailedFirst() const;

  /// Aggregate a status summary for the entire model (or for the
  /// subtree rooted at `root_id`).
  struct Summary {
    std::size_t passed{0};
    std::size_t failed{0};
    std::size_t skipped{0};
    std::size_t errored{0};
    std::size_t pending{0};
  };
  Summary Aggregate(const std::string &root_id = {}) const;

 private:
  std::unordered_map<std::string, TestNode> nodes_;
  std::vector<std::string> insertion_order_;
};

// ---------------------------------------------------------------------------
// Report parsers — each yields a flat list of `TestNode`s ready to
// be merged into a `TestModel` via `Upsert`.
// ---------------------------------------------------------------------------

/// Parse a CTest CDash-style XML report.
std::vector<TestNode> ParseCTestReport(const std::string &xml);

/// Parse a JUnit / pytest XML report.
std::vector<TestNode> ParseJUnitReport(const std::string &xml);

/// Parse a `cargo test -- -Z unstable-options --format json` line
/// stream (one JSON object per line).
std::vector<TestNode> ParseCargoReport(const std::string &json_lines);

/// Parse an xUnit v2 XML report (`<assemblies><assembly><test>`).
std::vector<TestNode> ParseXUnitReport(const std::string &xml);

/// Parse an NUnit 3 XML report (`<test-run><test-suite><test-case>`).
std::vector<TestNode> ParseNUnitReport(const std::string &xml);

}  // namespace polyglot::tools::ui::testing
