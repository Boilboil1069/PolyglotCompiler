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
#include <vector>

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

  // Invoke the platform-appropriate harness with --require-min-ok 1 so
  // a regression that drops the OK bucket below the minimum-runnable
  // contract (`00_minimal/print_then_exit`) surfaces as an exit-code 1
  // failure here, not just as a quietly degraded report.
#ifdef _WIN32
  fs::path script = repo / "scripts" / "build_all_samples.ps1";
  REQUIRE(fs::exists(script));
  std::string cmd = std::string("powershell -NoProfile -ExecutionPolicy Bypass -File \"") +
                    script.string() + "\" -RequireMinOk 1 > NUL 2>&1";
#else
  fs::path script = repo / "scripts" / "build_all_samples.sh";
  REQUIRE(fs::exists(script));
  std::string cmd = std::string("bash \"") + script.string() +
                    "\" --require-min-ok 1 > /dev/null 2>&1";
#endif
  int rc = std::system(cmd.c_str());
  // The --require-min-ok 1 gate must pass: if the shared minimum sample
  // (`00_minimal/print_then_exit`) is no longer reachable end-to-end,
  // the harness exits 1 and the test must fail loudly.
  INFO("harness rc=" << rc);
  REQUIRE(rc == 0);
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

  // ---------- Minimum-runnable contract ----------
  // Walk the per-sample objects in the compacted JSON and collect the
  // set of names whose status is "OK".  We do this by lock-stepping
  // "name":"..." with the matching "status":"..." that follows.
  std::vector<std::string> ok_walk;
  {
    size_t cur = 0;
    const std::string nameTag = "\"name\":\"";
    const std::string statusTag = "\"status\":\"";
    while ((cur = compact.find(nameTag, cur)) != std::string::npos) {
      auto ns = cur + nameTag.size();
      auto ne = compact.find('"', ns);
      if (ne == std::string::npos) break;
      std::string sample_name = compact.substr(ns, ne - ns);
      auto sp = compact.find(statusTag, ne);
      if (sp == std::string::npos) break;
      auto ss = sp + statusTag.size();
      auto se = compact.find('"', ss);
      if (se == std::string::npos) break;
      std::string sample_status = compact.substr(ss, se - ss);
      if (sample_status == "OK") {
        ok_walk.push_back(std::move(sample_name));
      }
      cur = se + 1;
    }
  }
  // The minimum-runnable contract: the shared sample
  // `00_minimal` (entry `print_then_exit.ploy`) MUST land in the OK
  // bucket on every supported platform.  This is the floor for the
  // `--require-min-ok 1` gate enforced by the harness.
  REQUIRE_FALSE(ok_walk.empty());
  bool has_minimum = std::any_of(
      ok_walk.begin(), ok_walk.end(),
      [](const std::string &n) { return n == "00_minimal"; });
  INFO("OK names: " << [&] {
    std::string acc;
    for (const auto &n : ok_walk) { acc += n; acc += ' '; }
    return acc;
  }());
  REQUIRE(has_minimum);

  // ---------- ok[] array consistency ----------
  // The harness emits a sorted `ok` array alongside the `samples`
  // array.  Parse it and assert that it matches the OK-set we walked
  // out of the per-sample status fields.  This guards against the
  // harness and the test drifting on what "OK" means.
  std::vector<std::string> ok_array;
  {
    auto ka = compact.find("\"ok\":[");
    if (ka != std::string::npos) {
      auto end_ka = compact.find(']', ka);
      if (end_ka != std::string::npos) {
        std::string body = compact.substr(ka + 6, end_ka - (ka + 6));
        size_t p = 0;
        while ((p = body.find('"', p)) != std::string::npos) {
          auto e = body.find('"', p + 1);
          if (e == std::string::npos) break;
          ok_array.push_back(body.substr(p + 1, e - (p + 1)));
          p = e + 1;
        }
      }
    }
  }
  REQUIRE_FALSE(ok_array.empty());
  std::vector<std::string> ok_walk_sorted = ok_walk;
  std::sort(ok_walk_sorted.begin(), ok_walk_sorted.end());
  // The `ok` array contract: byte-for-byte equality with the sorted
  // OK-set walked out of the `samples` array.
  REQUIRE(ok_array == ok_walk_sorted);
}
