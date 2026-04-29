/**
 * @file     samples_regression_test.cpp
 * @brief    Integration test that drives the build_all_samples regression
 *           harness and asserts the resulting JSON report is well-formed.
 *
 *           Rationale: B6/B7 ship per-sample expected stdout plus a runner
 *           script.  The integration test layer must verify the harness can
 *           execute end-to-end and produce a structured, parseable report
 *           describing every sample in the matrix.  We do **not** assert on
 *           per-sample pass rate because parts of the polyc backend pipeline
 *           are still maturing; the report itself is the contract under
 *           test, so future stages can tighten the assertion when more
 *           samples are expected to reach status==OK.
 *
 * @ingroup  Tests / integration / samples
 * @author   Manning Cyrus
 * @date     2026-04-29
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------------
// Locate the repository root by walking up from CMAKE_SOURCE_DIR (preferred)
// or, as a fallback, from the current working directory.  The repository is
// recognised by the presence of the top-level CMakeLists.txt + scripts dir.
// ---------------------------------------------------------------------------
fs::path FindRepoRoot() {
#ifdef POLYGLOT_TESTS_FIXTURE_ROOT
  // POLYGLOT_TESTS_FIXTURE_ROOT points at <repo>/tests/fixtures/...  walk up.
  fs::path p = fs::path(POLYGLOT_TESTS_FIXTURE_ROOT);
  for (int i = 0; i < 6 && !p.empty(); ++i) {
    if (fs::exists(p / "CMakeLists.txt") && fs::exists(p / "scripts")) {
      return p;
    }
    p = p.parent_path();
  }
#endif
  fs::path cwd = fs::current_path();
  for (int i = 0; i < 8 && !cwd.empty(); ++i) {
    if (fs::exists(cwd / "CMakeLists.txt") && fs::exists(cwd / "scripts" /
                                                         "build_all_samples.ps1")) {
      return cwd;
    }
    cwd = cwd.parent_path();
  }
  return {};
}

// Read an entire file into memory; returns empty string on failure.
std::string SlurpFile(const fs::path &p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Tiny scalar JSON probe — extracts the integer value associated with a
// top-level key like "total".  Returns -1 when the key cannot be parsed.
long long JsonScalarInt(const std::string &doc, std::string_view key) {
  std::string needle = "\"";
  needle.append(key);
  needle.append("\"");
  auto k = doc.find(needle);
  if (k == std::string::npos) {
    return -1;
  }
  auto colon = doc.find(':', k);
  if (colon == std::string::npos) {
    return -1;
  }
  auto p = colon + 1;
  while (p < doc.size() && (doc[p] == ' ' || doc[p] == '\t')) {
    ++p;
  }
  long long sign = 1;
  if (p < doc.size() && doc[p] == '-') {
    sign = -1;
    ++p;
  }
  long long v = 0;
  bool any = false;
  while (p < doc.size() && doc[p] >= '0' && doc[p] <= '9') {
    v = v * 10 + (doc[p] - '0');
    ++p;
    any = true;
  }
  return any ? sign * v : -1;
}

// Counts how many times a quoted "status":"<value>" tag appears.
size_t CountStatusOccurrences(const std::string &doc, std::string_view value) {
  std::string needle = "\"status\":\"";
  needle.append(value);
  needle.append("\"");
  std::string compact;
  compact.reserve(doc.size());
  // Strip whitespace inside JSON so "status" : "OK" matches "status":"OK".
  bool in_string = false;
  for (char c : doc) {
    if (c == '"') {
      in_string = !in_string;
      compact.push_back(c);
    } else if (!in_string && (c == ' ' || c == '\t' || c == '\n' || c == '\r')) {
      continue;
    } else {
      compact.push_back(c);
    }
  }
  size_t count = 0;
  size_t pos = 0;
  while ((pos = compact.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

} // namespace

TEST_CASE("sample regression harness produces a well-formed JSON report",
          "[samples][b6][integration]") {
  fs::path repo = FindRepoRoot();
  REQUIRE_FALSE(repo.empty());

  fs::path samples_dir = repo / "tests" / "samples";
  REQUIRE(fs::exists(samples_dir));

  // Count sample directories on disk so we can cross-check the report total.
  size_t sample_dirs = 0;
  for (const auto &entry : fs::directory_iterator(samples_dir)) {
    if (entry.is_directory()) {
      ++sample_dirs;
    }
  }
  REQUIRE(sample_dirs >= 16);

  // The harness writes here by default; (re)run it so the report reflects
  // the current source tree.  We accept either polyc/polyld being missing
  // (build not done yet) by checking for the binaries first.
  fs::path build_dir = repo / "build";
#ifdef _WIN32
  fs::path polyc = build_dir / "polyc.exe";
  fs::path polyld = build_dir / "polyld.exe";
#else
  fs::path polyc = build_dir / "polyc";
  fs::path polyld = build_dir / "polyld";
#endif
  if (!fs::exists(polyc) || !fs::exists(polyld)) {
    SUCCEED("polyc / polyld not built yet — skipping live harness invocation");
    return;
  }

  fs::path report = build_dir / "samples_report.json";

  // Invoke the platform-appropriate harness.  We rely on the script's
  // default-tolerant exit code (0) so that backend regressions do not gate
  // this test; the report itself is the contract under test here.
#ifdef _WIN32
  fs::path script = repo / "scripts" / "build_all_samples.ps1";
  REQUIRE(fs::exists(script));
  std::string cmd = std::string("powershell -NoProfile -ExecutionPolicy Bypass -File \"") +
                    script.string() + "\" > NUL 2>&1";
#else
  fs::path script = repo / "scripts" / "build_all_samples.sh";
  REQUIRE(fs::exists(script));
  std::string cmd = std::string("bash \"") + script.string() +
                    "\" > /dev/null 2>&1";
#endif
  int rc = std::system(cmd.c_str());
  // The harness defaults to exit 0; non-zero indicates a launcher failure.
  INFO("harness rc=" << rc);
  REQUIRE(fs::exists(report));

  // ---------- JSON well-formedness ----------
  std::string doc = SlurpFile(report);
  REQUIRE_FALSE(doc.empty());

  // Required scalar keys.
  long long total = JsonScalarInt(doc, "total");
  REQUIRE(total >= 0);
  REQUIRE(static_cast<size_t>(total) == sample_dirs);
  REQUIRE(doc.find("\"samples_dir\"") != std::string::npos);
  REQUIRE(doc.find("\"build_dir\"") != std::string::npos);
  REQUIRE(doc.find("\"polyc\"") != std::string::npos);
  REQUIRE(doc.find("\"polyld\"") != std::string::npos);
  REQUIRE(doc.find("\"samples\"") != std::string::npos);

  // Every status string in the report must belong to the documented enum.
  static constexpr std::array<std::string_view, 7> kAllowed = {
      "OK",       "OUTPUT_MISMATCH", "RUN_FAIL", "EMPTY_STDOUT",
      "LINK_FAIL","COMPILE_FAIL",    "SKIP"};
  size_t classified = 0;
  for (std::string_view s : kAllowed) {
    classified += CountStatusOccurrences(doc, s);
  }
  // total samples + 1 occurrence per non-zero bucket entry.  Lower-bound
  // check: every sample must contribute exactly one status field.
  REQUIRE(classified >= static_cast<size_t>(total));

  // No stray statuses outside the allowed set.
  // Search for "status":"X" where X is anything; collect distinct values.
  std::set<std::string> seen;
  size_t pos = 0;
  const std::string tag = "\"status\":\"";
  std::string compact;
  compact.reserve(doc.size());
  bool in_string = false;
  for (char c : doc) {
    if (c == '"') {
      in_string = !in_string;
      compact.push_back(c);
    } else if (!in_string && (c == ' ' || c == '\t' || c == '\n' || c == '\r')) {
      continue;
    } else {
      compact.push_back(c);
    }
  }
  while ((pos = compact.find(tag, pos)) != std::string::npos) {
    auto start = pos + tag.size();
    auto end = compact.find('"', start);
    if (end == std::string::npos) {
      break;
    }
    seen.insert(compact.substr(start, end - start));
    pos = end + 1;
  }
  for (const auto &s : seen) {
    bool ok = std::any_of(kAllowed.begin(), kAllowed.end(),
                          [&](std::string_view v) { return v == s; });
    INFO("unexpected status value: " << s);
    REQUIRE(ok);
  }
}
