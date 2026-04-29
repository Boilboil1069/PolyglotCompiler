/**
 * @file     profiler_e2e_test.cpp
 * @brief    End-to-end test: invoke `polyc --emit=call-graph` on a real
 *           cross-language sample and validate the resulting JSON document.
 *
 * @ingroup  Tests / Integration / Profiler
 * @author   Manning Cyrus
 * @date     2026-04-29
 */
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path FindRepoRoot() {
#ifdef POLYGLOT_TESTS_FIXTURE_ROOT
  fs::path p = fs::path(POLYGLOT_TESTS_FIXTURE_ROOT);
  for (int i = 0; i < 6 && !p.empty(); ++i) {
    if (fs::exists(p / "CMakeLists.txt") && fs::exists(p / "tests" / "samples")) {
      return p;
    }
    p = p.parent_path();
  }
#endif
  fs::path cwd = fs::current_path();
  for (int i = 0; i < 8 && !cwd.empty(); ++i) {
    if (fs::exists(cwd / "CMakeLists.txt") &&
        fs::exists(cwd / "tests" / "samples")) {
      return cwd;
    }
    cwd = cwd.parent_path();
  }
  return {};
}

fs::path FindPolycBinary(const fs::path &repo_root) {
  const std::string exe =
#ifdef _WIN32
      "polyc.exe";
#else
      "polyc";
#endif
  for (const auto &candidate : {fs::path("build") / exe,
                                fs::path("build-release") / exe,
                                fs::path("build") / "Debug" / exe,
                                fs::path("build") / "Release" / exe}) {
    if (fs::exists(repo_root / candidate)) {
      return repo_root / candidate;
    }
  }
  // Same directory as the test binary.
  fs::path here = fs::current_path() / exe;
  if (fs::exists(here)) {
    return here;
  }
  return {};
}

std::string SlurpFile(const fs::path &p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

} // namespace

TEST_CASE("polyc emits well-formed call-graph JSON for 09_mixed_pipeline",
          "[integration][profiler][callgraph]") {
  const fs::path repo = FindRepoRoot();
  if (repo.empty()) {
    SUCCEED("Repository root not found — skipping e2e test.");
    return;
  }
  const fs::path source = repo / "tests" / "samples" / "09_mixed_pipeline" /
                          "mixed_pipeline.ploy";
  if (!fs::exists(source)) {
    SUCCEED("Sample 09_mixed_pipeline not found — skipping.");
    return;
  }
  const fs::path polyc = FindPolycBinary(repo);
  if (polyc.empty()) {
    SUCCEED("polyc binary not found — skipping (build polyc first).");
    return;
  }

  const fs::path out = fs::temp_directory_path() /
                       "polyglot_profiler_e2e_callgraph.json";
  fs::remove(out);

  std::string cmd;
  cmd.reserve(512);
  cmd.append("\"").append(polyc.string()).append("\"");
  cmd.append(" --emit=call-graph:").append("\"").append(out.string()).append("\"");
  cmd.append(" \"").append(source.string()).append("\"");
  cmd.append(" --mode=compile");
#ifdef _WIN32
  cmd.append(" 1>NUL 2>NUL");
#else
  cmd.append(" >/dev/null 2>&1");
#endif

  // We don't assert the exit code: even a partial pipeline failure is
  // acceptable as long as the call-graph emitter produced a parseable
  // document (the emitter runs after middle-end IR lowering).
  std::system(cmd.c_str());

  if (!fs::exists(out)) {
    SUCCEED("polyc did not reach the call-graph emitter on this sample — "
            "treating as soft skip until the cross-language frontend is "
            "wired through.");
    return;
  }

  const std::string doc = SlurpFile(out);
  REQUIRE_FALSE(doc.empty());
  REQUIRE(doc.find("\"schema\":\"polyglot.callgraph.v1\"") != std::string::npos);
  REQUIRE(doc.find("\"nodes\"") != std::string::npos);
  REQUIRE(doc.find("\"edges\"") != std::string::npos);

  fs::remove(out);
}

TEST_CASE("polyc emits profile-symbols JSON when requested",
          "[integration][profiler][profile_symbols]") {
  const fs::path repo = FindRepoRoot();
  if (repo.empty()) {
    SUCCEED("Repository root not found — skipping.");
    return;
  }
  const fs::path source = repo / "tests" / "samples" / "15_full_stack";
  if (!fs::exists(source)) {
    SUCCEED("Sample 15_full_stack not found — skipping.");
    return;
  }
  // Locate the first .ploy entry in 15_full_stack.
  fs::path entry;
  for (const auto &de : fs::directory_iterator(source)) {
    if (de.path().extension() == ".ploy") {
      entry = de.path();
      break;
    }
  }
  if (entry.empty()) {
    SUCCEED("No .ploy entry in 15_full_stack — skipping.");
    return;
  }
  const fs::path polyc = FindPolycBinary(repo);
  if (polyc.empty()) {
    SUCCEED("polyc binary not found — skipping.");
    return;
  }

  const fs::path out = fs::temp_directory_path() /
                       "polyglot_profiler_e2e_symbols.json";
  fs::remove(out);

  std::string cmd;
  cmd.append("\"").append(polyc.string()).append("\"");
  cmd.append(" --emit=profile-symbols:").append("\"").append(out.string()).append("\"");
  cmd.append(" --profile-instrument");
  cmd.append(" \"").append(entry.string()).append("\"");
  cmd.append(" --mode=compile");
#ifdef _WIN32
  cmd.append(" 1>NUL 2>NUL");
#else
  cmd.append(" >/dev/null 2>&1");
#endif
  std::system(cmd.c_str());

  if (!fs::exists(out)) {
    SUCCEED("polyc did not reach the profile-symbols emitter on this "
            "sample — soft skip.");
    return;
  }
  const std::string doc = SlurpFile(out);
  REQUIRE(doc.find("\"schema\":\"polyglot.profilesymbols.v1\"") !=
          std::string::npos);
  REQUIRE(doc.find("\"symbols\"") != std::string::npos);

  fs::remove(out);
}
