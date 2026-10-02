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
#include <cctype>
#include <filesystem>
#include <regex>
#include <sstream>
#include <unordered_set>

namespace polyglot::tools::ui::cross_language {

std::string HostLanguageName(HostLanguage l) {
  switch (l) {
    case HostLanguage::kCpp:    return "cpp";
    case HostLanguage::kRust:   return "rust";
    case HostLanguage::kPython: return "python";
    case HostLanguage::kJava:   return "java";
    case HostLanguage::kDotnet: return "dotnet";
    case HostLanguage::kGo: return "go";
    case HostLanguage::kJavaScript: return "javascript";
    case HostLanguage::kRuby: return "ruby";
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
  if (name == "go") return HostLanguage::kGo;
  if (name == "javascript" || name == "js") return HostLanguage::kJavaScript;
  if (name == "ruby") return HostLanguage::kRuby;
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

  // 2. Update every `.poly` LINK site that targets the symbol.
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



namespace {
std::string Trim(std::string text) {
  auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

// Preserve offsets/newlines while removing comments and string literals. This
// prevents navigation from treating sample calls in docs as executable calls.
std::string MaskNonCode(const std::string &source, bool hash_comments = false) {
  std::string result = source;
  for (std::size_t i = 0; i < source.size();) {
    auto blank = [&](std::size_t from, std::size_t to) {
      for (auto j = from; j < to; ++j)
        if (result[j] != '\n' && result[j] != '\r') result[j] = ' ';
    };
    if (source.compare(i, 2, "//") == 0 || (hash_comments && source[i] == '#')) {
      const auto end = source.find('\n', i);
      const auto stop = end == std::string::npos ? source.size() : end;
      blank(i, stop); i = stop;
    } else if (source.compare(i, 2, "/*") == 0) {
      const auto end = source.find("*/", i + 2);
      const auto stop = end == std::string::npos ? source.size() : end + 2;
      blank(i, stop); i = stop;
    } else if (source[i] == '"' || source[i] == '\'') {
      const char quote = source[i];
      const std::string triple(3, quote);
      const bool is_triple = source.compare(i, 3, triple) == 0;
      const auto start = i;
      i += is_triple ? 3 : 1;
      while (i < source.size()) {
        if (source[i] == '\\') { i = std::min(i + 2, source.size()); continue; }
        if (is_triple && source.compare(i, 3, triple) == 0) { i += 3; break; }
        if (!is_triple && source[i++] == quote) break;
        if (is_triple) ++i;
      }
      blank(start, i);
    } else ++i;
  }
  return result;
}

std::string CanonicalHost(const std::string &language) {
  auto host = HostLanguageFromName(language);
  if (host) return HostLanguageName(*host);
  return language == "ploy" ? "poly" : language;
}

std::vector<std::string> Lines(const std::string &text) {
  std::vector<std::string> lines;
  std::istringstream stream(text);
  for (std::string line; std::getline(stream, line);) lines.push_back(line);
  return lines;
}
}

std::optional<ForeignCallTarget> ForeignTargetAt(
    const std::string &poly_source, std::size_t offset) {
  const auto masked = MaskNonCode(poly_source);
  static const std::regex call(R"(\bCALL\s*\(\s*([A-Za-z][A-Za-z0-9_]*)\s*,\s*([A-Za-z_][A-Za-z0-9_]*(?:(?:::|\.)[A-Za-z_][A-Za-z0-9_]*)*))");
  static const std::regex link(R"(\bLINK\s+([A-Za-z][A-Za-z0-9_]*)\s*::\s*([A-Za-z_][A-Za-z0-9_]*(?:(?:::|\.)[A-Za-z_][A-Za-z0-9_]*)*))");
  for (const auto *pattern : {&call, &link}) {
    for (std::sregex_iterator it(masked.begin(), masked.end(), *pattern), end; it != end; ++it) {
      const auto &m = *it;
      const auto first = static_cast<std::size_t>(m.position());
      const auto last = first + m.length();
      if (offset < first || offset > last) continue;
      const auto language = HostLanguageFromName(m[1].str());
      if (language) return ForeignCallTarget{HostLanguageName(*language), m[2].str()};
    }
  }
  return std::nullopt;
}

std::vector<std::string> ForeignSourceCandidates(
    const std::string &poly_file, const ForeignCallTarget &target) {
  namespace fs = std::filesystem;
  std::string module = target.qualified_symbol;
  const auto separator = module.rfind("::");
  const auto dot = module.rfind('.');
  auto last = separator;
  if (last == std::string::npos || (dot != std::string::npos && dot > last)) last = dot;
  if (last == std::string::npos) return {};
  module.resize(last);
  std::string path;
  for (std::size_t i = 0; i < module.size(); ++i) {
    if (module[i] == ':' && i + 1 < module.size() && module[i + 1] == ':') {
      path += '/'; ++i;
    } else path += module[i] == '.' ? '/' : module[i];
  }
  std::vector<std::string> extensions;
  const auto lang = CanonicalHost(target.language);
  if (lang == "cpp") extensions = {".cpp", ".cc", ".c", ".hpp", ".h"};
  else if (lang == "python") extensions = {".py"};
  else if (lang == "rust") extensions = {".rs"};
  else if (lang == "java") extensions = {".java"};
  else if (lang == "dotnet") extensions = {".cs"};
  else if (lang == "go") extensions = {".go"};
  else if (lang == "javascript") extensions = {".js", ".mjs"};
  else if (lang == "ruby") extensions = {".rb"};
  std::vector<std::string> result;
  const auto root = fs::path(poly_file).parent_path();
  // For Java/.NET module::Class::method, also try the containing module.
  for (;;) {
    for (const auto &ext : extensions) {
      result.push_back((root / (path + ext)).lexically_normal().string());
      result.push_back((root / lang / (path + ext)).lexically_normal().string());
      if (lang == "dotnet") result.push_back((root / "csharp" / (path + ext)).lexically_normal().string());
    }
    const auto slash = path.rfind('/');
    if (slash == std::string::npos) break;
    path.resize(slash);
  }
  return result;
}

std::vector<FunctionDocumentation> ExtractFunctionDocumentation(
    const std::string &source, const std::string &language,
    const std::string &filename) {
  const auto lang = CanonicalHost(language);
  const auto lines = Lines(source);
  const auto masked = MaskNonCode(source, lang == "python" || lang == "ruby");
  const auto code_lines = Lines(masked);
  std::regex pattern;
  if (lang == "poly") pattern = std::regex(R"(\b(?:FUNC|PIPELINE)\s+(\w+)\s*\()");
  else if (lang == "python") pattern = std::regex(R"(^\s*(?:async\s+)?def\s+(\w+)\s*\()");
  else if (lang == "rust") pattern = std::regex(R"(\bfn\s+(\w+)\s*\()");
  else if (lang == "go") pattern = std::regex(R"(^\s*func\s+(?:\([^)]*\)\s*)?(\w+)\s*\()");
  else if (lang == "javascript") pattern = std::regex(R"(\bfunction\s+(\w+)\s*\()");
  else if (lang == "ruby") pattern = std::regex(R"(^\s*def\s+(?:self\.)?(\w+))");
  else if (lang == "cpp" || lang == "java" || lang == "dotnet")
    pattern = std::regex(R"((?:^|[;{}])\s*(?:(?:public|private|protected|internal|static|inline|extern|virtual|constexpr|async|final|synchronized|override)\s+)*(?:[A-Za-z_][\w:<>,\[\]*&?]*\s+)+([A-Za-z_]\w*)\s*\()");
  else return {};

  std::vector<FunctionDocumentation> result;
  for (std::size_t line = 0; line < code_lines.size(); ++line) {
    const auto &code = code_lines[line];
    for (std::sregex_iterator it(code.begin(), code.end(), pattern), end; it != end; ++it) {
      const auto &m = *it;
      const auto name = m[1].str();
      const auto declaration_prefix = Trim(m.str().substr(0, m.position(1) - m.position()));
      if (declaration_prefix.rfind("return ", 0) == 0 ||
          declaration_prefix.rfind("throw ", 0) == 0 ||
          declaration_prefix.rfind("co_return ", 0) == 0) continue;
      if (name == "if" || name == "while" || name == "for" || name == "switch" || name == "catch") continue;
      FunctionDocumentation doc;
      doc.name = name; doc.language = lang;
      doc.location = {filename, static_cast<int>(line + 1), static_cast<int>(m.position(1) + 1)};
      std::size_t signature_end = line;
      doc.signature = Trim(lines[line]);
      // Collect multiline signatures until a body or Python's colon.
      while (signature_end + 1 < lines.size() && signature_end < line + 12 &&
             doc.signature.find(')') == std::string::npos && lang != "ruby") {
        doc.signature += " " + Trim(lines[++signature_end]);
      }
      if (auto brace = doc.signature.find('{'); brace != std::string::npos)
        doc.signature.resize(brace);
      doc.signature = Trim(doc.signature);
      // Ignore C-like prototypes; show the actual implementation when present.
      if ((lang == "cpp" || lang == "java" || lang == "dotnet") &&
          !doc.signature.empty() && doc.signature.back() == ';') continue;
      std::vector<std::string> comments;
      bool in_block = false;
      for (std::size_t prior = line; prior > 0;) {
        auto text = Trim(lines[--prior]);
        if (text.empty()) break;
        if (text.find("*/") != std::string::npos) in_block = true;
        const bool comment = in_block || text.rfind("//", 0) == 0 ||
                             text.rfind('#', 0) == 0 || text.rfind("/*", 0) == 0;
        if (!comment) break;
        const bool block_start = text.find("/*") != std::string::npos;
        while (!text.empty() && (text[0] == '/' || text[0] == '*' || text[0] == '#')) text.erase(0, 1);
        if (auto close = text.find("*/"); close != std::string::npos) text.resize(close);
        text = Trim(text);
        if (!text.empty()) comments.push_back(text);
        if (block_start) { in_block = false; break; }
      }
      std::reverse(comments.begin(), comments.end());
      for (const auto &comment : comments) {
        if (!doc.documentation.empty()) doc.documentation += '\n';
        doc.documentation += comment;
      }
      if (lang == "python" && signature_end + 1 < lines.size()) {
        auto first = Trim(lines[signature_end + 1]);
        const auto delimiter = first.rfind("\"\"\"", 0) == 0 ? "\"\"\"" :
                               first.rfind("'''", 0) == 0 ? "'''" : "";
        if (*delimiter) {
          std::string text = first.substr(3);
          for (auto next = signature_end + 2; text.find(delimiter) == std::string::npos && next < lines.size(); ++next)
            text += "\n" + Trim(lines[next]);
          if (auto close = text.find(delimiter); close != std::string::npos) text.resize(close);
          doc.documentation = Trim(text);
        }
      }
      // Bounded preview; navigation always opens the complete source file.
      for (std::size_t preview = line; preview < lines.size() && preview < line + 18; ++preview) {
        if (preview != line) doc.source_preview += '\n';
        doc.source_preview += lines[preview];
      }
      result.push_back(std::move(doc));
    }
  }
  return result;
}

}  // namespace polyglot::tools::ui::cross_language
