/**
 * @file     polyls_features.cpp
 * @brief    Language feature handlers for polyls (completion, hover,
 *           signature help, completionItem/resolve)
 *
 * Implements demand 2026-04-28-21 §4 — `textDocument/completion`,
 * `completionItem/resolve`, `textDocument/hover` and
 * `textDocument/signatureHelp` for `.ploy` documents.  The
 * implementation is self-contained and does not depend on any Qt
 * widgets so the polyls binary remains a headless console process.
 *
 * Algorithms are deliberately lightweight:
 *
 *   • Keywords: hard-coded list of `.ploy` keywords with snippet
 *     templates aligned with the IDE's existing editor table.
 *   • Document symbols: a single regex-free pass over the source
 *     extracts FUNC / PIPELINE / LET / VAR / STRUCT / IMPORT entries
 *     with their position and (for FUNC) the raw parameter list and
 *     return type, so we can answer hover and signatureHelp without a
 *     full parse tree.
 *   • Cross-language `LINK` completion: when the cursor sits in a
 *     `LINK <lang>::` context we emit one CompletionItem per known
 *     language id so the user can pick an interop target without
 *     remembering the exact name.
 *
 * @ingroup  Tool / polyls
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "tools/polyls/polyls_core/polyls_server.h"
#include "tools/ui/common/lsp/lsp_message.h"
#include "tools/ui/common/editing/format_engine.h"

namespace polyglot::polyls {

namespace lsp = polyglot::tools::ui::lsp;

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

constexpr int kInvalidParams = -32602;

/// Split @p text into 0-based lines preserving empties.
std::vector<std::string_view> SplitLines(std::string_view text) {
  std::vector<std::string_view> lines;
  std::size_t start = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\n') {
      std::size_t end = i;
      if (end > start && text[end - 1] == '\r') --end;
      lines.emplace_back(text.data() + start, end - start);
      start = i + 1;
    }
  }
  lines.emplace_back(text.data() + start, text.size() - start);
  return lines;
}

bool IsIdentChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

/// Extract the identifier sitting at @p column on @p line (0-based);
/// the empty string is returned when no identifier covers the position.
std::string IdentifierAt(std::string_view line, std::size_t column) {
  if (column > line.size()) column = line.size();
  std::size_t left = column;
  while (left > 0 && IsIdentChar(line[left - 1])) --left;
  std::size_t right = column;
  while (right < line.size() && IsIdentChar(line[right])) ++right;
  if (left == right) return {};
  return std::string(line.substr(left, right - left));
}

/// Identifier prefix immediately to the left of @p column (no extension
/// to the right).  Used by `textDocument/completion`.
std::string PrefixAt(std::string_view line, std::size_t column) {
  if (column > line.size()) column = line.size();
  std::size_t left = column;
  while (left > 0 && IsIdentChar(line[left - 1])) --left;
  return std::string(line.substr(left, column - left));
}

/// True when the prefix immediately preceding @p column ends with the
/// pattern `LINK <ident>::`, in which case we want cross-language member
/// completions instead of generic identifiers.
bool IsLinkLanguageContext(std::string_view line, std::size_t column,
                           std::string &out_lang) {
  if (column > line.size()) column = line.size();
  // Walk left over a possible identifier prefix already typed.
  std::size_t left = column;
  while (left > 0 && IsIdentChar(line[left - 1])) --left;
  // Expect "::" before that.
  if (left < 2 || line[left - 1] != ':' || line[left - 2] != ':') return false;
  std::size_t end = left - 2;
  // Identifier (language id) before "::".
  std::size_t lang_end = end;
  while (lang_end > 0 && IsIdentChar(line[lang_end - 1])) --lang_end;
  if (lang_end == end) return false;
  std::string_view lang = line.substr(lang_end, end - lang_end);
  // Skip whitespace, then expect "LINK" keyword.
  std::size_t k = lang_end;
  while (k > 0 && (line[k - 1] == ' ' || line[k - 1] == '\t')) --k;
  static constexpr std::string_view kLink = "LINK";
  if (k < kLink.size()) return false;
  std::string_view kw = line.substr(k - kLink.size(), kLink.size());
  std::string upper(kw);
  std::transform(upper.begin(), upper.end(), upper.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  if (upper != kLink) return false;
  out_lang.assign(lang.data(), lang.size());
  return true;
}

/// Document symbol parsed from a `.ploy` source.  Positions are 0-based
/// to match the LSP wire format.
struct PloySymbol {
  std::string name;
  std::string kind;        ///< "func" | "pipeline" | "let" | "var" | "struct" | "import"
  std::uint32_t line{0};
  std::uint32_t character{0};
  std::string params;      ///< Raw "(a, b)" text for FUNC.
  std::string return_type; ///< Raw return type after "->" for FUNC.
};

std::vector<PloySymbol> CollectDocumentSymbols(std::string_view text) {
  std::vector<PloySymbol> out;
  auto lines = SplitLines(text);
  for (std::uint32_t ln = 0; ln < lines.size(); ++ln) {
    std::string_view line = lines[ln];
    // Skip leading whitespace.
    std::size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i == line.size()) continue;
    // Skip comment lines.
    if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') continue;

    auto starts_with = [&](std::string_view kw) {
      if (line.size() - i < kw.size()) return false;
      if (line.substr(i, kw.size()) != kw) return false;
      // Must be followed by space, '(' or end-of-line.
      std::size_t e = i + kw.size();
      return e == line.size() || std::isspace(static_cast<unsigned char>(line[e])) ||
             line[e] == '(' || line[e] == '{';
    };

    auto read_ident = [&](std::size_t &cursor) -> std::string {
      while (cursor < line.size() &&
             std::isspace(static_cast<unsigned char>(line[cursor])))
        ++cursor;
      std::size_t start = cursor;
      while (cursor < line.size() && IsIdentChar(line[cursor])) ++cursor;
      return std::string(line.substr(start, cursor - start));
    };

    if (starts_with("FUNC")) {
      std::size_t c = i + 4;
      PloySymbol sym;
      sym.kind = "func";
      sym.line = ln;
      const std::size_t name_start_skip = c;
      sym.name = read_ident(c);
      if (sym.name.empty()) continue;
      sym.character = static_cast<std::uint32_t>(name_start_skip + 1);
      // Capture parameter list "(...)".
      while (c < line.size() && line[c] != '(') ++c;
      if (c < line.size() && line[c] == '(') {
        std::size_t end = c + 1;
        int depth = 1;
        while (end < line.size() && depth > 0) {
          if (line[end] == '(') ++depth;
          else if (line[end] == ')') --depth;
          if (depth > 0) ++end;
        }
        sym.params.assign(line.data() + c, (end < line.size() ? end + 1 : line.size()) - c);
        c = (end < line.size()) ? end + 1 : line.size();
      }
      // Optional "-> type".
      while (c + 1 < line.size() && (line[c] == ' ' || line[c] == '\t')) ++c;
      if (c + 1 < line.size() && line[c] == '-' && line[c + 1] == '>') {
        c += 2;
        while (c < line.size() && (line[c] == ' ' || line[c] == '\t')) ++c;
        std::size_t rt_start = c;
        while (c < line.size() && (IsIdentChar(line[c]) || line[c] == ':' ||
                                   line[c] == '<' || line[c] == '>' || line[c] == ',' ||
                                   line[c] == ' '))
          ++c;
        sym.return_type.assign(line.data() + rt_start, c - rt_start);
        // Trim trailing space.
        while (!sym.return_type.empty() && sym.return_type.back() == ' ')
          sym.return_type.pop_back();
      }
      out.push_back(std::move(sym));
      continue;
    }
    if (starts_with("PIPELINE")) {
      std::size_t c = i + 8;
      PloySymbol sym;
      sym.kind = "pipeline";
      sym.line = ln;
      sym.name = read_ident(c);
      if (!sym.name.empty()) out.push_back(std::move(sym));
      continue;
    }
    if (starts_with("STRUCT")) {
      std::size_t c = i + 6;
      PloySymbol sym;
      sym.kind = "struct";
      sym.line = ln;
      sym.name = read_ident(c);
      if (!sym.name.empty()) out.push_back(std::move(sym));
      continue;
    }
    if (starts_with("LET") || starts_with("VAR")) {
      const bool is_let = starts_with("LET");
      std::size_t c = i + 3;
      PloySymbol sym;
      sym.kind = is_let ? "let" : "var";
      sym.line = ln;
      sym.name = read_ident(c);
      if (!sym.name.empty()) out.push_back(std::move(sym));
      continue;
    }
    if (starts_with("IMPORT")) {
      // IMPORT <lang> PACKAGE <pkg> ;
      std::size_t c = i + 6;
      const std::string lang = read_ident(c);
      // Skip "PACKAGE".
      while (c < line.size() && std::isspace(static_cast<unsigned char>(line[c])))
        ++c;
      if (line.compare(c, 7, "PACKAGE") == 0) c += 7;
      PloySymbol sym;
      sym.kind = "import";
      sym.line = ln;
      sym.name = read_ident(c);
      sym.return_type = lang;  // Re-purpose to carry the language id.
      if (!sym.name.empty()) out.push_back(std::move(sym));
      continue;
    }
  }
  return out;
}

/// Hard-coded keyword table for `.ploy`.  Keep in sync with
/// CompilerService::GetPloyCompletions() so the in-process editor
/// completion list and the LSP one offer the same coverage.
struct PloyKeyword {
  const char *label;
  const char *detail;
  const char *insert_text;
};

const std::vector<PloyKeyword> &PloyKeywordTable() {
  static const std::vector<PloyKeyword> kTable = {
      {"LINK", "Link external function",
       "LINK ${1:lang}::${2:module}::${3:func} AS FUNC(${4:params}) -> ${5:ret};"},
      {"IMPORT", "Import package",
       "IMPORT ${1:lang} PACKAGE ${2:package};"},
      {"EXPORT", "Export function", "EXPORT ${1:name} AS ${2:lang}::${3:func};"},
      {"FUNC", "Define function",
       "FUNC ${1:name}(${2:params}) -> ${3:return_type} {\n    ${4}\n}"},
      {"LET", "Declare immutable variable", "LET ${1:name} = ${2:value};"},
      {"VAR", "Declare mutable variable", "VAR ${1:name} = ${2:value};"},
      {"PIPELINE", "Define pipeline", "PIPELINE ${1:name} {\n    ${2}\n}"},
      {"STRUCT", "Define struct", "STRUCT ${1:name} {\n    ${2:fields}\n}"},
      {"IF", "If statement", "IF (${1:condition}) {\n    ${2}\n}"},
      {"WHILE", "While loop", "WHILE (${1:condition}) {\n    ${2}\n}"},
      {"FOR", "For loop", "FOR ${1:var} IN ${2:iterable} {\n    ${3}\n}"},
      {"MATCH", "Match expression",
       "MATCH ${1:value} {\n    CASE ${2:pattern}: ${3}\n    DEFAULT: ${4}\n}"},
      {"RETURN", "Return from function", "RETURN ${1:value};"},
      {"NEW", "Instantiate class", "NEW(${1:lang}, ${2:class}, ${3:args})"},
      {"METHOD", "Call method on object",
       "METHOD(${1:lang}, ${2:obj}, ${3:method}, ${4:args})"},
      {"GET", "Get attribute", "GET(${1:lang}, ${2:obj}, ${3:attr})"},
      {"SET", "Set attribute", "SET(${1:lang}, ${2:obj}, ${3:attr}, ${4:value})"},
      {"WITH", "Resource block",
       "WITH ${1:lang}, ${2:resource} {\n    ${3}\n}"},
      {"DELETE", "Delete object", "DELETE(${1:lang}, ${2:obj});"},
      {"CALL", "Call external function",
       "CALL(${1:lang}, ${2:module}::${3:func}, ${4:args})"},
      {"CONFIG", "Configuration block", "CONFIG {\n    ${1}\n}"},
      {"CONVERT", "Type conversion", "CONVERT(${1:value}, ${2:target_type})"},
      {"EXTEND", "Extend class",
       "EXTEND ${1:lang}::${2:class} {\n    ${3}\n}"},
      {"INFER", "Inferred type annotation", "INFER"},
      {"TRUE", "Boolean true", "TRUE"},
      {"FALSE", "Boolean false", "FALSE"},
      {"NULL", "Null value", "NULL"},
  };
  return kTable;
}

/// Map our string kind to the LSP CompletionItemKind enum (numeric).
int LspKindForSymbolKind(const std::string &k) {
  if (k == "func") return static_cast<int>(lsp::CompletionItemKind::kFunction);
  if (k == "pipeline") return static_cast<int>(lsp::CompletionItemKind::kFunction);
  if (k == "struct") return static_cast<int>(lsp::CompletionItemKind::kStruct);
  if (k == "let" || k == "var")
    return static_cast<int>(lsp::CompletionItemKind::kVariable);
  if (k == "import") return static_cast<int>(lsp::CompletionItemKind::kModule);
  return static_cast<int>(lsp::CompletionItemKind::kText);
}

/// Build a CompletionItem JSON object.  We emit the LSP-native shape
/// directly (camelCase keys) because `lsp::CompletionItem` does not
/// include every field we want to expose (kind enum, sort order).
Json MakeCompletionItem(const std::string &label, int kind,
                        const std::string &detail,
                        const std::string &documentation,
                        const std::string &insert_text,
                        bool is_snippet) {
  Json j = {
      {"label", label},
      {"kind", kind},
      {"detail", detail},
      {"documentation", documentation},
      {"insertText", insert_text},
      // 1 = PlainText, 2 = Snippet
      {"insertTextFormat", is_snippet ? 2 : 1},
  };
  return j;
}

}  // namespace

// ---------------------------------------------------------------------------
// textDocument/completion
// ---------------------------------------------------------------------------

void PolylsServer::HandleCompletion(int id, const Json &params) {
  if (!params.is_object() || !params.contains("textDocument") ||
      !params.contains("position")) {
    SendError(id, kInvalidParams, "completion: missing textDocument/position");
    return;
  }
  const std::string uri =
      params["textDocument"].value("uri", std::string{});
  const auto &pos = params["position"];
  const std::uint32_t line_no = pos.value("line", 0u);
  const std::uint32_t character = pos.value("character", 0u);

  std::string text;
  std::string language_id;
  {
    std::lock_guard<std::mutex> lock(docs_mu_);
    auto it = documents_.find(uri);
    if (it == documents_.end()) {
      SendResponse(id, Json::array());
      return;
    }
    text = it->second.text;
    language_id = it->second.language_id;
  }

  // Only `.ploy` is owned by polyls.  Foreign language ids return an
  // empty list so the editor's local completer takes over.
  if (language_id != "ploy" && language_id != "poly") {
    SendResponse(id, Json::array());
    return;
  }

  auto lines = SplitLines(text);
  std::string current_line;
  if (line_no < lines.size()) current_line.assign(lines[line_no]);
  const std::string prefix = PrefixAt(current_line, character);

  std::string link_lang;
  const bool link_ctx =
      IsLinkLanguageContext(current_line, character, link_lang);

  Json items = Json::array();
  std::unordered_set<std::string> emitted;

  // 1) Cross-language LINK members — when the cursor is inside a
  //    `LINK <lang>::` context we suggest a generic shape; full
  //    cross-language indexing belongs to demand 2026-04-28-22 and is
  //    surfaced here as a snippet placeholder.
  if (link_ctx) {
    const std::string snippet =
        "${1:module}::${2:func} AS FUNC(${3:params}) -> ${4:return_type};";
    items.push_back(MakeCompletionItem(
        "<" + link_lang + " symbol>",
        static_cast<int>(lsp::CompletionItemKind::kModule),
        "Cross-language LINK target (" + link_lang + ")",
        "Insert a `LINK " + link_lang + "::module::func AS ...` template.",
        snippet, /*is_snippet=*/true));
    SendResponse(id, items);
    return;
  }

  auto label_matches = [&](const std::string &label) {
    if (prefix.empty()) return true;
    if (label.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(label[i])) !=
          std::tolower(static_cast<unsigned char>(prefix[i])))
        return false;
    }
    return true;
  };

  // 2) Keywords + snippets.
  for (const auto &kw : PloyKeywordTable()) {
    if (!label_matches(kw.label)) continue;
    if (!emitted.insert(kw.label).second) continue;
    const bool snippet =
        std::string_view{kw.insert_text}.find("${") != std::string_view::npos;
    items.push_back(MakeCompletionItem(
        kw.label, static_cast<int>(lsp::CompletionItemKind::kKeyword),
        kw.detail, kw.detail, kw.insert_text, snippet));
  }

  // 3) User-declared symbols from the current document.
  for (const auto &sym : CollectDocumentSymbols(text)) {
    if (!label_matches(sym.name)) continue;
    if (!emitted.insert(sym.name).second) continue;
    std::string detail = sym.kind;
    if (sym.kind == "func") {
      detail = "FUNC " + sym.name + sym.params;
      if (!sym.return_type.empty()) detail += " -> " + sym.return_type;
    } else if (sym.kind == "import") {
      detail = "IMPORT " + sym.return_type + " PACKAGE " + sym.name;
    }
    items.push_back(MakeCompletionItem(
        sym.name, LspKindForSymbolKind(sym.kind), detail, detail, sym.name,
        /*is_snippet=*/false));
  }

  SendResponse(id, items);
}

// ---------------------------------------------------------------------------
// completionItem/resolve
// ---------------------------------------------------------------------------
//
// The minimum LSP contract is to echo the item back; clients use this
// hook to lazy-load expensive `documentation` blobs.  Our completions
// already carry the full text, so we simply pass the JSON through —
// preserving any extra fields the client may have attached.
//
void PolylsServer::HandleCompletionResolve(int id, const Json &params) {
  if (!params.is_object()) {
    SendError(id, kInvalidParams, "completionItem/resolve: object expected");
    return;
  }
  SendResponse(id, params);
}

// ---------------------------------------------------------------------------
// textDocument/hover
// ---------------------------------------------------------------------------

void PolylsServer::HandleHover(int id, const Json &params) {
  if (!params.is_object() || !params.contains("textDocument") ||
      !params.contains("position")) {
    SendError(id, kInvalidParams, "hover: missing textDocument/position");
    return;
  }
  const std::string uri =
      params["textDocument"].value("uri", std::string{});
  const auto &pos = params["position"];
  const std::uint32_t line_no = pos.value("line", 0u);
  const std::uint32_t character = pos.value("character", 0u);

  std::string text;
  {
    std::lock_guard<std::mutex> lock(docs_mu_);
    auto it = documents_.find(uri);
    if (it == documents_.end()) {
      SendResponse(id, Json());
      return;
    }
    text = it->second.text;
  }

  auto lines = SplitLines(text);
  if (line_no >= lines.size()) {
    SendResponse(id, Json());
    return;
  }
  const std::string ident = IdentifierAt(lines[line_no], character);
  if (ident.empty()) {
    SendResponse(id, Json());
    return;
  }

  std::string markdown;

  // Look up a user-declared symbol with this name.
  for (const auto &sym : CollectDocumentSymbols(text)) {
    if (sym.name != ident) continue;
    if (sym.kind == "func") {
      markdown = "```ploy\nFUNC " + sym.name + sym.params;
      if (!sym.return_type.empty()) markdown += " -> " + sym.return_type;
      markdown += "\n```\n\n*User-defined function.*";
    } else if (sym.kind == "pipeline") {
      markdown = "```ploy\nPIPELINE " + sym.name + "\n```\n\n*User-defined pipeline.*";
    } else if (sym.kind == "struct") {
      markdown = "```ploy\nSTRUCT " + sym.name + "\n```\n\n*User-defined struct.*";
    } else if (sym.kind == "let" || sym.kind == "var") {
      markdown = "```ploy\n" + std::string(sym.kind == "let" ? "LET " : "VAR ") +
                 sym.name + "\n```\n\n*Local variable.*";
    } else if (sym.kind == "import") {
      markdown = "```ploy\nIMPORT " + sym.return_type + " PACKAGE " + sym.name +
                 "\n```\n\n*Imported package — cross-language symbols are " +
                 "available through `" + sym.return_type + "::" + sym.name + "::…`*";
    }
    break;
  }

  // Fall back to keyword documentation.
  if (markdown.empty()) {
    for (const auto &kw : PloyKeywordTable()) {
      if (ident == kw.label) {
        markdown = "**" + std::string(kw.label) + "** — " + kw.detail;
        break;
      }
    }
  }

  if (markdown.empty()) {
    SendResponse(id, Json());
    return;
  }
  Json hover = {
      {"contents", {{"kind", "markdown"}, {"value", markdown}}},
  };
  SendResponse(id, hover);
}

// ---------------------------------------------------------------------------
// textDocument/signatureHelp
// ---------------------------------------------------------------------------

void PolylsServer::HandleSignatureHelp(int id, const Json &params) {
  if (!params.is_object() || !params.contains("textDocument") ||
      !params.contains("position")) {
    SendError(id, kInvalidParams,
              "signatureHelp: missing textDocument/position");
    return;
  }
  const std::string uri =
      params["textDocument"].value("uri", std::string{});
  const auto &pos = params["position"];
  const std::uint32_t line_no = pos.value("line", 0u);
  const std::uint32_t character = pos.value("character", 0u);

  std::string text;
  {
    std::lock_guard<std::mutex> lock(docs_mu_);
    auto it = documents_.find(uri);
    if (it == documents_.end()) {
      SendResponse(id, Json());
      return;
    }
    text = it->second.text;
  }

  auto lines = SplitLines(text);
  if (line_no >= lines.size()) {
    SendResponse(id, Json());
    return;
  }
  const std::string &line = std::string(lines[line_no]);
  // Walk left to find the unmatched '(' that opens the active call,
  // counting commas at the top depth to identify the active parameter.
  std::size_t cursor = std::min<std::size_t>(character, line.size());
  int depth = 0;
  std::size_t open_paren = std::string::npos;
  int active_param = 0;
  for (std::size_t i = cursor; i > 0; --i) {
    char c = line[i - 1];
    if (c == ')') ++depth;
    else if (c == '(') {
      if (depth == 0) {
        open_paren = i - 1;
        break;
      }
      --depth;
    } else if (c == ',' && depth == 0) {
      ++active_param;
    }
  }
  if (open_paren == std::string::npos) {
    SendResponse(id, Json());
    return;
  }

  // Identifier directly to the left of '('.
  std::size_t left = open_paren;
  while (left > 0 && (line[left - 1] == ' ' || line[left - 1] == '\t')) --left;
  std::size_t name_end = left;
  while (left > 0 && IsIdentChar(line[left - 1])) --left;
  if (left == name_end) {
    SendResponse(id, Json());
    return;
  }
  const std::string callee = line.substr(left, name_end - left);

  // Look up a FUNC symbol with this name.
  for (const auto &sym : CollectDocumentSymbols(text)) {
    if (sym.kind != "func" || sym.name != callee) continue;

    // Build SignatureInformation.  Parameters are split by top-level commas
    // inside sym.params (which still includes the surrounding parentheses).
    std::vector<std::string> param_labels;
    if (sym.params.size() >= 2) {
      const std::string inside = sym.params.substr(1, sym.params.size() - 2);
      std::size_t i = 0;
      int p_depth = 0;
      std::size_t start = 0;
      auto push = [&](std::size_t end) {
        std::string p = inside.substr(start, end - start);
        // Trim.
        while (!p.empty() && (p.front() == ' ' || p.front() == '\t')) p.erase(p.begin());
        while (!p.empty() && (p.back() == ' ' || p.back() == '\t')) p.pop_back();
        if (!p.empty()) param_labels.push_back(p);
      };
      for (; i < inside.size(); ++i) {
        char c = inside[i];
        if (c == '(' || c == '<' || c == '[') ++p_depth;
        else if (c == ')' || c == '>' || c == ']') --p_depth;
        else if (c == ',' && p_depth == 0) {
          push(i);
          start = i + 1;
        }
      }
      push(inside.size());
    }

    Json parameters = Json::array();
    for (const auto &p : param_labels) {
      parameters.push_back(Json{{"label", p}, {"documentation", ""}});
    }
    std::string sig_label = "FUNC " + sym.name + sym.params;
    if (!sym.return_type.empty()) sig_label += " -> " + sym.return_type;

    Json signatures = Json::array();
    signatures.push_back(Json{
        {"label", sig_label},
        {"documentation", "User-defined function in current document."},
        {"parameters", parameters},
    });

    const int clamped =
        param_labels.empty()
            ? 0
            : std::min<int>(active_param, static_cast<int>(param_labels.size()) - 1);
    Json help = {
        {"signatures", signatures},
        {"activeSignature", 0},
        {"activeParameter", clamped},
    };
    SendResponse(id, help);
    return;
  }
  SendResponse(id, Json());
}

// ---------------------------------------------------------------------------
// Document & workspace symbols (demand 2026-04-28-25 §3)
// ---------------------------------------------------------------------------

namespace {

int LspSymbolKindForPloy(const std::string &k) {
  // Mapping aligned with lsp::SymbolKind (numeric wire values).
  if (k == "func" || k == "pipeline") return 12;   // Function
  if (k == "struct") return 23;                    // Struct
  if (k == "let" || k == "var") return 13;         // Variable
  if (k == "import") return 2;                     // Module
  return 13;
}

Json BuildSymbolRange(const PloySymbol &s) {
  // Single-line span — we only know the start.  Editors that require
  // an exact range fall back to selection_range identity.
  Json pos{{"line", s.line}, {"character", s.character}};
  return Json{{"start", pos}, {"end", pos}};
}

}  // namespace

void PolylsServer::HandleDocumentSymbol(int id, const Json &params) {
  if (!params.is_object() || !params.contains("textDocument")) {
    SendError(id, kInvalidParams, "documentSymbol: missing textDocument");
    return;
  }
  const std::string uri =
      params["textDocument"].value("uri", std::string{});
  std::string text;
  std::string language_id;
  {
    std::lock_guard<std::mutex> lock(docs_mu_);
    auto it = documents_.find(uri);
    if (it == documents_.end()) {
      SendResponse(id, Json::array());
      return;
    }
    text = it->second.text;
    language_id = it->second.language_id;
  }
  if (language_id != "ploy" && language_id != "poly") {
    // Foreign languages: foster the editor's own outline by returning
    // an empty array (LSP spec permits this).
    SendResponse(id, Json::array());
    return;
  }
  Json arr = Json::array();
  for (const auto &sym : CollectDocumentSymbols(text)) {
    Json node = {
        {"name", sym.name},
        {"detail", sym.params},
        {"kind", LspSymbolKindForPloy(sym.kind)},
        {"range", BuildSymbolRange(sym)},
        {"selectionRange", BuildSymbolRange(sym)},
        {"children", Json::array()},
    };
    arr.push_back(std::move(node));
  }
  SendResponse(id, arr);
}

void PolylsServer::HandleWorkspaceSymbol(int id, const Json &params) {
  std::string query;
  if (params.is_object() && params.contains("query") &&
      params["query"].is_string()) {
    query = params["query"].get<std::string>();
  }
  // Lowercase the needle once for case-insensitive substring matching.
  std::string lneedle;
  lneedle.reserve(query.size());
  for (char c : query) lneedle.push_back(
      static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

  Json arr = Json::array();
  for (const auto &e : index_->Entries()) {
    if (!lneedle.empty()) {
      std::string lname;
      lname.reserve(e.name.size());
      for (char c : e.name) lname.push_back(
          static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
      if (lname.find(lneedle) == std::string::npos) continue;
    }
    int kind = 13;  // Variable default
    switch (e.kind) {
      case IndexEntryKind::kFunction:
      case IndexEntryKind::kForeignFunction:
      case IndexEntryKind::kPipeline:
        kind = 12; break;
      case IndexEntryKind::kStruct:
      case IndexEntryKind::kForeignClass:
        kind = 23; break;
      case IndexEntryKind::kImport:
      case IndexEntryKind::kLink:
        kind = 2; break;
      case IndexEntryKind::kVariable:
        kind = 13; break;
    }
    Json start{{"line", e.definition.line},
               {"character", e.definition.character}};
    Json end{{"line", e.definition.end_line},
             {"character", e.definition.end_character}};
    Json info = {
        {"name", e.name},
        {"kind", kind},
        {"location", Json{
            {"uri", e.definition.uri},
            {"range", Json{{"start", start}, {"end", end}}},
        }},
        {"containerName", e.qualified_name},
    };
    arr.push_back(std::move(info));
  }
  SendResponse(id, arr);
}

namespace {

// Counts the number of lines in `text` (1 + newline count).
std::uint32_t CountLines(const std::string &text) {
  std::uint32_t n = 1;
  for (char c : text) if (c == '\n') ++n;
  return n;
}

// Builds a single full-document TextEdit replacing `original` with
// `formatted`.  Returns an empty array when no change is needed.
Json BuildFullDocumentEdit(const std::string &original,
                          const std::string &formatted) {
  if (original == formatted) return Json::array();
  std::uint32_t end_line = CountLines(original);
  Json edit = {
      {"range", {
          {"start", {{"line", 0}, {"character", 0}}},
          {"end",   {{"line", end_line}, {"character", 0}}},
      }},
      {"newText", formatted},
  };
  return Json::array({edit});
}

polyglot::tools::ui::FormatOptions OptionsFromJson(const Json &params) {
  polyglot::tools::ui::FormatOptions o;
  if (params.is_object() && params.contains("options") &&
      params["options"].is_object()) {
    const Json &j = params["options"];
    if (j.contains("tabSize") && j["tabSize"].is_number_unsigned()) {
      o.tab_size = j["tabSize"].get<std::uint32_t>();
    }
    if (j.contains("insertSpaces") && j["insertSpaces"].is_boolean()) {
      o.insert_spaces = j["insertSpaces"].get<bool>();
    }
    if (j.contains("trimTrailingWhitespace") &&
        j["trimTrailingWhitespace"].is_boolean()) {
      o.trim_trailing_whitespace = j["trimTrailingWhitespace"].get<bool>();
    }
    if (j.contains("insertFinalNewline") &&
        j["insertFinalNewline"].is_boolean()) {
      o.insert_final_newline = j["insertFinalNewline"].get<bool>();
    }
  }
  return o;
}

}  // namespace

void PolylsServer::HandleFormatting(int id, const Json &params) {
  if (!params.is_object() || !params.contains("textDocument")) {
    SendError(id, kInvalidParams, "formatting: missing textDocument");
    return;
  }
  const std::string uri = params["textDocument"].value("uri", std::string{});
  std::string text;
  std::string language_id;
  {
    std::lock_guard<std::mutex> lock(docs_mu_);
    auto it = documents_.find(uri);
    if (it == documents_.end()) { SendResponse(id, Json::array()); return; }
    text = it->second.text;
    language_id = it->second.language_id;
  }
  if (language_id != "ploy" && language_id != "poly") {
    // Foreign languages would route through their own LSP — we
    // return an empty edit list so the editor falls back gracefully.
    SendResponse(id, Json::array());
    return;
  }
  auto opts = OptionsFromJson(params);
  std::string formatted = polyglot::tools::ui::FormatPloy(text, opts);
  SendResponse(id, BuildFullDocumentEdit(text, formatted));
}

void PolylsServer::HandleRangeFormatting(int id, const Json &params) {
  // For Ploy we re-format the whole document (range formatting falls
  // back to full formatting because re-indenting a sub-range without
  // its enclosing brace context produces unstable results).
  HandleFormatting(id, params);
}

void PolylsServer::HandleOnTypeFormatting(int id, const Json &params) {
  HandleFormatting(id, params);
}

}  // namespace polyglot::polyls
