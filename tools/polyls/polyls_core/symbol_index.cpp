/**
 * @file     symbol_index.cpp
 * @brief    Implementation of @ref polyglot::polyls::SymbolIndex
 *
 * @ingroup  Tool / polyls
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/polyls/polyls_core/symbol_index.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace polyglot::polyls {

using Json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

// ────────────────────────────────────────────────────────────────────────────
// String helpers (regex-free; the index runs on every keystroke).
// ────────────────────────────────────────────────────────────────────────────

bool IsIdentChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool IsIdentStart(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

std::vector<std::string_view> SplitLines(std::string_view text) {
  std::vector<std::string_view> out;
  std::size_t start = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\n') {
      std::size_t end = i;
      if (end > start && text[end - 1] == '\r') --end;
      out.emplace_back(text.data() + start, end - start);
      start = i + 1;
    }
  }
  out.emplace_back(text.data() + start, text.size() - start);
  return out;
}

std::string Trim(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
    s.erase(s.begin());
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
    s.pop_back();
  return s;
}

bool StartsWithKeyword(std::string_view line, std::size_t pos,
                       std::string_view kw) {
  if (line.size() - pos < kw.size()) return false;
  if (line.substr(pos, kw.size()) != kw) return false;
  std::size_t e = pos + kw.size();
  return e == line.size() || std::isspace(static_cast<unsigned char>(line[e])) ||
         line[e] == '(' || line[e] == '{' || line[e] == ':' || line[e] == ';';
}

std::size_t SkipSpaces(std::string_view line, std::size_t i) {
  while (i < line.size() &&
         (line[i] == ' ' || line[i] == '\t'))
    ++i;
  return i;
}

std::string ReadIdent(std::string_view line, std::size_t &i) {
  i = SkipSpaces(line, i);
  std::size_t start = i;
  if (i < line.size() && IsIdentStart(line[i])) {
    ++i;
    while (i < line.size() && IsIdentChar(line[i])) ++i;
  }
  return std::string(line.substr(start, i - start));
}

/// Read a "::"-separated qualified identifier.  Returns the bare last
/// component via @p out_bare and the full text via the return value.
std::string ReadQualified(std::string_view line, std::size_t &i,
                          std::string &out_bare) {
  i = SkipSpaces(line, i);
  std::size_t start = i;
  out_bare.clear();
  while (i < line.size()) {
    if (IsIdentStart(line[i])) {
      std::size_t s = i;
      ++i;
      while (i < line.size() && IsIdentChar(line[i])) ++i;
      out_bare.assign(line.substr(s, i - s));
      if (i + 1 < line.size() && line[i] == ':' && line[i + 1] == ':') {
        i += 2;
        continue;
      }
    }
    break;
  }
  return std::string(line.substr(start, i - start));
}

SymbolLocation MakeLoc(const std::string &uri, std::uint32_t line,
                       std::uint32_t character, std::uint32_t length) {
  SymbolLocation loc;
  loc.uri = uri;
  loc.line = line;
  loc.character = character;
  loc.end_line = line;
  loc.end_character = character + length;
  return loc;
}

const char *KindToString(IndexEntryKind k) {
  switch (k) {
    case IndexEntryKind::kFunction: return "function";
    case IndexEntryKind::kPipeline: return "pipeline";
    case IndexEntryKind::kStruct: return "struct";
    case IndexEntryKind::kVariable: return "variable";
    case IndexEntryKind::kImport: return "import";
    case IndexEntryKind::kLink: return "link";
    case IndexEntryKind::kForeignFunction: return "foreign_function";
    case IndexEntryKind::kForeignClass: return "foreign_class";
  }
  return "unknown";
}

IndexEntryKind KindFromString(const std::string &s) {
  if (s == "function") return IndexEntryKind::kFunction;
  if (s == "pipeline") return IndexEntryKind::kPipeline;
  if (s == "struct") return IndexEntryKind::kStruct;
  if (s == "variable") return IndexEntryKind::kVariable;
  if (s == "import") return IndexEntryKind::kImport;
  if (s == "link") return IndexEntryKind::kLink;
  if (s == "foreign_function") return IndexEntryKind::kForeignFunction;
  if (s == "foreign_class") return IndexEntryKind::kForeignClass;
  return IndexEntryKind::kFunction;
}

Json LocToJson(const SymbolLocation &loc) {
  return Json{{"uri", loc.uri},
              {"line", loc.line},
              {"character", loc.character},
              {"endLine", loc.end_line},
              {"endCharacter", loc.end_character}};
}

SymbolLocation LocFromJson(const Json &j) {
  SymbolLocation loc;
  loc.uri = j.value("uri", std::string());
  loc.line = j.value("line", 0u);
  loc.character = j.value("character", 0u);
  loc.end_line = j.value("endLine", loc.line);
  loc.end_character = j.value("endCharacter", loc.character);
  return loc;
}

/// Reserved keywords that must not be recorded as references.
const std::unordered_set<std::string> &PloyKeywordSet() {
  static const std::unordered_set<std::string> kSet = {
      "FUNC",   "PIPELINE",  "STRUCT",  "LET",      "VAR",     "IF",
      "ELSE",   "WHILE",     "FOR",     "MATCH",    "CASE",    "DEFAULT",
      "RETURN", "LINK",      "IMPORT",  "EXPORT",   "PACKAGE", "AS",
      "IN",     "MAP_TYPE",  "CALL",    "NEW",      "METHOD",  "GET",
      "SET",    "WITH",      "DELETE",  "EXTEND",   "CONFIG",  "CONVERT",
      "INFER",  "TRUE",      "FALSE",   "NULL",     "AND",     "OR",
      "NOT",    "INT",       "FLOAT",   "STRING",   "BOOL",    "VOID",
      "ARRAY",  "LIST",      "TUPLE",   "DICT",     "OPTION",  "Some",
      "None",   "PIPELINE",  "ENUM",    "I8",       "I16",     "I32",
      "I64",    "U8",        "U16",     "U32",      "U64",     "F32",
      "F64",
  };
  return kSet;
}

}  // namespace

// ────────────────────────────────────────────────────────────────────────────
// URI helpers
// ────────────────────────────────────────────────────────────────────────────

std::string UriToPath(const std::string &uri) {
  constexpr const char *kPrefix = "file://";
  std::string body = uri;
  if (body.rfind(kPrefix, 0) == 0) {
    body.erase(0, std::strlen(kPrefix));
    if (body.size() >= 3 && body[0] == '/' &&
        std::isalpha(static_cast<unsigned char>(body[1])) && body[2] == ':') {
      body.erase(0, 1);
    }
  }
  // Percent-decode.
  std::string out;
  out.reserve(body.size());
  auto from_hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
  };
  for (std::size_t i = 0; i < body.size(); ++i) {
    if (body[i] == '%' && i + 2 < body.size()) {
      const int hi = from_hex(body[i + 1]);
      const int lo = from_hex(body[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back(body[i]);
  }
  return out;
}

std::string PathToUri(const std::string &path) {
  std::string body = path;
#ifdef _WIN32
  std::replace(body.begin(), body.end(), '\\', '/');
  // Windows drive letter — prepend an extra slash to obtain "file:///C:/...".
  if (body.size() >= 2 && std::isalpha(static_cast<unsigned char>(body[0])) &&
      body[1] == ':') {
    return "file:///" + body;
  }
#endif
  if (!body.empty() && body.front() != '/') return "file://" + body;
  return "file://" + body;
}

// ────────────────────────────────────────────────────────────────────────────
// Construction
// ────────────────────────────────────────────────────────────────────────────

SymbolIndex::SymbolIndex() = default;
SymbolIndex::~SymbolIndex() = default;

// ────────────────────────────────────────────────────────────────────────────
// Indexing dispatch
// ────────────────────────────────────────────────────────────────────────────

void SymbolIndex::IndexDocument(const std::string &uri,
                                const std::string &language_id,
                                const std::string &text) {
  std::lock_guard<std::mutex> lock(mu_);
  entries_by_uri_.erase(uri);
  refs_by_uri_.erase(uri);
  if (text.empty()) return;

  if (language_id == "ploy" || language_id == "poly") {
    IndexPloy(uri, text);
  } else if (language_id == "cpp" || language_id == "c++" ||
             language_id == "cxx") {
    IndexCpp(uri, text);
  } else if (language_id == "python") {
    IndexPython(uri, text);
  } else if (language_id == "rust") {
    IndexRust(uri, text);
  } else if (language_id == "java") {
    IndexJava(uri, text);
  } else if (language_id == "csharp" || language_id == "dotnet" ||
             language_id == "cs") {
    IndexDotnet(uri, text);
  } else {
    return;
  }
  CollectReferences(uri, text);
}

void SymbolIndex::RemoveDocument(const std::string &uri) {
  std::lock_guard<std::mutex> lock(mu_);
  entries_by_uri_.erase(uri);
  refs_by_uri_.erase(uri);
}

std::size_t SymbolIndex::DocumentCount() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::set<std::string> uris;
  for (const auto &kv : entries_by_uri_) uris.insert(kv.first);
  for (const auto &kv : refs_by_uri_) uris.insert(kv.first);
  return uris.size();
}

std::size_t SymbolIndex::EntryCount() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::size_t n = 0;
  for (const auto &kv : entries_by_uri_) n += kv.second.size();
  return n;
}

std::vector<IndexEntry> SymbolIndex::Entries() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<IndexEntry> out;
  for (const auto &kv : entries_by_uri_)
    out.insert(out.end(), kv.second.begin(), kv.second.end());
  return out;
}

// ────────────────────────────────────────────────────────────────────────────
// .ploy indexer — covers FUNC / PIPELINE / STRUCT / LET / VAR / IMPORT /
// EXPORT / LINK / MAP_TYPE.  LINK supports both legacy
// `LINK lang::module::func AS …` and tuple form
// `LINK(target_lang, source_lang, target_func, source_func)`.
// ────────────────────────────────────────────────────────────────────────────

void SymbolIndex::IndexPloy(const std::string &uri, const std::string &text) {
  auto &out = entries_by_uri_[uri];
  const auto lines = SplitLines(text);

  for (std::uint32_t ln = 0; ln < lines.size(); ++ln) {
    std::string_view line = lines[ln];
    std::size_t i = SkipSpaces(line, 0);
    if (i == line.size()) continue;
    if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') continue;

    // ── FUNC name(args) -> ret ────────────────────────────────────────
    if (StartsWithKeyword(line, i, "FUNC")) {
      std::size_t c = i + 4;
      const std::size_t name_pos = SkipSpaces(line, c);
      c = name_pos;
      const std::string name = ReadIdent(line, c);
      if (name.empty()) continue;

      IndexEntry e;
      e.name = name;
      e.qualified_name = "ploy::" + name;
      e.language = "ploy";
      e.kind = IndexEntryKind::kFunction;
      e.definition = MakeLoc(uri, ln, static_cast<std::uint32_t>(name_pos),
                              static_cast<std::uint32_t>(name.size()));

      // Capture signature text (params + return) for completeness.
      std::size_t sig_end = line.size();
      e.signature.assign(line.substr(name_pos, sig_end - name_pos));

      // Try to grab the return type after "->".
      const std::size_t arrow = line.find("->", c);
      if (arrow != std::string_view::npos) {
        std::size_t k = SkipSpaces(line, arrow + 2);
        std::size_t s = k;
        while (k < line.size() && (IsIdentChar(line[k]) || line[k] == ':' ||
                                   line[k] == '<' || line[k] == '>' ||
                                   line[k] == ','))
          ++k;
        e.type_definition_name = std::string(line.substr(s, k - s));
      }
      out.push_back(std::move(e));
      continue;
    }

    // ── PIPELINE name { … } ───────────────────────────────────────────
    if (StartsWithKeyword(line, i, "PIPELINE")) {
      std::size_t c = i + 8;
      const std::size_t name_pos = SkipSpaces(line, c);
      c = name_pos;
      const std::string name = ReadIdent(line, c);
      if (name.empty()) continue;
      IndexEntry e;
      e.name = name;
      e.qualified_name = "ploy::pipeline::" + name;
      e.language = "ploy";
      e.kind = IndexEntryKind::kPipeline;
      e.definition = MakeLoc(uri, ln, static_cast<std::uint32_t>(name_pos),
                              static_cast<std::uint32_t>(name.size()));
      out.push_back(std::move(e));
      continue;
    }

    // ── STRUCT name { … } ─────────────────────────────────────────────
    if (StartsWithKeyword(line, i, "STRUCT")) {
      std::size_t c = i + 6;
      const std::size_t name_pos = SkipSpaces(line, c);
      c = name_pos;
      const std::string name = ReadIdent(line, c);
      if (name.empty()) continue;
      IndexEntry e;
      e.name = name;
      e.qualified_name = "ploy::" + name;
      e.language = "ploy";
      e.kind = IndexEntryKind::kStruct;
      e.definition = MakeLoc(uri, ln, static_cast<std::uint32_t>(name_pos),
                              static_cast<std::uint32_t>(name.size()));
      out.push_back(std::move(e));
      continue;
    }

    // ── LET / VAR name [: type] = … ────────────────────────────────────
    const bool is_let = StartsWithKeyword(line, i, "LET");
    const bool is_var = !is_let && StartsWithKeyword(line, i, "VAR");
    if (is_let || is_var) {
      std::size_t c = i + 3;
      const std::size_t name_pos = SkipSpaces(line, c);
      c = name_pos;
      const std::string name = ReadIdent(line, c);
      if (name.empty()) continue;
      IndexEntry e;
      e.name = name;
      e.qualified_name = "ploy::" + name;
      e.language = "ploy";
      e.kind = IndexEntryKind::kVariable;
      e.definition = MakeLoc(uri, ln, static_cast<std::uint32_t>(name_pos),
                              static_cast<std::uint32_t>(name.size()));
      // Optional type annotation.
      std::size_t t = SkipSpaces(line, c);
      if (t < line.size() && line[t] == ':') {
        ++t;
        t = SkipSpaces(line, t);
        std::size_t s = t;
        while (t < line.size() && (IsIdentChar(line[t]) || line[t] == ':' ||
                                   line[t] == '<' || line[t] == '>' ||
                                   line[t] == ','))
          ++t;
        e.type_definition_name = std::string(line.substr(s, t - s));
      }
      out.push_back(std::move(e));
      continue;
    }

    // ── IMPORT lang::module ; / IMPORT lang PACKAGE pkg ; ─────────────
    if (StartsWithKeyword(line, i, "IMPORT")) {
      std::size_t c = i + 6;
      const std::size_t lang_pos = SkipSpaces(line, c);
      c = lang_pos;
      const std::string lang = ReadIdent(line, c);
      c = SkipSpaces(line, c);
      // "lang::module" form.
      if (c + 1 < line.size() && line[c] == ':' && line[c + 1] == ':') {
        c += 2;
        std::string bare;
        const std::size_t qpos = c;
        const std::string qual = ReadQualified(line, c, bare);
        if (bare.empty()) continue;
        IndexEntry e;
        e.name = bare;
        e.qualified_name = lang + "::" + qual;
        e.language = lang;
        e.kind = IndexEntryKind::kImport;
        e.definition = MakeLoc(uri, ln, static_cast<std::uint32_t>(qpos),
                                static_cast<std::uint32_t>(qual.size()));
        out.push_back(std::move(e));
        continue;
      }
      // "lang PACKAGE pkg" form.
      if (StartsWithKeyword(line, c, "PACKAGE")) {
        c += 7;
        const std::size_t pkg_pos = SkipSpaces(line, c);
        c = pkg_pos;
        const std::string pkg = ReadIdent(line, c);
        if (pkg.empty()) continue;
        IndexEntry e;
        e.name = pkg;
        e.qualified_name = lang + "::package::" + pkg;
        e.language = lang;
        e.kind = IndexEntryKind::kImport;
        e.definition = MakeLoc(uri, ln, static_cast<std::uint32_t>(pkg_pos),
                                static_cast<std::uint32_t>(pkg.size()));
        out.push_back(std::move(e));
      }
      continue;
    }

    // ── EXPORT name AS lang::func ; ───────────────────────────────────
    if (StartsWithKeyword(line, i, "EXPORT")) {
      std::size_t c = i + 6;
      const std::size_t name_pos = SkipSpaces(line, c);
      c = name_pos;
      const std::string name = ReadIdent(line, c);
      if (name.empty()) continue;
      IndexEntry e;
      e.name = name;
      e.qualified_name = "ploy::export::" + name;
      e.language = "ploy";
      e.kind = IndexEntryKind::kFunction;
      e.definition = MakeLoc(uri, ln, static_cast<std::uint32_t>(name_pos),
                              static_cast<std::uint32_t>(name.size()));
      out.push_back(std::move(e));
      continue;
    }

    // ── LINK ────────────────────────────────────────────────────────────
    // Two forms supported by the .ploy frontend:
    //   1. LINK target_lang::mod::func AS …
    //   2. LINK(target_lang, source_lang, target_func, source_func)
    //      { MAP_TYPE … }
    if (StartsWithKeyword(line, i, "LINK")) {
      std::size_t c = i + 4;
      c = SkipSpaces(line, c);

      if (c < line.size() && line[c] == '(') {
        // Tuple form.  Pick out the four idents inside the parens, on
        // this line or the next.  We tolerate trailing whitespace.
        std::string body(line.substr(c + 1));
        // Most .ploy files keep all four args on the same line.
        const std::size_t close = body.find(')');
        if (close != std::string::npos) body = body.substr(0, close);

        std::vector<std::string> args;
        std::stringstream ss(body);
        std::string tok;
        while (std::getline(ss, tok, ',')) args.push_back(Trim(tok));
        if (args.size() < 4) continue;

        const std::string &tlang = args[0];
        const std::string &tfunc = args[2];
        // Bare = last component of "a::b::c".
        std::string bare = tfunc;
        const auto pos = bare.rfind("::");
        if (pos != std::string::npos) bare = bare.substr(pos + 2);

        IndexEntry e;
        e.name = bare;
        e.qualified_name = tlang + "::" + tfunc;
        e.language = "ploy";
        e.kind = IndexEntryKind::kLink;
        // The LINK keyword position is the navigation anchor.
        e.definition = MakeLoc(uri, ln, static_cast<std::uint32_t>(i), 4);
        e.link_target_language = tlang;
        e.link_target_qualified = tfunc;
        out.push_back(std::move(e));
        continue;
      }

      // Legacy form "LINK target_lang::mod::func AS …".
      std::string bare;
      const std::size_t qpos = c;
      const std::string qual = ReadQualified(line, c, bare);
      if (bare.empty()) continue;
      // Split off the language head ("cpp", "python", …) from the
      // qualifier prefix.
      std::string tlang;
      std::string rest;
      const std::size_t sep = qual.find("::");
      if (sep != std::string::npos) {
        tlang = qual.substr(0, sep);
        rest = qual.substr(sep + 2);
      } else {
        tlang = qual;
        rest = qual;
      }
      IndexEntry e;
      e.name = bare;
      e.qualified_name = qual;
      e.language = "ploy";
      e.kind = IndexEntryKind::kLink;
      e.definition = MakeLoc(uri, ln, static_cast<std::uint32_t>(qpos),
                              static_cast<std::uint32_t>(qual.size()));
      e.link_target_language = tlang;
      e.link_target_qualified = rest;
      out.push_back(std::move(e));
      continue;
    }
  }
}

// ────────────────────────────────────────────────────────────────────────────
// C++ indexer — picks up free functions and class declarations.
// We deliberately ignore method bodies; navigating to the class / free
// function declaration is what cross-language LINK targets need.
// ────────────────────────────────────────────────────────────────────────────

void SymbolIndex::IndexCpp(const std::string &uri, const std::string &text) {
  auto &out = entries_by_uri_[uri];
  const auto lines = SplitLines(text);
  // Track the last namespace prefix we entered.  We do not handle
  // nested namespaces deeply; the qualifier is appended to entries
  // until a closing '}' at depth zero is seen.
  std::vector<std::string> ns_stack;
  int brace_depth = 0;
  std::vector<int> ns_open_depth;

  for (std::uint32_t ln = 0; ln < lines.size(); ++ln) {
    std::string_view line = lines[ln];
    std::size_t i = SkipSpaces(line, 0);
    if (i == line.size()) continue;
    if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') {
      goto count_braces;
    }

    if (StartsWithKeyword(line, i, "namespace")) {
      std::size_t c = i + 9;
      c = SkipSpaces(line, c);
      std::string name = ReadIdent(line, c);
      // Anonymous namespace stays empty — still push so the brace
      // tracking pops correctly.
      ns_stack.push_back(name);
      ns_open_depth.push_back(brace_depth);
    } else if (StartsWithKeyword(line, i, "class") ||
               StartsWithKeyword(line, i, "struct")) {
      const bool is_struct = StartsWithKeyword(line, i, "struct");
      const std::size_t kwlen = is_struct ? 6 : 5;
      std::size_t c = i + kwlen;
      const std::size_t name_pos = SkipSpaces(line, c);
      c = name_pos;
      const std::string name = ReadIdent(line, c);
      if (!name.empty()) {
        IndexEntry e;
        e.name = name;
        std::string qual = name;
        for (auto it = ns_stack.rbegin(); it != ns_stack.rend(); ++it) {
          if (!it->empty()) qual = *it + "::" + qual;
        }
        e.qualified_name = qual;
        e.language = "cpp";
        e.kind = IndexEntryKind::kForeignClass;
        e.definition = MakeLoc(uri, ln, static_cast<std::uint32_t>(name_pos),
                                static_cast<std::uint32_t>(name.size()));
        e.signature.assign(line);
        out.push_back(std::move(e));
      }
    } else {
      // Free-function heuristic: a line whose stripped form ends in
      // "(...)" or "(... {" and contains a top-level identifier
      // followed by '('.  We accept lines whose brace depth equals the
      // number of open namespaces — i.e. we are in namespace body
      // (or file top level) but not inside any function/class body.
      if (brace_depth == static_cast<int>(ns_open_depth.size())) {
        const std::size_t paren = line.find('(', i);
        if (paren != std::string_view::npos) {
          // Walk back over identifier.
          std::size_t e = paren;
          while (e > i && (line[e - 1] == ' ' || line[e - 1] == '\t')) --e;
          std::size_t s = e;
          while (s > i && IsIdentChar(line[s - 1])) --s;
          if (s < e) {
            std::string name(line.substr(s, e - s));
            // Filter common control-flow keywords.
            if (name != "if" && name != "for" && name != "while" &&
                name != "switch" && name != "return" && name != "sizeof" &&
                name != "static_cast" && name != "reinterpret_cast" &&
                name != "dynamic_cast" && name != "const_cast" &&
                IsIdentStart(name.front())) {
              IndexEntry entry;
              entry.name = name;
              std::string qual = name;
              for (auto it = ns_stack.rbegin(); it != ns_stack.rend(); ++it) {
                if (!it->empty()) qual = *it + "::" + qual;
              }
              entry.qualified_name = qual;
              entry.language = "cpp";
              entry.kind = IndexEntryKind::kForeignFunction;
              entry.definition = MakeLoc(uri, ln, static_cast<std::uint32_t>(s),
                                          static_cast<std::uint32_t>(name.size()));
              entry.signature.assign(line);
              out.push_back(std::move(entry));
            }
          }
        }
      }
    }

  count_braces:
    for (char ch : line) {
      if (ch == '{') ++brace_depth;
      else if (ch == '}') {
        --brace_depth;
        if (!ns_open_depth.empty() && brace_depth < ns_open_depth.back()) {
          ns_open_depth.pop_back();
          if (!ns_stack.empty()) ns_stack.pop_back();
        }
      }
    }
  }
}

// ────────────────────────────────────────────────────────────────────────────
// Python indexer — `def name(...)` and `class Name`.
// ────────────────────────────────────────────────────────────────────────────

void SymbolIndex::IndexPython(const std::string &uri,
                              const std::string &text) {
  auto &out = entries_by_uri_[uri];
  const auto lines = SplitLines(text);
  for (std::uint32_t ln = 0; ln < lines.size(); ++ln) {
    std::string_view line = lines[ln];
    const std::size_t i = SkipSpaces(line, 0);
    if (i == line.size()) continue;
    if (line[i] == '#') continue;

    if (StartsWithKeyword(line, i, "def")) {
      std::size_t c = i + 3;
      const std::size_t name_pos = SkipSpaces(line, c);
      c = name_pos;
      std::string name = ReadIdent(line, c);
      if (name.empty()) continue;
      IndexEntry e;
      e.name = name;
      e.qualified_name = name;
      e.language = "python";
      e.kind = IndexEntryKind::kForeignFunction;
      e.definition = MakeLoc(uri, ln, static_cast<std::uint32_t>(name_pos),
                              static_cast<std::uint32_t>(name.size()));
      e.signature.assign(line);
      out.push_back(std::move(e));
    } else if (StartsWithKeyword(line, i, "class")) {
      std::size_t c = i + 5;
      const std::size_t name_pos = SkipSpaces(line, c);
      c = name_pos;
      std::string name = ReadIdent(line, c);
      if (name.empty()) continue;
      IndexEntry e;
      e.name = name;
      e.qualified_name = name;
      e.language = "python";
      e.kind = IndexEntryKind::kForeignClass;
      e.definition = MakeLoc(uri, ln, static_cast<std::uint32_t>(name_pos),
                              static_cast<std::uint32_t>(name.size()));
      e.signature.assign(line);
      out.push_back(std::move(e));
    }
  }
}

// ────────────────────────────────────────────────────────────────────────────
// Rust indexer — `fn name(...)`, `pub fn name(...)`, `struct Name`,
// `enum Name`, `trait Name`, `impl Name`.
// ────────────────────────────────────────────────────────────────────────────

void SymbolIndex::IndexRust(const std::string &uri, const std::string &text) {
  auto &out = entries_by_uri_[uri];
  const auto lines = SplitLines(text);
  for (std::uint32_t ln = 0; ln < lines.size(); ++ln) {
    std::string_view line = lines[ln];
    std::size_t i = SkipSpaces(line, 0);
    if (i == line.size()) continue;
    if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') continue;

    // Skip leading visibility qualifier.
    if (StartsWithKeyword(line, i, "pub")) {
      i = SkipSpaces(line, i + 3);
      // pub(crate), pub(super), pub(in path)
      if (i < line.size() && line[i] == '(') {
        std::size_t close = line.find(')', i);
        if (close != std::string_view::npos) i = SkipSpaces(line, close + 1);
      }
    }

    auto emit = [&](IndexEntryKind kind, const std::string &name,
                    std::size_t pos) {
      if (name.empty()) return;
      IndexEntry e;
      e.name = name;
      e.qualified_name = name;
      e.language = "rust";
      e.kind = kind;
      e.definition = MakeLoc(uri, ln, static_cast<std::uint32_t>(pos),
                              static_cast<std::uint32_t>(name.size()));
      e.signature.assign(line);
      out.push_back(std::move(e));
    };

    if (StartsWithKeyword(line, i, "fn")) {
      std::size_t c = i + 2;
      std::size_t name_pos = SkipSpaces(line, c);
      c = name_pos;
      emit(IndexEntryKind::kForeignFunction, ReadIdent(line, c), name_pos);
    } else if (StartsWithKeyword(line, i, "struct") ||
               StartsWithKeyword(line, i, "enum") ||
               StartsWithKeyword(line, i, "trait") ||
               StartsWithKeyword(line, i, "impl")) {
      const std::size_t kwlen =
          StartsWithKeyword(line, i, "struct") ? 6
              : StartsWithKeyword(line, i, "trait")  ? 5
              : StartsWithKeyword(line, i, "impl")   ? 4
                                                     : 4;
      std::size_t c = i + kwlen;
      std::size_t name_pos = SkipSpaces(line, c);
      c = name_pos;
      emit(IndexEntryKind::kForeignClass, ReadIdent(line, c), name_pos);
    }
  }
}

// ────────────────────────────────────────────────────────────────────────────
// Java / .NET indexers — minimal: top-level class/interface declarations
// and methods with a visibility modifier.
// ────────────────────────────────────────────────────────────────────────────

void SymbolIndex::IndexJava(const std::string &uri, const std::string &text) {
  auto &out = entries_by_uri_[uri];
  const auto lines = SplitLines(text);
  for (std::uint32_t ln = 0; ln < lines.size(); ++ln) {
    std::string_view line = lines[ln];
    std::size_t i = SkipSpaces(line, 0);
    if (i == line.size()) continue;
    if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') continue;

    // Strip "public", "private", "protected", "static", "final".
    auto consume = [&](std::string_view kw) {
      if (StartsWithKeyword(line, i, kw)) {
        i = SkipSpaces(line, i + kw.size());
        return true;
      }
      return false;
    };
    while (consume("public") || consume("private") || consume("protected") ||
           consume("static") || consume("final") || consume("abstract")) {
    }

    if (StartsWithKeyword(line, i, "class") ||
        StartsWithKeyword(line, i, "interface") ||
        StartsWithKeyword(line, i, "enum") ||
        StartsWithKeyword(line, i, "record")) {
      std::size_t c = i +
          (StartsWithKeyword(line, i, "interface") ? 9
           : StartsWithKeyword(line, i, "record")  ? 6
           : StartsWithKeyword(line, i, "class")   ? 5
                                                   : 4);
      std::size_t name_pos = SkipSpaces(line, c);
      c = name_pos;
      std::string name = ReadIdent(line, c);
      if (name.empty()) continue;
      IndexEntry e;
      e.name = name;
      e.qualified_name = name;
      e.language = "java";
      e.kind = IndexEntryKind::kForeignClass;
      e.definition = MakeLoc(uri, ln, static_cast<std::uint32_t>(name_pos),
                              static_cast<std::uint32_t>(name.size()));
      e.signature.assign(line);
      out.push_back(std::move(e));
    }
  }
}

void SymbolIndex::IndexDotnet(const std::string &uri,
                              const std::string &text) {
  // Java's structural conventions overlap enough with C# at the index
  // level (class/interface/enum/record + method declarations) that we
  // share the implementation, then re-tag the language id.
  IndexJava(uri, text);
  auto it = entries_by_uri_.find(uri);
  if (it == entries_by_uri_.end()) return;
  for (auto &e : it->second) e.language = "dotnet";
}

// ────────────────────────────────────────────────────────────────────────────
// Reference collection — every identifier occurrence whose token text
// matches an entry name is recorded as a reference.  Definition sites
// are tagged with `is_definition = true` so References() can include or
// exclude them on demand.
// ────────────────────────────────────────────────────────────────────────────

void SymbolIndex::CollectReferences(const std::string &uri,
                                    const std::string &text) {
  auto &refs_for_uri = refs_by_uri_[uri];
  refs_for_uri.clear();

  // Build the union of all known names across the workspace.
  std::unordered_set<std::string> known;
  std::unordered_set<std::string> definitions_at_uri;
  for (const auto &kv : entries_by_uri_) {
    for (const auto &e : kv.second) known.insert(e.name);
  }
  if (known.empty()) return;
  const auto it = entries_by_uri_.find(uri);
  if (it != entries_by_uri_.end()) {
    for (const auto &e : it->second) {
      const std::string key = std::to_string(e.definition.line) + ":" +
                              std::to_string(e.definition.character) + ":" +
                              e.name;
      definitions_at_uri.insert(key);
    }
  }

  const auto &kw = PloyKeywordSet();
  const auto lines = SplitLines(text);
  for (std::uint32_t ln = 0; ln < lines.size(); ++ln) {
    std::string_view line = lines[ln];
    std::size_t i = 0;
    while (i < line.size()) {
      if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') break;
      if (line[i] == '#') break;  // comment in python / etc.
      if (!IsIdentStart(line[i])) {
        ++i;
        continue;
      }
      const std::size_t s = i;
      while (i < line.size() && IsIdentChar(line[i])) ++i;
      std::string tok(line.substr(s, i - s));
      if (kw.count(tok)) continue;
      if (!known.count(tok)) continue;
      ReferenceSite r;
      r.name = std::move(tok);
      r.location = MakeLoc(uri, ln, static_cast<std::uint32_t>(s),
                            static_cast<std::uint32_t>(i - s));
      const std::string key = std::to_string(ln) + ":" +
                              std::to_string(s) + ":" + r.name;
      r.is_definition = definitions_at_uri.count(key) > 0;
      refs_for_uri.push_back(std::move(r));
    }
  }
}

// ────────────────────────────────────────────────────────────────────────────
// Navigation queries
// ────────────────────────────────────────────────────────────────────────────

std::vector<SymbolLocation> SymbolIndex::Definition(
    const std::string &name) const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<SymbolLocation> out;
  for (const auto &kv : entries_by_uri_) {
    for (const auto &e : kv.second) {
      if (e.name == name) out.push_back(e.definition);
    }
  }
  return out;
}

std::vector<SymbolLocation> SymbolIndex::Declaration(
    const std::string &name) const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<SymbolLocation> out;
  for (const auto &kv : entries_by_uri_) {
    for (const auto &e : kv.second) {
      if (e.name != name) continue;
      out.push_back(e.declaration ? *e.declaration : e.definition);
    }
  }
  return out;
}

std::vector<SymbolLocation> SymbolIndex::Implementation(
    const std::string &name) const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<SymbolLocation> out;
  for (const auto &kv : entries_by_uri_) {
    for (const auto &e : kv.second) {
      if (e.name != name) continue;
      // For LINK entries, the "implementation" is the host-language
      // target — answer with that target's definition if present.
      if (e.kind == IndexEntryKind::kLink) {
        for (const auto &kv2 : entries_by_uri_) {
          for (const auto &t : kv2.second) {
            if (t.language == e.link_target_language &&
                (t.qualified_name == e.link_target_qualified ||
                 t.name == e.name)) {
              out.push_back(t.definition);
            }
          }
        }
        continue;
      }
      out.push_back(e.implementation ? *e.implementation : e.definition);
    }
  }
  return out;
}

std::vector<SymbolLocation> SymbolIndex::TypeDefinition(
    const std::string &name) const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<SymbolLocation> out;
  for (const auto &kv : entries_by_uri_) {
    for (const auto &e : kv.second) {
      if (e.name != name) continue;
      if (e.type_definition_name.empty()) continue;
      // Resolve the type name to its definition (within .ploy or any
      // imported language).  Strip generic parameters for matching.
      std::string tname = e.type_definition_name;
      const std::size_t lt = tname.find('<');
      if (lt != std::string::npos) tname.erase(lt);
      // Strip language prefix "lang::" if any.
      const std::size_t sep = tname.rfind("::");
      if (sep != std::string::npos) tname = tname.substr(sep + 2);
      for (const auto &kv2 : entries_by_uri_) {
        for (const auto &t : kv2.second) {
          if (t.name == tname && (t.kind == IndexEntryKind::kStruct ||
                                  t.kind == IndexEntryKind::kForeignClass)) {
            out.push_back(t.definition);
          }
        }
      }
    }
  }
  return out;
}

std::vector<SymbolLocation> SymbolIndex::References(
    const std::string &name, bool include_definition) const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<SymbolLocation> out;
  for (const auto &kv : refs_by_uri_) {
    for (const auto &r : kv.second) {
      if (r.name != name) continue;
      if (!include_definition && r.is_definition) continue;
      out.push_back(r.location);
    }
  }
  return out;
}

// ────────────────────────────────────────────────────────────────────────────
// Cross-language helpers
// ────────────────────────────────────────────────────────────────────────────

std::vector<SymbolLocation> SymbolIndex::CrossLanguageTarget(
    const std::string &target_language,
    const std::string &qualified_target) const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<SymbolLocation> out;
  // Bare last component, used when the host language file does not
  // emit a fully-qualified name (Python / Rust / Java).
  std::string bare = qualified_target;
  const auto pos = bare.rfind("::");
  if (pos != std::string::npos) bare = bare.substr(pos + 2);

  for (const auto &kv : entries_by_uri_) {
    for (const auto &e : kv.second) {
      if (e.language != target_language) continue;
      if (e.kind != IndexEntryKind::kForeignFunction &&
          e.kind != IndexEntryKind::kForeignClass) {
        continue;
      }
      if (e.qualified_name == qualified_target || e.name == bare) {
        out.push_back(e.definition);
      }
    }
  }
  return out;
}

std::vector<SymbolLocation> SymbolIndex::CrossLanguageBackrefs(
    const std::string &target_language,
    const std::string &qualified_target) const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<SymbolLocation> out;
  std::string bare = qualified_target;
  const auto pos = bare.rfind("::");
  if (pos != std::string::npos) bare = bare.substr(pos + 2);

  for (const auto &kv : entries_by_uri_) {
    for (const auto &e : kv.second) {
      if (e.kind != IndexEntryKind::kLink) continue;
      if (e.link_target_language != target_language) continue;
      if (e.link_target_qualified == qualified_target ||
          e.name == bare) {
        out.push_back(e.definition);
      }
    }
  }
  return out;
}

// ────────────────────────────────────────────────────────────────────────────
// Persistence (.polyc-cache/symbol_index.json)
// ────────────────────────────────────────────────────────────────────────────

bool SymbolIndex::SaveToCache(const std::string &cache_dir) const {
  std::lock_guard<std::mutex> lock(mu_);
  std::error_code ec;
  fs::create_directories(cache_dir, ec);
  if (ec) return false;

  Json root;
  root["version"] = 1;
  root["generator"] = "polyls.symbol_index";

  Json docs = Json::array();
  for (const auto &kv : entries_by_uri_) {
    Json d;
    d["uri"] = kv.first;
    Json ents = Json::array();
    for (const auto &e : kv.second) {
      Json je;
      je["name"] = e.name;
      je["qualifiedName"] = e.qualified_name;
      je["language"] = e.language;
      je["kind"] = KindToString(e.kind);
      je["definition"] = LocToJson(e.definition);
      if (e.declaration) je["declaration"] = LocToJson(*e.declaration);
      if (e.implementation)
        je["implementation"] = LocToJson(*e.implementation);
      if (!e.link_target_language.empty())
        je["linkTargetLanguage"] = e.link_target_language;
      if (!e.link_target_qualified.empty())
        je["linkTargetQualified"] = e.link_target_qualified;
      if (!e.type_definition_name.empty())
        je["typeDefinitionName"] = e.type_definition_name;
      if (!e.signature.empty()) je["signature"] = e.signature;
      ents.push_back(std::move(je));
    }
    d["entries"] = std::move(ents);

    auto rit = refs_by_uri_.find(kv.first);
    if (rit != refs_by_uri_.end()) {
      Json refs = Json::array();
      for (const auto &r : rit->second) {
        Json jr;
        jr["name"] = r.name;
        jr["location"] = LocToJson(r.location);
        jr["isDefinition"] = r.is_definition;
        refs.push_back(std::move(jr));
      }
      d["references"] = std::move(refs);
    }
    docs.push_back(std::move(d));
  }
  root["documents"] = std::move(docs);

  const fs::path path = fs::path(cache_dir) / "symbol_index.json";
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out << root.dump(2);
  return out.good();
}

bool SymbolIndex::LoadFromCache(const std::string &cache_dir) {
  std::lock_guard<std::mutex> lock(mu_);
  entries_by_uri_.clear();
  refs_by_uri_.clear();

  const fs::path path = fs::path(cache_dir) / "symbol_index.json";
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  Json root;
  try {
    in >> root;
  } catch (const std::exception &) {
    return false;
  }
  if (!root.is_object()) return false;
  if (root.value("version", 0) != 1) return false;

  for (const auto &d : root.value("documents", Json::array())) {
    const std::string uri = d.value("uri", std::string());
    if (uri.empty()) continue;
    auto &out = entries_by_uri_[uri];
    for (const auto &je : d.value("entries", Json::array())) {
      IndexEntry e;
      e.name = je.value("name", std::string());
      e.qualified_name = je.value("qualifiedName", std::string());
      e.language = je.value("language", std::string());
      e.kind = KindFromString(je.value("kind", std::string("function")));
      if (je.contains("definition"))
        e.definition = LocFromJson(je["definition"]);
      if (je.contains("declaration"))
        e.declaration = LocFromJson(je["declaration"]);
      if (je.contains("implementation"))
        e.implementation = LocFromJson(je["implementation"]);
      e.link_target_language =
          je.value("linkTargetLanguage", std::string());
      e.link_target_qualified =
          je.value("linkTargetQualified", std::string());
      e.type_definition_name =
          je.value("typeDefinitionName", std::string());
      e.signature = je.value("signature", std::string());
      out.push_back(std::move(e));
    }
    auto &refs = refs_by_uri_[uri];
    for (const auto &jr : d.value("references", Json::array())) {
      ReferenceSite r;
      r.name = jr.value("name", std::string());
      if (jr.contains("location")) r.location = LocFromJson(jr["location"]);
      r.is_definition = jr.value("isDefinition", false);
      refs.push_back(std::move(r));
    }
  }
  return true;
}

}  // namespace polyglot::polyls
