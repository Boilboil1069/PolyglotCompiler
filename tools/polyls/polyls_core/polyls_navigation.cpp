/**
 * @file     polyls_navigation.cpp
 * @brief    Navigation feature handlers for polyls
 *
 * Implements `textDocument/definition`, `textDocument/declaration`,
 * `textDocument/implementation`, `textDocument/typeDefinition` and
 * `textDocument/references` on top of @ref SymbolIndex.
 *
 * The handlers share a uniform `(uri, position) → identifier` lookup
 * step that locates the token under the cursor in the open document.
 * Cross-language navigation goes through @ref SymbolIndex helpers
 * (`CrossLanguageTarget` / `CrossLanguageBackrefs`) so that a `.ploy`
 * `LINK` site can hop to its host-language target and host-language
 * symbols can reverse-list the `.ploy` LINK sites that import them.
 *
 * @ingroup  Tool / polyls
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include "tools/polyls/polyls_core/polyls_server.h"
#include "tools/polyls/polyls_core/symbol_index.h"
#include "tools/ui/common/lsp/lsp_message.h"

namespace polyglot::polyls {

namespace lsp = polyglot::tools::ui::lsp;

namespace {

constexpr int kInvalidParams = -32602;

bool IsIdentChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
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

/// Result of token resolution under the cursor.  When the token sits
/// inside an `lang::module::func` qualifier we record the language
/// prefix and the qualifier suffix so cross-language jumps can target
/// the right host file.
struct TokenAtCursor {
  std::string bare;          ///< Last identifier component.
  std::string qualified;     ///< Full `a::b::c` text or just bare.
  std::string language;      ///< Lang prefix when qualifier starts with one.
};

TokenAtCursor ResolveToken(std::string_view line, std::size_t column) {
  TokenAtCursor t;
  if (column > line.size()) column = line.size();

  // Identifier under cursor.
  std::size_t left = column;
  while (left > 0 && IsIdentChar(line[left - 1])) --left;
  std::size_t right = column;
  while (right < line.size() && IsIdentChar(line[right])) ++right;
  if (left == right) return t;
  t.bare.assign(line.substr(left, right - left));

  // Walk left over preceding "ident::" segments.
  std::size_t qstart = left;
  while (qstart >= 2 && line[qstart - 1] == ':' && line[qstart - 2] == ':') {
    std::size_t e = qstart - 2;
    std::size_t s = e;
    while (s > 0 && IsIdentChar(line[s - 1])) --s;
    if (s == e) break;
    qstart = s;
  }

  // Walk right over trailing "::ident" segments.
  std::size_t qend = right;
  while (qend + 2 <= line.size() && line[qend] == ':' && line[qend + 1] == ':') {
    std::size_t s = qend + 2;
    std::size_t e = s;
    while (e < line.size() && IsIdentChar(line[e])) ++e;
    if (s == e) break;
    qend = e;
  }
  t.qualified.assign(line.substr(qstart, qend - qstart));

  const std::size_t sep = t.qualified.find("::");
  if (sep != std::string::npos) {
    const std::string head = t.qualified.substr(0, sep);
    static const char *kKnownLangs[] = {"cpp",  "python", "rust",
                                        "java", "dotnet", "csharp"};
    for (const char *k : kKnownLangs) {
      if (head == k) {
        t.language = head;
        break;
      }
    }
  }
  return t;
}

Json MakeLspLocation(const SymbolLocation &loc) {
  return Json{{"uri", loc.uri},
              {"range",
               {{"start", {{"line", loc.line}, {"character", loc.character}}},
                {"end",
                 {{"line", loc.end_line},
                  {"character", loc.end_character}}}}}};
}

Json LocationsToJson(const std::vector<SymbolLocation> &v) {
  Json arr = Json::array();
  for (const auto &loc : v) arr.push_back(MakeLspLocation(loc));
  return arr;
}

/// Pull (uri, line, column, current document text, language id) from
/// the LSP request envelope; returns false on malformed input.
bool ExtractRequest(const Json &params, std::string &uri,
                    std::uint32_t &line, std::uint32_t &character) {
  if (!params.is_object() || !params.contains("textDocument") ||
      !params.contains("position")) {
    return false;
  }
  uri = params["textDocument"].value("uri", std::string{});
  if (uri.empty()) return false;
  const auto &pos = params["position"];
  line = pos.value("line", 0u);
  character = pos.value("character", 0u);
  return true;
}

/// Common pre-amble shared by the five navigation handlers.  Returns
/// the resolved token, or an empty `bare` when the cursor sits in
/// whitespace / a comment.
struct ResolvedRequest {
  std::string uri;
  std::uint32_t line{0};
  std::uint32_t character{0};
  std::string text;
  std::string language_id;
  TokenAtCursor token;
  bool ok{false};
};

ResolvedRequest Resolve(PolylsServer &srv, const Json &params) {
  ResolvedRequest r;
  if (!ExtractRequest(params, r.uri, r.line, r.character)) return r;
  for (const auto &doc : srv.SnapshotDocuments()) {
    if (doc.uri == r.uri) {
      r.text = doc.text;
      r.language_id = doc.language_id;
      break;
    }
  }
  if (r.text.empty()) {
    // Document not opened; we may still answer from the persisted
    // index, but without text we cannot resolve the token under the
    // cursor.  Return an empty bare so callers send back an empty list.
    r.ok = true;
    return r;
  }
  const auto lines = SplitLines(r.text);
  if (r.line < lines.size()) {
    r.token = ResolveToken(lines[r.line], r.character);
  }
  r.ok = true;
  return r;
}

}  // namespace

// ---------------------------------------------------------------------------
// textDocument/definition
// ---------------------------------------------------------------------------

void PolylsServer::HandleDefinition(int id, const Json &params) {
  ResolvedRequest r = Resolve(*this, params);
  if (!r.ok) {
    SendError(id, kInvalidParams, "definition: invalid params");
    return;
  }
  if (r.token.bare.empty()) {
    SendResponse(id, Json::array());
    return;
  }
  std::vector<SymbolLocation> hits = index_->Definition(r.token.bare);

  // Cross-language: if we're in a `.ploy` LINK target qualifier
  // ("cpp::module::func") jump to the host file.
  if (!r.token.language.empty()) {
    const std::size_t sep = r.token.qualified.find("::");
    const std::string qual_rest = (sep == std::string::npos)
                                      ? r.token.qualified
                                      : r.token.qualified.substr(sep + 2);
    auto cross = index_->CrossLanguageTarget(r.token.language, qual_rest);
    hits.insert(hits.end(), cross.begin(), cross.end());
  }
  SendResponse(id, LocationsToJson(hits));
}

// ---------------------------------------------------------------------------
// textDocument/declaration
// ---------------------------------------------------------------------------

void PolylsServer::HandleDeclaration(int id, const Json &params) {
  ResolvedRequest r = Resolve(*this, params);
  if (!r.ok) {
    SendError(id, kInvalidParams, "declaration: invalid params");
    return;
  }
  if (r.token.bare.empty()) {
    SendResponse(id, Json::array());
    return;
  }
  SendResponse(id, LocationsToJson(index_->Declaration(r.token.bare)));
}

// ---------------------------------------------------------------------------
// textDocument/implementation
// ---------------------------------------------------------------------------
//
// For `.ploy` LINK declarations the implementation IS the host-language
// target.  For ordinary FUNC / STRUCT entries the implementation
// coincides with the definition.

void PolylsServer::HandleImplementation(int id, const Json &params) {
  ResolvedRequest r = Resolve(*this, params);
  if (!r.ok) {
    SendError(id, kInvalidParams, "implementation: invalid params");
    return;
  }
  if (r.token.bare.empty()) {
    SendResponse(id, Json::array());
    return;
  }
  std::vector<SymbolLocation> hits = index_->Implementation(r.token.bare);
  if (!r.token.language.empty()) {
    const std::size_t sep = r.token.qualified.find("::");
    const std::string qual_rest = (sep == std::string::npos)
                                      ? r.token.qualified
                                      : r.token.qualified.substr(sep + 2);
    auto cross = index_->CrossLanguageTarget(r.token.language, qual_rest);
    hits.insert(hits.end(), cross.begin(), cross.end());
  }
  SendResponse(id, LocationsToJson(hits));
}

// ---------------------------------------------------------------------------
// textDocument/typeDefinition
// ---------------------------------------------------------------------------

void PolylsServer::HandleTypeDefinition(int id, const Json &params) {
  ResolvedRequest r = Resolve(*this, params);
  if (!r.ok) {
    SendError(id, kInvalidParams, "typeDefinition: invalid params");
    return;
  }
  if (r.token.bare.empty()) {
    SendResponse(id, Json::array());
    return;
  }
  SendResponse(id, LocationsToJson(index_->TypeDefinition(r.token.bare)));
}

// ---------------------------------------------------------------------------
// textDocument/references
// ---------------------------------------------------------------------------

void PolylsServer::HandleReferences(int id, const Json &params) {
  ResolvedRequest r = Resolve(*this, params);
  if (!r.ok) {
    SendError(id, kInvalidParams, "references: invalid params");
    return;
  }
  if (r.token.bare.empty()) {
    SendResponse(id, Json::array());
    return;
  }
  bool include_decl = true;
  if (params.contains("context") && params["context"].is_object()) {
    include_decl =
        params["context"].value("includeDeclaration", true);
  }
  std::vector<SymbolLocation> hits =
      index_->References(r.token.bare, include_decl);

  // Reverse cross-language: a host-language symbol additionally lists
  // every `.ploy` LINK site that imports it.
  if (r.language_id != "ploy" && r.language_id != "poly" &&
      !r.language_id.empty()) {
    auto back = index_->CrossLanguageBackrefs(r.language_id, r.token.bare);
    hits.insert(hits.end(), back.begin(), back.end());
  }
  SendResponse(id, LocationsToJson(hits));
}

}  // namespace polyglot::polyls
