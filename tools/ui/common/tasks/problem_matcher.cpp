/**
 * @file     problem_matcher.cpp
 * @brief    Implementation of `problem_matcher.h`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/tasks/problem_matcher.h"

#include <regex>

namespace polyglot::tools::ui::tasks {

namespace {

DiagnosticSeverity SeverityFromString(const std::string &s) {
  if (s == "warning" || s == "warn") return DiagnosticSeverity::kWarning;
  if (s == "info" || s == "note") return DiagnosticSeverity::kInfo;
  return DiagnosticSeverity::kError;
}

// path:line:col: severity: message  (gcc/clang/rustc share the layout)
const std::regex &GccRegex() {
  static const std::regex r(
      R"(^([^:\n]+):(\d+):(\d+):\s+(error|warning|note|info)\s*:\s*(.*)$)");
  return r;
}

// path(line,col): severity CODE: message  (msbuild / .NET)
const std::regex &MsbuildRegex() {
  static const std::regex r(
      R"(^(.+)\((\d+),(\d+)\):\s+(error|warning)\s+([A-Z0-9]+):\s+(.*)$)");
  return r;
}

// path(line,col): error TSxxxx: message  (tsc)
const std::regex &TscRegex() {
  static const std::regex r(
      R"(^(.+)\((\d+),(\d+)\):\s+(error|warning)\s+(TS\d+):\s+(.*)$)");
  return r;
}

}  // namespace

ProblemMatcher::ProblemMatcher(std::string name) : name_(std::move(name)) {}

std::optional<Diagnostic> ProblemMatcher::Match(
    const std::string &line) const {
  std::smatch m;
  if (name_ == "$msbuild") {
    if (std::regex_match(line, m, MsbuildRegex())) {
      Diagnostic d;
      d.file = m[1];
      d.line = std::stoi(m[2]);
      d.column = std::stoi(m[3]);
      d.severity = SeverityFromString(m[4]);
      d.code = m[5];
      d.message = m[6];
      return d;
    }
    return std::nullopt;
  }
  if (name_ == "$tsc") {
    if (std::regex_match(line, m, TscRegex())) {
      Diagnostic d;
      d.file = m[1];
      d.line = std::stoi(m[2]);
      d.column = std::stoi(m[3]);
      d.severity = SeverityFromString(m[4]);
      d.code = m[5];
      d.message = m[6];
      return d;
    }
    return std::nullopt;
  }
  // $gcc / $clang / $rustc share the layout.
  if (std::regex_match(line, m, GccRegex())) {
    Diagnostic d;
    d.file = m[1];
    d.line = std::stoi(m[2]);
    d.column = std::stoi(m[3]);
    d.severity = SeverityFromString(m[4]);
    d.message = m[5];
    return d;
  }
  return std::nullopt;
}

WatchSignal ProblemMatcher::DetectWatch(const std::string &line) const {
  // Conventional watch markers used by background tasks.
  if (line.find("Watching for file changes") != std::string::npos ||
      line.find("Starting compilation in watch mode") != std::string::npos ||
      line.find("entering directory") != std::string::npos)
    return WatchSignal::kBegin;
  if (line.find("Found 0 errors") != std::string::npos ||
      line.find("Compilation complete") != std::string::npos ||
      line.find("Build succeeded") != std::string::npos ||
      line.find("leaving directory") != std::string::npos)
    return WatchSignal::kEnd;
  return WatchSignal::kNone;
}

}  // namespace polyglot::tools::ui::tasks
