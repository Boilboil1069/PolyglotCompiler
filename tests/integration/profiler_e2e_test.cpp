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
  fs::path newest;
  fs::file_time_type newest_mtime{};
  for (const auto &candidate : {fs::path("build") / exe,
                                fs::path("build-release") / exe,
                                fs::path("build") / "Debug" / exe,
                                fs::path("build") / "Release" / exe}) {
    const fs::path binary = repo_root / candidate;
    std::error_code ec;
    const auto mtime = fs::last_write_time(binary, ec);
    if (!ec && (newest.empty() || mtime > newest_mtime)) {
      newest = binary;
      newest_mtime = mtime;
    }
  }
  if (!newest.empty()) {
    return newest;
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

TEST_CASE("polyc canonicalizes Poly CLI language selection",
          "[integration][polyc][poly][compat]") {
  const fs::path repo = FindRepoRoot();
  if (repo.empty()) {
    SUCCEED("Repository root not found — skipping.");
    return;
  }
  const fs::path polyc = FindPolycBinary(repo);
  if (polyc.empty()) {
    SUCCEED("polyc binary not found — skipping (build polyc first).");
    return;
  }

  const fs::path scratch = fs::temp_directory_path() / "polyglot_poly_cli_identity";
  std::error_code ec;
  fs::create_directories(scratch, ec);
  REQUIRE_FALSE(ec);
  const fs::path source = scratch / "canonical.poly";
  {
    std::ofstream out(source, std::ios::binary);
    REQUIRE(out.good());
    out << "FUNC main() -> i32 { RETURN 0; }\n";
  }
  const fs::path legacy_source = scratch / "legacy.ploy";
  fs::copy_file(source, legacy_source, fs::copy_options::overwrite_existing, ec);
  REQUIRE_FALSE(ec);

  const auto run = [&](const std::string &tag, const std::string &lang_flag,
                       const fs::path &input) {
    const fs::path object = scratch / (tag + ".pobj");
    const fs::path log = scratch / (tag + ".log");
    fs::remove(object, ec);
    fs::remove(log, ec);

    std::string cmd = "\"" + polyc.string() + "\" \"" + input.string() +
                      "\" --emit-obj=\"" + object.string() + "\"";
    if (!lang_flag.empty())
      cmd += " --lang=" + lang_flag;
    cmd += " >\"" + log.string() + "\" 2>&1";

    INFO("command: " << cmd);
    REQUIRE(std::system(cmd.c_str()) == 0);
    REQUIRE(fs::exists(object));
    const std::string output = SlurpFile(log);
    CHECK(output.find("[polyc] Language: poly") != std::string::npos);
  };

  SECTION("canonical .poly auto-detection") { run("auto", "", source); }
  SECTION("explicit canonical --lang=poly") {
    run("explicit_poly", "poly", source);
  }
  SECTION("legacy --lang=ploy canonicalizes before the pipeline") {
    run("legacy_ploy", "ploy", source);
  }
  SECTION("legacy --lang=Ploy canonicalizes before the pipeline") {
    run("legacy_Ploy", "Ploy", source);
  }
  SECTION("legacy .ploy auto-detection canonicalizes before the pipeline") {
    run("legacy_extension", "", legacy_source);
  }
}

TEST_CASE("polyc emits an instrumented call graph for canonical Poly",
          "[integration][profiler][callgraph][poly]") {
  const fs::path repo = FindRepoRoot();
  if (repo.empty()) {
    SUCCEED("Repository root not found — skipping e2e test.");
    return;
  }
  const fs::path polyc = FindPolycBinary(repo);
  if (polyc.empty()) {
    SUCCEED("polyc binary not found — skipping (build polyc first).");
    return;
  }

  const fs::path scratch = fs::temp_directory_path() / "polyglot_profiler_e2e";
  std::error_code ec;
  fs::create_directories(scratch, ec);
  REQUIRE_FALSE(ec);
  const fs::path source = scratch / "canonical.poly";
  const fs::path out = scratch / "canonical_callgraph.json";
  const fs::path symbols = scratch / "canonical_symbols.json";
  const fs::path object = scratch / "canonical.o";
  const fs::path log = scratch / "canonical.log";
  {
    std::ofstream source_out(source, std::ios::binary | std::ios::trunc);
    REQUIRE(source_out.good());
    source_out << "FUNC main() -> i32 { RETURN 0; }\n";
  }
  fs::remove(out);
  fs::remove(symbols);
  fs::remove(object);
  fs::remove(log);

  std::string cmd;
  cmd.reserve(512);
  cmd.append("\"").append(polyc.string()).append("\"");
  cmd.append(" --emit=call-graph:").append("\"").append(out.string()).append("\"");
  cmd.append(" --emit=profile-symbols:").append("\"").append(symbols.string()).append("\"");
  cmd.append(" --profile-instrument");
  cmd.append(" --lang=poly");
  cmd.append(" \"").append(source.string()).append("\"");
  cmd.append(" --mode=compile");
  cmd.append(" --emit-obj=\"").append(object.string()).append("\"");
  cmd.append(" --no-package-index --no-aux");
#ifdef _WIN32
  cmd.append(" >\"").append(log.string()).append("\" 2>&1");
#else
  cmd.append(" >\"").append(log.string()).append("\" 2>&1");
#endif

  INFO("command: " << cmd);
  const int exit_code = std::system(cmd.c_str());
  INFO("polyc output: " << SlurpFile(log));
  REQUIRE(exit_code == 0);
  REQUIRE(fs::exists(object));
  REQUIRE(fs::exists(out));
  REQUIRE(fs::exists(symbols));

  const std::string doc = SlurpFile(out);
  REQUIRE_FALSE(doc.empty());
  REQUIRE(doc.find("\"schema\":\"polyglot.callgraph.v1\"") != std::string::npos);
  REQUIRE(doc.find("\"nodes\"") != std::string::npos);
  REQUIRE(doc.find("\"edges\"") != std::string::npos);
  REQUIRE(doc.find("\"language\":\"poly\"") != std::string::npos);
  REQUIRE(doc.find("\"language\":\"ploy\"") == std::string::npos);
  REQUIRE(doc.find("__ploy_rt_call_enter") != std::string::npos);
  REQUIRE(doc.find("__ploy_rt_call_exit") != std::string::npos);

  const std::string symbols_doc = SlurpFile(symbols);
  REQUIRE(symbols_doc.find("\"schema\":\"polyglot.profilesymbols.v1\"") !=
          std::string::npos);
  REQUIRE(symbols_doc.find("\"language\":\"poly\"") != std::string::npos);
  REQUIRE(symbols_doc.find("\"language\":\"ploy\"") == std::string::npos);

  fs::remove(out);
}

TEST_CASE("polyc emits canonical profile symbols for legacy Poly input",
          "[integration][profiler][profile_symbols][poly][compat]") {
  const fs::path repo = FindRepoRoot();
  if (repo.empty()) {
    SUCCEED("Repository root not found — skipping.");
    return;
  }
  const fs::path polyc = FindPolycBinary(repo);
  if (polyc.empty()) {
    SUCCEED("polyc binary not found — skipping.");
    return;
  }

  const fs::path scratch = fs::temp_directory_path() / "polyglot_profiler_e2e";
  std::error_code ec;
  fs::create_directories(scratch, ec);
  REQUIRE_FALSE(ec);
  const fs::path source = scratch / "legacy.ploy";
  const fs::path out = scratch / "legacy_symbols.json";
  const fs::path call_graph = scratch / "legacy_callgraph.json";
  const fs::path object = scratch / "legacy.o";
  const fs::path log = scratch / "legacy.log";
  {
    std::ofstream source_out(source, std::ios::binary | std::ios::trunc);
    REQUIRE(source_out.good());
    source_out << "FUNC main() -> i32 { RETURN 0; }\n";
  }
  fs::remove(out);
  fs::remove(call_graph);
  fs::remove(object);
  fs::remove(log);

  std::string cmd;
  cmd.append("\"").append(polyc.string()).append("\"");
  cmd.append(" --emit=profile-symbols:").append("\"").append(out.string()).append("\"");
  cmd.append(" --emit=call-graph:").append("\"").append(call_graph.string()).append("\"");
  cmd.append(" --profile-instrument");
  cmd.append(" --lang=ploy");
  cmd.append(" \"").append(source.string()).append("\"");
  cmd.append(" --mode=compile");
  cmd.append(" --emit-obj=\"").append(object.string()).append("\"");
  cmd.append(" --no-package-index --no-aux");
#ifdef _WIN32
  cmd.append(" >\"").append(log.string()).append("\" 2>&1");
#else
  cmd.append(" >\"").append(log.string()).append("\" 2>&1");
#endif
  INFO("command: " << cmd);
  const int exit_code = std::system(cmd.c_str());
  INFO("polyc output: " << SlurpFile(log));
  REQUIRE(exit_code == 0);
  REQUIRE(fs::exists(object));
  REQUIRE(fs::exists(out));
  REQUIRE(fs::exists(call_graph));

  const std::string doc = SlurpFile(out);
  REQUIRE_FALSE(doc.empty());
  REQUIRE(doc.find("\"schema\":\"polyglot.profilesymbols.v1\"") !=
          std::string::npos);
  REQUIRE(doc.find("\"symbols\"") != std::string::npos);
  REQUIRE(doc.find("\"language\":\"poly\"") != std::string::npos);
  REQUIRE(doc.find("\"language\":\"ploy\"") == std::string::npos);

  const std::string call_graph_doc = SlurpFile(call_graph);
  REQUIRE(call_graph_doc.find("\"schema\":\"polyglot.callgraph.v1\"") !=
          std::string::npos);
  REQUIRE(call_graph_doc.find("\"language\":\"poly\"") != std::string::npos);
  REQUIRE(call_graph_doc.find("\"language\":\"ploy\"") == std::string::npos);
  REQUIRE(call_graph_doc.find("__ploy_rt_call_enter") != std::string::npos);
  REQUIRE(call_graph_doc.find("__ploy_rt_call_exit") != std::string::npos);

  fs::remove(out);
}
