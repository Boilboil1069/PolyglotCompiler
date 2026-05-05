/**
 * @file     coverage_model.cpp
 * @brief    Implementation of `coverage_model.h`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/testing/coverage_model.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

#include <nlohmann/json.hpp>

namespace polyglot::tools::ui::testing {
namespace {

using Json = nlohmann::json;

std::string Trim(std::string s) {
  auto not_ws = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_ws));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_ws).base(), s.end());
  return s;
}

std::string Attr(const std::string &tag_body, const std::string &name) {
  auto p = tag_body.find(name + "=");
  if (p == std::string::npos) return {};
  auto q = p + name.size() + 1;
  if (q >= tag_body.size()) return {};
  char quote = tag_body[q];
  if (quote != '"' && quote != '\'') return {};
  auto end = tag_body.find(quote, q + 1);
  if (end == std::string::npos) return {};
  return tag_body.substr(q + 1, end - q - 1);
}

}  // namespace

std::size_t FileCoverage::covered_lines() const {
  std::size_t n = 0;
  for (const auto &kv : line_hits) if (kv.second > 0) ++n;
  return n;
}

std::size_t FileCoverage::total_lines() const { return line_hits.size(); }

double FileCoverage::percent() const {
  if (line_hits.empty()) return 0.0;
  return 100.0 * static_cast<double>(covered_lines()) /
         static_cast<double>(line_hits.size());
}

CoverageFormat DetectCoverageFormat(const std::string &text) {
  std::string head = Trim(text.substr(0, 2048));
  if (head.rfind("TN:", 0) == 0 || head.rfind("SF:", 0) == 0)
    return CoverageFormat::kLcov;
  if (head.rfind("<?xml", 0) == 0 || head.rfind("<coverage", 0) == 0)
    return CoverageFormat::kCobertura;
  if (head.rfind("{", 0) == 0) {
    // coverlet documents have "Modules", tarpaulin has "files".
    if (head.find("Modules") != std::string::npos)
      return CoverageFormat::kCoverlet;
    return CoverageFormat::kTarpaulin;
  }
  return CoverageFormat::kLcov;
}

bool CoverageModel::Load(const std::string &text) {
  return Load(text, DetectCoverageFormat(text));
}

bool CoverageModel::Load(const std::string &text, CoverageFormat format) {
  switch (format) {
    case CoverageFormat::kLcov:        IngestLcov(text); return true;
    case CoverageFormat::kCobertura:
    case CoverageFormat::kCoveragePy:  IngestCobertura(text); return true;
    case CoverageFormat::kTarpaulin:   IngestTarpaulin(text); return true;
    case CoverageFormat::kCoverlet:    IngestCoverlet(text); return true;
  }
  return false;
}

void CoverageModel::IngestLcov(const std::string &text) {
  std::stringstream ss(text);
  std::string line;
  std::string current;
  while (std::getline(ss, line)) {
    line = Trim(std::move(line));
    if (line.rfind("SF:", 0) == 0) {
      current = line.substr(3);
      files_[current].file = current;
    } else if (line.rfind("DA:", 0) == 0 && !current.empty()) {
      auto comma = line.find(',', 3);
      if (comma == std::string::npos) continue;
      try {
        int ln = std::stoi(line.substr(3, comma - 3));
        int hits = std::stoi(line.substr(comma + 1));
        files_[current].line_hits[ln] = hits;
      } catch (...) {}
    } else if (line == "end_of_record") {
      current.clear();
    }
  }
}

void CoverageModel::IngestCobertura(const std::string &text) {
  // Tag-level scan: every <class filename="..."> opens a file
  // context until </class>; <line number="N" hits="K"/> records
  // attach to that file.
  std::string current;
  std::size_t pos = 0;
  while ((pos = text.find('<', pos)) != std::string::npos) {
    auto end = text.find('>', pos);
    if (end == std::string::npos) break;
    std::string body = text.substr(pos + 1, end - pos - 1);
    pos = end + 1;
    if (body.rfind("/class", 0) == 0) {
      current.clear();
      continue;
    }
    if (body.rfind("class", 0) == 0 && body.size() > 5 &&
        std::isspace(static_cast<unsigned char>(body[5]))) {
      std::string filename = Attr(body, "filename");
      if (!filename.empty()) {
        current = filename;
        files_[current].file = current;
      }
      continue;
    }
    if (body.rfind("line", 0) == 0 && body.size() > 4 &&
        std::isspace(static_cast<unsigned char>(body[4])) &&
        !current.empty()) {
      std::string number = Attr(body, "number");
      std::string hits = Attr(body, "hits");
      if (number.empty() || hits.empty()) continue;
      try {
        files_[current].line_hits[std::stoi(number)] = std::stoi(hits);
      } catch (...) {}
    }
  }
}

void CoverageModel::IngestTarpaulin(const std::string &text) {
  auto j = Json::parse(text, nullptr, false);
  if (j.is_discarded()) return;
  // tarpaulin: { "files": [ { "path": [...], "traces": [{"line": N, "stats": {"Line": K}}] } ] }
  if (!j.is_object() || !j.contains("files") || !j["files"].is_array()) return;
  for (const auto &f : j["files"]) {
    std::string path;
    if (f.contains("path") && f["path"].is_array()) {
      for (const auto &part : f["path"]) {
        if (path.empty()) path = part.get<std::string>();
        else              path += "/" + part.get<std::string>();
      }
    } else if (f.contains("path") && f["path"].is_string()) {
      path = f["path"].get<std::string>();
    }
    if (path.empty()) continue;
    auto &fc = files_[path];
    fc.file = path;
    if (!f.contains("traces")) continue;
    for (const auto &trace : f["traces"]) {
      if (!trace.contains("line")) continue;
      int line = trace["line"].get<int>();
      int hits = 0;
      if (trace.contains("stats") && trace["stats"].is_object() &&
          trace["stats"].contains("Line"))
        hits = trace["stats"]["Line"].get<int>();
      fc.line_hits[line] = hits;
    }
  }
}

void CoverageModel::IngestCoverlet(const std::string &text) {
  auto j = Json::parse(text, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return;
  // coverlet:
  //  { "Modules": { "Module": { "ClassFiles": { "<file>": { "Lines": { "<n>": K } } } } } }
  // The exact shape uses "Files" rather than "ClassFiles" in
  // newer coverlet versions; handle both.
  if (!j.contains("Modules")) return;
  for (const auto &mod : j["Modules"].items()) {
    const auto &mod_val = mod.value();
    if (!mod_val.is_object()) continue;
    for (const auto &cls : mod_val.items()) {
      const auto &cls_val = cls.value();
      if (!cls_val.is_object()) continue;
      const Json *files_node = nullptr;
      if (cls_val.contains("Files")) files_node = &cls_val["Files"];
      else if (cls_val.contains("ClassFiles")) files_node = &cls_val["ClassFiles"];
      if (!files_node || !files_node->is_object()) continue;
      for (const auto &file : files_node->items()) {
        const auto &file_val = file.value();
        if (!file_val.is_object() || !file_val.contains("Lines")) continue;
        auto &fc = files_[file.key()];
        fc.file = file.key();
        for (const auto &line : file_val["Lines"].items()) {
          try {
            int ln = std::stoi(line.key());
            int hits = line.value().get<int>();
            fc.line_hits[ln] = hits;
          } catch (...) {}
        }
      }
    }
  }
}

std::vector<const FileCoverage *> CoverageModel::Files() const {
  std::vector<const FileCoverage *> out;
  out.reserve(files_.size());
  for (const auto &kv : files_) out.push_back(&kv.second);
  std::sort(out.begin(), out.end(),
            [](const FileCoverage *a, const FileCoverage *b) {
              return a->file < b->file;
            });
  return out;
}

const FileCoverage *CoverageModel::Find(const std::string &file) const {
  auto it = files_.find(file);
  return it == files_.end() ? nullptr : &it->second;
}

double CoverageModel::OverallPercent() const {
  std::size_t total = 0, covered = 0;
  for (const auto &kv : files_) {
    total   += kv.second.total_lines();
    covered += kv.second.covered_lines();
  }
  if (total == 0) return 0.0;
  return 100.0 * static_cast<double>(covered) /
         static_cast<double>(total);
}

std::vector<const FileCoverage *> CoverageModel::BelowThreshold(
    double threshold_percent) const {
  std::vector<const FileCoverage *> out;
  for (const auto &kv : files_) {
    if (kv.second.percent() < threshold_percent)
      out.push_back(&kv.second);
  }
  std::sort(out.begin(), out.end(),
            [](const FileCoverage *a, const FileCoverage *b) {
              return a->file < b->file;
            });
  return out;
}

}  // namespace polyglot::tools::ui::testing
