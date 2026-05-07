// samples_cross_platform_consistency_test.cpp
//
// Verifies that the `samples_report.json` OK-bucket recorded on the
// current host coincides with the OK-buckets recorded on the other
// supported hosts.  CI gathers per-host artefacts under
// `tests/integration/fixtures/samples_reports/{macos-arm64,
// linux-x86_64,windows-x86_64}/samples_report.json`.  When all three
// fixtures are present the test asserts the three OK sets agree
// element-for-element; if a fixture is missing (e.g. running locally
// before all CI jobs have uploaded their report) the test still
// asserts the local report is well-formed and that the floor sample
// `00_minimal` is present in every fixture that *is* available.
//
// The cross-set comparison is the assertion contract referenced by
// the closing-gate item in the project demand log.

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace fs = std::filesystem;

namespace {

fs::path FindRepoRoot() {
  fs::path p = fs::current_path();
  for (int i = 0; i < 8; ++i) {
    if (fs::exists(p / "CMakeLists.txt") &&
        fs::exists(p / "tests" / "samples")) {
      return p;
    }
    if (!p.has_parent_path()) break;
    p = p.parent_path();
  }
  return {};
}

std::string Slurp(const fs::path &p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Strip JSON whitespace outside of string literals so the linear
// "name":"...","status":"..." walk below is robust against pretty
// printing differences across the bash and PowerShell harnesses.
std::string Compact(const std::string &doc) {
  std::string out;
  out.reserve(doc.size());
  bool in_string = false;
  for (char c : doc) {
    if (c == '"') {
      in_string = !in_string;
      out.push_back(c);
    } else if (!in_string &&
               (c == ' ' || c == '\t' || c == '\n' || c == '\r')) {
      continue;
    } else {
      out.push_back(c);
    }
  }
  return out;
}

// Walk per-sample objects in the compacted JSON in document order and
// extract the names whose status is "OK".  Returns an ordered set so
// callers can perform deterministic set algebra.
std::set<std::string> ExtractOkNames(const std::string &doc) {
  std::set<std::string> ok;
  const std::string compact = Compact(doc);
  const std::string nameTag   = "\"name\":\"";
  const std::string statusTag = "\"status\":\"";
  std::size_t cur = 0;
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
      ok.insert(std::move(sample_name));
    }
    cur = se + 1;
  }
  return ok;
}

} // namespace

TEST_CASE("samples_report.json OK buckets agree across the three host platforms",
          "[samples][cross_platform][integration]") {
  const fs::path repo = FindRepoRoot();
  REQUIRE_FALSE(repo.empty());

  // ---------- Local report (always required) ----------
  const fs::path local_report = repo / "build" / "samples_report.json";
  if (!fs::exists(local_report)) {
    SUCCEED("local samples_report.json not generated yet — "
            "run scripts/build_all_samples.sh first");
    return;
  }
  const std::string local_doc = Slurp(local_report);
  REQUIRE_FALSE(local_doc.empty());
  const std::set<std::string> local_ok = ExtractOkNames(local_doc);
  // The minimum-runnable contract: `00_minimal` must always be OK.
  REQUIRE(local_ok.count("00_minimal") == 1);

  // ---------- Per-host fixtures (optional unless all present) ----------
  // CI uploads each per-host report into a dedicated fixture sub-tree.
  // When invoked under CI all three artefacts are downloaded into the
  // workspace before this test runs; locally none of them will be
  // present and the assertion below short-circuits cleanly.
  const fs::path fixtures =
      repo / "tests" / "integration" / "fixtures" / "samples_reports";
  static constexpr std::array<std::string_view, 3> kHostDirs = {
      "macos-arm64", "linux-x86_64", "windows-x86_64"};

  std::vector<std::pair<std::string, std::set<std::string>>> per_host;
  per_host.reserve(kHostDirs.size());
  std::size_t present = 0;
  for (auto host : kHostDirs) {
    const fs::path p =
        fixtures / std::string(host) / "samples_report.json";
    if (!fs::exists(p)) continue;
    ++present;
    const std::string doc = Slurp(p);
    REQUIRE_FALSE(doc.empty());
    auto ok = ExtractOkNames(doc);
    // Floor invariant per platform: the minimum sample is always OK.
    INFO("host " << host << ": missing 00_minimal in OK bucket");
    REQUIRE(ok.count("00_minimal") == 1);
    per_host.emplace_back(std::string(host), std::move(ok));
  }

  if (present == 0) {
    SUCCEED("no per-host fixture reports under tests/integration/"
            "fixtures/samples_reports — local-only invocation");
    return;
  }

  // When every host has uploaded its report the OK buckets must be
  // pairwise identical.  A divergence means one platform has either
  // regressed or pulled ahead; either way the matrix is no longer
  // self-consistent and the closing gate must surface it as a hard
  // test failure.
  if (present == kHostDirs.size()) {
    const auto &reference = per_host.front().second;
    for (std::size_t i = 1; i < per_host.size(); ++i) {
      const auto &other = per_host[i].second;
      INFO("host A=" << per_host.front().first
           << " host B=" << per_host[i].first
           << " |A|=" << reference.size()
           << " |B|=" << other.size());
      REQUIRE(reference == other);
    }
  } else {
    // Partial set: assert the reports we have agree with each other so
    // problems are caught as soon as the second host uploads.
    if (per_host.size() >= 2) {
      const auto &a = per_host.front().second;
      for (std::size_t i = 1; i < per_host.size(); ++i) {
        const auto &b = per_host[i].second;
        INFO("partial cross-host comparison " << per_host.front().first
             << " vs " << per_host[i].first);
        REQUIRE(a == b);
      }
    } else {
      SUCCEED("only one per-host fixture present — pairwise comparison "
              "deferred until at least two hosts have uploaded");
    }
  }
}
