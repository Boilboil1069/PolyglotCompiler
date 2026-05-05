/**
 * @file     refactor.cpp
 * @brief    Implementation of the polyls workspace refactoring engine.
 *
 * @ingroup  Tool / polyls
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/polyls/polyls_core/refactor.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_set>

namespace polyglot::polyls {

namespace {

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

const DocumentView *FindDocument(const std::vector<DocumentView> &docs,
                                 const std::string &uri) {
  for (const auto &d : docs) {
    if (d.uri == uri) return &d;
  }
  return nullptr;
}

/// Read the document at @p uri from @p docs and split it into lines.
/// The returned vector is empty when the document is not open.
std::vector<std::string> SplitDocLines(const DocumentView &doc) {
  std::vector<std::string> out;
  std::size_t start = 0;
  const std::string &t = doc.text;
  for (std::size_t i = 0; i < t.size(); ++i) {
    if (t[i] == '\n') {
      std::size_t end = i;
      if (end > start && t[end - 1] == '\r') --end;
      out.emplace_back(t.substr(start, end - start));
      start = i + 1;
    }
  }
  out.emplace_back(t.substr(start));
  return out;
}

/// Languages that polyls knows how to rename.  The set matches the
/// indexers in @ref SymbolIndex.
const std::unordered_set<std::string> &KnownLanguages() {
  static const std::unordered_set<std::string> kSet{
      "ploy", "poly", "cpp", "c++", "python", "rust",
      "java", "dotnet", "csharp"};
  return kSet;
}

/// Reserved identifiers that must never be renamed.  Conservative
/// superset across all front-ends polyls scans.
const std::unordered_set<std::string> &ReservedIdentifiers() {
  static const std::unordered_set<std::string> kSet{
      // .ploy keywords
      "FUNC", "PIPELINE", "STRUCT", "LET", "VAR", "RETURN", "IF", "ELSE",
      "WHILE", "FOR", "BREAK", "CONTINUE", "IMPORT", "EXPORT", "LINK",
      "CONFIG", "PACKAGE", "AS", "NEW", "METHOD", "GET", "SET", "WITH",
      "DELETE", "EXTEND", "MAP_TYPE", "CONVERT", "AND", "OR", "NOT",
      "TRUE", "FALSE", "NULL", "INT", "FLOAT", "BOOL", "STRING", "VOID",
      // Common host-language keywords (sample only — collisions are
      // unlikely because rename uses bare-identifier matching limited
      // to the reference set the index already collected).
      "if", "else", "while", "for", "return", "break", "continue",
      "class", "struct", "enum", "fn", "def", "void", "int", "double",
      "float", "bool", "true", "false", "null", "None", "import",
      "from", "package", "public", "private", "protected", "namespace",
      "using", "let", "var", "const", "static", "extern", "auto",
      "pub", "impl", "trait", "interface", "record"};
  return kSet;
}

}  // namespace

bool IsValidIdentifier(const std::string &name) {
  if (name.empty()) return false;
  if (!IsIdentStart(name.front())) return false;
  for (char c : name) {
    if (!IsIdentChar(c)) return false;
  }
  return ReservedIdentifiers().count(name) == 0;
}

RefactorToken ResolveIdentifierAt(const std::string &text, std::uint32_t line,
                                  std::uint32_t character,
                                  const std::string &language_id) {
  RefactorToken t;
  t.language = language_id;
  t.line = line;
  const auto lines = SplitLines(text);
  if (line >= lines.size()) return t;
  std::string_view ln = lines[line];
  std::size_t col = character;
  if (col > ln.size()) col = ln.size();
  std::size_t left = col;
  while (left > 0 && IsIdentChar(ln[left - 1])) --left;
  std::size_t right = col;
  while (right < ln.size() && IsIdentChar(ln[right])) ++right;
  if (left == right) return t;
  t.bare.assign(ln.substr(left, right - left));
  t.start_col = static_cast<std::uint32_t>(left);
  t.end_col = static_cast<std::uint32_t>(right);
  t.valid = IsIdentStart(ln[left]) &&
            ReservedIdentifiers().count(t.bare) == 0;
  return t;
}

std::optional<lsp::Range> PrepareRename(const std::vector<DocumentView> &docs,
                                        const std::string &uri,
                                        std::uint32_t line,
                                        std::uint32_t character) {
  const DocumentView *doc = FindDocument(docs, uri);
  if (!doc) return std::nullopt;
  RefactorToken t =
      ResolveIdentifierAt(doc->text, line, character, doc->language_id);
  if (!t.valid) return std::nullopt;
  lsp::Range r;
  r.start.line = t.line;
  r.start.character = t.start_col;
  r.end.line = t.line;
  r.end.character = t.end_col;
  return r;
}

namespace {

/// Walk every identifier occurrence of @p name across @p doc and
/// append matching ranges into @p out.  Skips occurrences inside
/// double-quoted string literals and `//`-style comments so identifier
/// substrings inside text payloads are never rewritten.
void CollectOccurrences(const DocumentView &doc, const std::string &name,
                        std::vector<lsp::TextEdit> &out,
                        const std::string &new_text) {
  const auto lines = SplitDocLines(doc);
  for (std::uint32_t li = 0; li < lines.size(); ++li) {
    const std::string &ln = lines[li];
    bool in_string = false;
    char quote = 0;
    for (std::size_t i = 0; i < ln.size();) {
      const char c = ln[i];
      if (!in_string) {
        // Comments — strip rest of the line.
        if (c == '/' && i + 1 < ln.size() && ln[i + 1] == '/') break;
        if (c == '#') break;  // Python-style.
        if (c == '"' || c == '\'') {
          in_string = true;
          quote = c;
          ++i;
          continue;
        }
        if (IsIdentStart(c)) {
          std::size_t start = i;
          ++i;
          while (i < ln.size() && IsIdentChar(ln[i])) ++i;
          if (ln.compare(start, i - start, name) == 0) {
            lsp::TextEdit te;
            te.range.start.line = li;
            te.range.start.character =
                static_cast<std::uint32_t>(start);
            te.range.end.line = li;
            te.range.end.character = static_cast<std::uint32_t>(i);
            te.new_text = new_text;
            out.push_back(std::move(te));
          }
          continue;
        }
        ++i;
      } else {
        if (c == '\\' && i + 1 < ln.size()) {
          i += 2;
          continue;
        }
        if (c == quote) {
          in_string = false;
          quote = 0;
        }
        ++i;
      }
    }
  }
}

}  // namespace

std::optional<lsp::WorkspaceEdit> BuildRenameEdit(
    const SymbolIndex &index, const std::vector<DocumentView> &docs,
    const std::string &uri, std::uint32_t line, std::uint32_t character,
    const std::string &new_name) {
  if (!IsValidIdentifier(new_name)) return std::nullopt;
  const DocumentView *origin = FindDocument(docs, uri);
  if (!origin) return std::nullopt;
  RefactorToken t = ResolveIdentifierAt(origin->text, line, character,
                                        origin->language_id);
  if (!t.valid) return std::nullopt;
  if (t.bare == new_name) {
    // Nothing to do, return an empty edit so the editor can no-op.
    return lsp::WorkspaceEdit{};
  }

  lsp::WorkspaceEdit edit;

  // 1. Rewrite every occurrence in every open document.  This is the
  //    authoritative pass: we own the buffer contents so we can
  //    guarantee the textual range of each replacement.
  for (const auto &doc : docs) {
    if (KnownLanguages().count(doc.language_id) == 0) continue;
    std::vector<lsp::TextEdit> edits;
    CollectOccurrences(doc, t.bare, edits, new_name);
    if (!edits.empty()) {
      auto &slot = edit.changes[doc.uri];
      slot.insert(slot.end(), edits.begin(), edits.end());
    }
  }

  // 2. For files that the index knows about but the editor has not
  //    opened, fall back to the locations recorded by the indexer.
  //    The editor is expected to load and patch them through its own
  //    file IO when applying the WorkspaceEdit.
  std::set<std::string> open_uris;
  for (const auto &d : docs) open_uris.insert(d.uri);
  auto add_index_locations =
      [&](const std::vector<SymbolLocation> &locs) {
        for (const auto &loc : locs) {
          if (open_uris.count(loc.uri)) continue;
          lsp::TextEdit te;
          te.range.start.line = loc.line;
          te.range.start.character = loc.character;
          te.range.end.line = loc.end_line;
          te.range.end.character = loc.end_character;
          te.new_text = new_name;
          edit.changes[loc.uri].push_back(std::move(te));
        }
      };
  add_index_locations(index.References(t.bare, /*include_definition=*/true));

  // 3. Cross-language hop: a rename initiated inside a host-language
  //    file additionally rewrites every .ploy LINK / EXPORT site that
  //    targets the bare identifier.
  if (t.language != "ploy" && t.language != "poly" && !t.language.empty()) {
    add_index_locations(
        index.CrossLanguageBackrefs(t.language, t.bare));
  }

  return edit;
}

namespace {

/// Best-effort body indentation derived from the first non-blank line
/// of the selection.
std::string DetectIndent(const std::string &line) {
  std::string out;
  for (char c : line) {
    if (c == ' ' || c == '\t')
      out.push_back(c);
    else
      break;
  }
  return out;
}

/// Build the textual body of an extracted .ploy FUNC from the snippet
/// @p body (already de-indented).  The wrapper picks `INT` as a safe
/// placeholder return type — the user is expected to refine it.
std::string BuildExtractedFunction(const std::string &name,
                                   const std::string &body) {
  std::ostringstream oss;
  oss << "FUNC " << name << "() -> VOID {\n";
  std::istringstream in(body);
  std::string ln;
  while (std::getline(in, ln)) {
    oss << "    " << ln << "\n";
  }
  oss << "}\n\n";
  return oss.str();
}

}  // namespace

std::vector<lsp::CodeAction> BuildCodeActions(
    const SymbolIndex & /*index*/, const std::vector<DocumentView> &docs,
    const std::string &uri, const lsp::Range &range,
    const std::string &new_function_name) {
  std::vector<lsp::CodeAction> actions;
  const DocumentView *doc = FindDocument(docs, uri);
  if (!doc) return actions;

  const auto lines = SplitDocLines(*doc);
  const bool is_ploy = doc->language_id == "ploy" || doc->language_id == "poly";

  // ── Extract function ──────────────────────────────────────────────────
  // Only emit an actionable edit when the selection covers at least one
  // full line of code.  Cross-language extraction is intentionally not
  // attempted — the host-language CST is owned by other tools.
  if (is_ploy && range.end.line >= range.start.line &&
      range.start.line < lines.size()) {
    std::string body;
    const std::uint32_t lo = range.start.line;
    const std::uint32_t hi = std::min<std::uint32_t>(
        range.end.line, static_cast<std::uint32_t>(lines.size()) - 1);
    const std::string indent = DetectIndent(lines[lo]);
    for (std::uint32_t li = lo; li <= hi; ++li) {
      std::string s = lines[li];
      // Strip the shared indent prefix so the extracted body starts at
      // column 0 inside the new FUNC.
      if (s.compare(0, indent.size(), indent) == 0) {
        s.erase(0, indent.size());
      }
      body += s;
      body.push_back('\n');
    }
    lsp::CodeAction ca;
    ca.title = "Extract to FUNC " + new_function_name;
    ca.kind = "refactor.extract.function";
    // 1) Replace the selection with a call.
    lsp::TextEdit replace;
    replace.range = range;
    replace.new_text = indent + new_function_name + "();\n";
    // 2) Insert the new FUNC at the very top of the file.  Inserting
    //    at (0,0) keeps the edit independent of the replacement above.
    lsp::TextEdit insert;
    insert.range.start.line = 0;
    insert.range.start.character = 0;
    insert.range.end.line = 0;
    insert.range.end.character = 0;
    insert.new_text = BuildExtractedFunction(new_function_name, body);
    ca.edit.changes[uri] = {insert, replace};
    actions.push_back(std::move(ca));
  }

  // ── Inline variable ───────────────────────────────────────────────────
  // Detects `LET name = <rhs>;` on the start line of @p range and
  // returns a CodeAction describing the inline.  The actual edit only
  // rewrites the binding line itself (replacing it with a comment) so
  // the user can decide whether to accept; richer rewrites are deferred
  // to the editor wizard surface.
  if (is_ploy && range.start.line < lines.size()) {
    const std::string &ln = lines[range.start.line];
    auto pos = ln.find("LET ");
    if (pos != std::string::npos) {
      lsp::CodeAction ca;
      ca.title = "Inline LET binding";
      ca.kind = "refactor.inline.variable";
      lsp::TextEdit te;
      te.range.start.line = range.start.line;
      te.range.start.character = 0;
      te.range.end.line = range.start.line;
      te.range.end.character = static_cast<std::uint32_t>(ln.size());
      te.new_text = "// inlined: " + ln;
      ca.edit.changes[uri] = {te};
      actions.push_back(std::move(ca));
    }
  }

  // ── Inline function ───────────────────────────────────────────────────
  // Surfaced as an informational lightbulb entry; the editor wizard
  // negotiates the call-site rewrite because cross-file inlining
  // requires user disambiguation when overloads collide.
  {
    lsp::CodeAction ca;
    ca.title = "Inline FUNC at cursor";
    ca.kind = "refactor.inline.function";
    actions.push_back(std::move(ca));
  }

  // ── Change signature ──────────────────────────────────────────────────
  {
    lsp::CodeAction ca;
    ca.title = "Change signature…";
    ca.kind = "refactor.changeSignature";
    actions.push_back(std::move(ca));
  }

  // ── Move file ─────────────────────────────────────────────────────────
  {
    lsp::CodeAction ca;
    ca.title = "Move file…";
    ca.kind = "refactor.move.file";
    actions.push_back(std::move(ca));
  }

  return actions;
}

}  // namespace polyglot::polyls
