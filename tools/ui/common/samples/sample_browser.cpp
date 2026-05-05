/**
 * @file     sample_browser.cpp
 * @brief    Implementation of `sample_browser.h`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/samples/sample_browser.h"

#include <algorithm>

#include <nlohmann/json.hpp>

namespace polyglot::tools::ui::samples {
namespace {

using Json = nlohmann::json;

bool ContainsCi(const std::string &haystack, const std::string &needle) {
  if (needle.empty()) return true;
  auto lower = [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  };
  std::string h, n;
  h.reserve(haystack.size());
  n.reserve(needle.size());
  for (char c : haystack) h.push_back(lower(static_cast<unsigned char>(c)));
  for (char c : needle)   n.push_back(lower(static_cast<unsigned char>(c)));
  return h.find(n) != std::string::npos;
}

bool ContainsAll(const std::vector<std::string> &set,
                 const std::vector<std::string> &required) {
  for (const auto &r : required) {
    bool found = false;
    for (const auto &s : set) {
      if (s == r) { found = true; break; }
    }
    if (!found) return false;
  }
  return true;
}

std::string JoinPath(const std::string &a, const std::string &b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  bool a_slash = a.back() == '/';
  bool b_slash = b.front() == '/';
  if (a_slash && b_slash)   return a + b.substr(1);
  if (!a_slash && !b_slash) return a + "/" + b;
  return a + b;
}

}  // namespace

std::string EntryKindName(EntryKind k) {
  return k == EntryKind::kSample ? "sample" : "tutorial";
}

std::optional<EntryKind> EntryKindFromName(const std::string &name) {
  if (name == "sample")   return EntryKind::kSample;
  if (name == "tutorial") return EntryKind::kTutorial;
  return std::nullopt;
}

std::string DifficultyName(Difficulty d) {
  switch (d) {
    case Difficulty::kBeginner:     return "beginner";
    case Difficulty::kIntermediate: return "intermediate";
    case Difficulty::kAdvanced:     return "advanced";
  }
  return "unknown";
}

std::optional<Difficulty> DifficultyFromName(const std::string &name) {
  if (name == "beginner")     return Difficulty::kBeginner;
  if (name == "intermediate") return Difficulty::kIntermediate;
  if (name == "advanced")     return Difficulty::kAdvanced;
  return std::nullopt;
}

void SampleCatalogue::AddEntry(CatalogueEntry entry) {
  for (auto &existing : entries_) {
    if (existing.id == entry.id) {
      existing = std::move(entry);
      return;
    }
  }
  entries_.push_back(std::move(entry));
}

bool SampleCatalogue::LoadIndex(const std::string &json) {
  auto j = Json::parse(json, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return false;
  if (!j.contains("entries") || !j["entries"].is_array()) return false;
  for (const auto &e : j["entries"]) {
    CatalogueEntry entry;
    entry.id = e.value("id", std::string{});
    entry.title = e.value("title", std::string{});
    entry.summary = e.value("summary", std::string{});
    entry.root_path = e.value("root_path", std::string{});
    auto kind = EntryKindFromName(e.value("kind", std::string{"sample"}));
    if (kind) entry.kind = *kind;
    auto diff = DifficultyFromName(
        e.value("difficulty", std::string{"beginner"}));
    if (diff) entry.difficulty = *diff;
    if (e.contains("languages") && e["languages"].is_array())
      for (const auto &l : e["languages"])
        entry.languages.push_back(l.get<std::string>());
    if (e.contains("topics") && e["topics"].is_array())
      for (const auto &t : e["topics"])
        entry.topics.push_back(t.get<std::string>());
    if (e.contains("files") && e["files"].is_array())
      for (const auto &f : e["files"])
        entry.files.push_back(f.get<std::string>());
    AddEntry(std::move(entry));
  }
  return true;
}

const CatalogueEntry *SampleCatalogue::Find(const std::string &id) const {
  for (const auto &e : entries_)
    if (e.id == id) return &e;
  return nullptr;
}

std::vector<const CatalogueEntry *> SampleCatalogue::Filter(
    const CatalogueQuery &q) const {
  std::vector<const CatalogueEntry *> out;
  for (const auto &e : entries_) {
    if (q.kind && e.kind != *q.kind) continue;
    if (q.difficulty && e.difficulty != *q.difficulty) continue;
    if (!ContainsAll(e.languages, q.languages)) continue;
    if (!ContainsAll(e.topics, q.topics)) continue;
    if (!q.text.empty() &&
        !ContainsCi(e.title, q.text) &&
        !ContainsCi(e.summary, q.text) &&
        !ContainsCi(e.id, q.text))
      continue;
    out.push_back(&e);
  }
  return out;
}

std::optional<CopyPlan> SampleCatalogue::PlanCopy(
    const std::string &entry_id,
    const std::string &destination_root) const {
  const auto *entry = Find(entry_id);
  if (!entry) return std::nullopt;
  CopyPlan plan;
  plan.entry_id = entry_id;
  plan.destination_root = destination_root;
  for (const auto &f : entry->files) {
    plan.files.emplace_back(JoinPath(entry->root_path, f),
                            JoinPath(destination_root, f));
  }
  return plan;
}

}  // namespace polyglot::tools::ui::samples
