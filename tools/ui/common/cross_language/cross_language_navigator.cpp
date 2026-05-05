/**
 * @file     cross_language_navigator.cpp
 * @brief    Implementation of `cross_language_navigator.h`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/cross_language/cross_language_navigator.h"

#include <algorithm>
#include <unordered_set>

namespace polyglot::tools::ui::cross_language {

std::string HostLanguageName(HostLanguage l) {
  switch (l) {
    case HostLanguage::kCpp:    return "cpp";
    case HostLanguage::kRust:   return "rust";
    case HostLanguage::kPython: return "python";
    case HostLanguage::kJava:   return "java";
    case HostLanguage::kDotnet: return "dotnet";
  }
  return "unknown";
}

std::optional<HostLanguage> HostLanguageFromName(const std::string &name) {
  if (name == "cpp" || name == "c++") return HostLanguage::kCpp;
  if (name == "rust")                 return HostLanguage::kRust;
  if (name == "python")               return HostLanguage::kPython;
  if (name == "java")                 return HostLanguage::kJava;
  if (name == "dotnet" || name == "csharp" || name == "cs")
    return HostLanguage::kDotnet;
  return std::nullopt;
}

void LinkRegistry::AddSite(LinkSite site) {
  sites_.push_back(std::move(site));
}

void LinkRegistry::AddDefinition(Definition def) {
  defs_.push_back(std::move(def));
}

std::optional<Definition> LinkRegistry::GotoDefinition(
    const LinkSite &site) const {
  for (const auto &d : defs_) {
    if (d.language == site.target_language && d.symbol == site.target_symbol)
      return d;
  }
  return std::nullopt;
}

std::vector<LinkSite> LinkRegistry::FindLinkReferences(
    const Definition &def) const {
  std::vector<LinkSite> out;
  for (const auto &s : sites_) {
    if (s.target_language == def.language && s.target_symbol == def.symbol)
      out.push_back(s);
  }
  return out;
}

std::vector<LinkRegistry::CodeLens> LinkRegistry::CodeLensFor(
    const std::string &host_file) const {
  std::vector<CodeLens> out;
  for (const auto &d : defs_) {
    if (d.location.file != host_file) continue;
    CodeLens lens;
    lens.anchor = d.location;
    lens.symbol = d.symbol;
    lens.reference_count = static_cast<int>(FindLinkReferences(d).size());
    out.push_back(std::move(lens));
  }
  return out;
}

std::vector<WorkspaceEdit> RenamePlanner::Plan(
    HostLanguage language,
    const std::string &symbol,
    const std::string &new_name,
    const std::vector<Reference> &extra_references) const {
  std::vector<WorkspaceEdit> plan;

  // 1. Update host-language definitions captured in the registry.
  for (const auto &d : registry_.definitions()) {
    if (d.language != language || d.symbol != symbol) continue;
    WorkspaceEdit e;
    e.file = d.location.file;
    e.line = d.location.line;
    e.column = d.location.column;
    e.length = static_cast<int>(symbol.size());
    e.new_text = new_name;
    plan.push_back(std::move(e));
  }

  // 2. Update every `.ploy` LINK site that targets the symbol.
  for (const auto &s : registry_.sites()) {
    if (s.target_language != language || s.target_symbol != symbol) continue;
    WorkspaceEdit e;
    e.file = s.location.file;
    e.line = s.location.line;
    e.column = s.location.column;
    e.length = static_cast<int>(symbol.size());
    e.new_text = new_name;
    plan.push_back(std::move(e));
  }

  // 3. Update host-language references the LSP discovered.
  for (const auto &r : extra_references) {
    if (r.symbol != symbol) continue;
    WorkspaceEdit e;
    e.file = r.location.file;
    e.line = r.location.line;
    e.column = r.location.column;
    e.length = static_cast<int>(symbol.size());
    e.new_text = new_name;
    plan.push_back(std::move(e));
  }

  return plan;
}

}  // namespace polyglot::tools::ui::cross_language
