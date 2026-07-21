/**
 * @file     inline_test_lens.cpp
 * @brief    Implementation of `inline_test_lens.h`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/testing/inline_test_lens.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace polyglot::tools::ui::testing {
namespace {

void ScanLines(const std::string &content, const std::regex &re,
               int symbol_group, const std::string &framework,
               std::vector<Lens> &out) {
  std::stringstream ss(content);
  std::string line;
  int line_no = 0;
  while (std::getline(ss, line)) {
    ++line_no;
    std::smatch m;
    if (!std::regex_search(line, m, re)) continue;
    Lens l;
    l.line = line_no;
    l.framework = framework;
    if (symbol_group > 0 && symbol_group < static_cast<int>(m.size()))
      l.symbol = m[symbol_group];
    l.actions = {LensAction::kRun, LensAction::kDebug};
    out.push_back(std::move(l));
  }
}

std::vector<Lens> DetectCpp(const std::string &content) {
  // Catch2 v3: TEST_CASE("name", "[tag]")  and  TEST("name").
  static const std::regex kCatch2(
      R"rx(\b(?:TEST_CASE|TEST|SCENARIO)\s*\(\s*"([^"]+)")rx");
  std::vector<Lens> out;
  ScanLines(content, kCatch2, 1, "catch2", out);
  return out;
}

std::vector<Lens> DetectPy(const std::string &content) {
  static const std::regex kPytest(
      R"(^\s*def\s+(test_[A-Za-z_0-9]*)\s*\()");
  std::vector<Lens> out;
  ScanLines(content, kPytest, 1, "pytest", out);
  return out;
}

std::vector<Lens> DetectRust(const std::string &content) {
  // The `#[test]` attribute precedes the `fn` declaration.  We
  // emit the lens on the line carrying `fn`.
  std::stringstream ss(content);
  std::string line;
  int line_no = 0;
  bool armed = false;
  static const std::regex kFn(R"(^\s*(?:pub\s+)?fn\s+([A-Za-z_][A-Za-z_0-9]*))");
  std::vector<Lens> out;
  while (std::getline(ss, line)) {
    ++line_no;
    if (line.find("#[test]") != std::string::npos) {
      armed = true;
      continue;
    }
    if (!armed) continue;
    std::smatch m;
    if (std::regex_search(line, m, kFn)) {
      Lens l;
      l.line = line_no;
      l.framework = "cargo";
      l.symbol = m[1];
      l.actions = {LensAction::kRun, LensAction::kDebug};
      out.push_back(std::move(l));
      armed = false;
    }
  }
  return out;
}

std::vector<Lens> DetectJava(const std::string &content) {
  // JUnit 5 `@Test` annotation precedes the method declaration.
  std::stringstream ss(content);
  std::string line;
  int line_no = 0;
  bool armed = false;
  static const std::regex kMethod(
      R"(^\s*(?:public|protected|private)?\s*(?:static\s+)?\w[\w<>\[\]]*\s+([A-Za-z_][A-Za-z_0-9]*)\s*\()");
  std::vector<Lens> out;
  while (std::getline(ss, line)) {
    ++line_no;
    if (line.find("@Test") != std::string::npos) {
      armed = true;
      continue;
    }
    if (!armed) continue;
    std::smatch m;
    if (std::regex_search(line, m, kMethod)) {
      Lens l;
      l.line = line_no;
      l.framework = "junit";
      l.symbol = m[1];
      l.actions = {LensAction::kRun, LensAction::kDebug};
      out.push_back(std::move(l));
      armed = false;
    }
  }
  return out;
}

std::vector<Lens> DetectCSharp(const std::string &content) {
  std::stringstream ss(content);
  std::string line;
  int line_no = 0;
  std::string framework;
  static const std::regex kMethod(
      R"(^\s*(?:public|internal|private)?\s*(?:static\s+|async\s+)*[\w<>\[\]]+\s+([A-Za-z_][A-Za-z_0-9]*)\s*\()");
  std::vector<Lens> out;
  while (std::getline(ss, line)) {
    ++line_no;
    if (line.find("[Fact]") != std::string::npos ||
        line.find("[Theory]") != std::string::npos) {
      framework = "xunit";
      continue;
    }
    if (line.find("[Test]") != std::string::npos ||
        line.find("[TestCase") != std::string::npos) {
      framework = "nunit";
      continue;
    }
    if (framework.empty()) continue;
    std::smatch m;
    if (std::regex_search(line, m, kMethod)) {
      Lens l;
      l.line = line_no;
      l.framework = framework;
      l.symbol = m[1];
      l.actions = {LensAction::kRun, LensAction::kDebug};
      out.push_back(std::move(l));
      framework.clear();
    }
  }
  return out;
}

}  // namespace

InlineTestLens::InlineTestLens() {
  detectors_["cpp"] = DetectCpp;
  detectors_["cc"]  = DetectCpp;
  detectors_["cxx"] = DetectCpp;
  detectors_["h"]   = DetectCpp;
  detectors_["hpp"] = DetectCpp;
  detectors_["py"]  = DetectPy;
  detectors_["rs"]  = DetectRust;
  detectors_["java"] = DetectJava;
  detectors_["cs"]  = DetectCSharp;
}

void InlineTestLens::RegisterDetector(std::string extension,
                                      LensDetector detector) {
  detectors_[std::move(extension)] = std::move(detector);
}

std::string InlineTestLens::Extension(const std::string &path) {
  auto dot = path.find_last_of('.');
  if (dot == std::string::npos) return {};
  std::string ext = path.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return ext;
}

std::vector<Lens> InlineTestLens::ComputeForFile(
    const std::string &file_path, const std::string &content) const {
  auto it = detectors_.find(Extension(file_path));
  std::vector<Lens> result;
  if (it != detectors_.end()) result = it->second(content);
  cache_[file_path] = result;
  return result;
}

bool InlineTestLens::RecordFailure(const std::string &file_path, int line,
                                   const std::string &message) {
  auto it = cache_.find(file_path);
  if (it == cache_.end()) return false;
  for (auto &l : it->second) {
    if (l.line == line) {
      l.failure_message = message;
      return true;
    }
  }
  return false;
}

std::vector<Lens> InlineTestLens::Cached(const std::string &file_path) const {
  auto it = cache_.find(file_path);
  return it == cache_.end() ? std::vector<Lens>{} : it->second;
}

}  // namespace polyglot::tools::ui::testing
