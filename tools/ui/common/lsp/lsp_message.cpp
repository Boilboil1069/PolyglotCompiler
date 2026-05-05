/**
 * @file     lsp_message.cpp
 * @brief    JSON serialization + JSON-RPC framing for LSP messages
 *
 * @ingroup  Tool / polyui / LSP
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include "tools/ui/common/lsp/lsp_message.h"

#include <cctype>
#include <charconv>
#include <stdexcept>

namespace polyglot::tools::ui::lsp {

// ---------------------------------------------------------------------------
// ToJson
// ---------------------------------------------------------------------------

Json ToJson(const Position &v) {
  return Json{{"line", v.line}, {"character", v.character}};
}

Json ToJson(const Range &v) {
  return Json{{"start", ToJson(v.start)}, {"end", ToJson(v.end)}};
}

Json ToJson(const Location &v) {
  return Json{{"uri", v.uri}, {"range", ToJson(v.range)}};
}

Json ToJson(const Diagnostic &v) {
  Json j = {
      {"range", ToJson(v.range)},
      {"severity", static_cast<int>(v.severity)},
      {"message", v.message},
  };
  if (!v.code.empty()) j["code"] = v.code;
  if (!v.source.empty()) j["source"] = v.source;
  return j;
}

Json ToJson(const TextEdit &v) {
  return Json{{"range", ToJson(v.range)}, {"newText", v.new_text}};
}

Json ToJson(const WorkspaceEdit &v) {
  Json changes = Json::object();
  for (const auto &kv : v.changes) {
    Json arr = Json::array();
    for (const auto &edit : kv.second) arr.push_back(ToJson(edit));
    changes[kv.first] = arr;
  }
  return Json{{"changes", changes}};
}

Json ToJson(const MarkupContent &v) {
  return Json{{"kind", v.kind}, {"value", v.value}};
}

Json ToJson(const Hover &v) {
  Json j = {{"contents", ToJson(v.contents)}};
  if (v.range) j["range"] = ToJson(*v.range);
  return j;
}

Json ToJson(const CompletionItem &v) {
  Json j = {
      {"label", v.label},
      {"kind", static_cast<int>(v.kind)},
  };
  if (!v.detail.empty()) j["detail"] = v.detail;
  if (!v.documentation.empty()) j["documentation"] = v.documentation;
  if (!v.insert_text.empty()) j["insertText"] = v.insert_text;
  return j;
}

Json ToJson(const ParameterInformation &v) {
  Json j = {{"label", v.label}};
  if (!v.documentation.empty()) j["documentation"] = v.documentation;
  return j;
}

Json ToJson(const SignatureInformation &v) {
  Json params = Json::array();
  for (const auto &p : v.parameters) params.push_back(ToJson(p));
  Json j = {{"label", v.label}, {"parameters", params}};
  if (!v.documentation.empty()) j["documentation"] = v.documentation;
  return j;
}

Json ToJson(const SignatureHelp &v) {
  Json sigs = Json::array();
  for (const auto &s : v.signatures) sigs.push_back(ToJson(s));
  Json j = {{"signatures", sigs}};
  if (v.active_signature) j["activeSignature"] = *v.active_signature;
  if (v.active_parameter) j["activeParameter"] = *v.active_parameter;
  return j;
}

Json ToJson(const SymbolInformation &v) {
  Json j = {
      {"name", v.name},
      {"kind", static_cast<int>(v.kind)},
      {"location", ToJson(v.location)},
  };
  if (!v.container_name.empty()) j["containerName"] = v.container_name;
  return j;
}

Json ToJson(const DocumentSymbol &v) {
  Json children = Json::array();
  for (const auto &c : v.children) children.push_back(ToJson(c));
  Json j = {
      {"name", v.name},
      {"kind", static_cast<int>(v.kind)},
      {"range", ToJson(v.range)},
      {"selectionRange", ToJson(v.selection_range)},
      {"children", children},
  };
  if (!v.detail.empty()) j["detail"] = v.detail;
  return j;
}

Json ToJson(const CodeAction &v) {
  Json j = {{"title", v.title}};
  if (!v.kind.empty()) j["kind"] = v.kind;
  if (!v.edit.changes.empty()) j["edit"] = ToJson(v.edit);
  return j;
}

Json ToJson(const TextDocumentIdentifier &v) { return Json{{"uri", v.uri}}; }

Json ToJson(const VersionedTextDocumentIdentifier &v) {
  return Json{{"uri", v.uri}, {"version", v.version}};
}

Json ToJson(const TextDocumentItem &v) {
  return Json{
      {"uri", v.uri},
      {"languageId", v.language_id},
      {"version", v.version},
      {"text", v.text},
  };
}

Json ToJson(const TextDocumentContentChangeEvent &v) {
  Json j;
  j["text"] = v.text;
  if (v.range) j["range"] = ToJson(*v.range);
  return j;
}

Json ToJson(const DidOpenParams &v) {
  return Json{{"textDocument", ToJson(v.text_document)}};
}

Json ToJson(const DidChangeParams &v) {
  Json changes = Json::array();
  for (const auto &c : v.content_changes) changes.push_back(ToJson(c));
  return Json{
      {"textDocument", ToJson(v.text_document)},
      {"contentChanges", changes},
  };
}

Json ToJson(const DidCloseParams &v) {
  return Json{{"textDocument", ToJson(v.text_document)}};
}

Json ToJson(const DidSaveParams &v) {
  Json j = {{"textDocument", ToJson(v.text_document)}};
  if (v.text) j["text"] = *v.text;
  return j;
}

Json ToJson(const PublishDiagnosticsParams &v) {
  Json arr = Json::array();
  for (const auto &d : v.diagnostics) arr.push_back(ToJson(d));
  return Json{{"uri", v.uri}, {"diagnostics", arr}};
}

Json ToJson(const ServerCapabilities &v) {
  return Json{
      {"textDocumentSync", v.text_document_sync},
      {"hoverProvider", v.hover_provider},
      {"completionProvider", v.completion_provider},
      {"signatureHelpProvider", v.signature_help_provider},
      {"definitionProvider", v.definition_provider},
      {"referencesProvider", v.references_provider},
      {"documentSymbolProvider", v.document_symbol_provider},
      {"renameProvider", v.rename_provider},
      {"codeActionProvider", v.code_action_provider},
      {"diagnosticProvider", v.diagnostic_provider},
  };
}

Json ToJson(const InitializeParams &v) {
  Json j = Json::object();
  if (v.process_id) j["processId"] = *v.process_id;
  if (v.root_uri) j["rootUri"] = *v.root_uri;
  j["capabilities"] = v.capabilities.raw;
  j["initializationOptions"] = v.initialization_options;
  return j;
}

Json ToJson(const InitializeResult &v) {
  return Json{
      {"capabilities", ToJson(v.capabilities)},
      {"serverInfo", {{"name", v.server_name}, {"version", v.server_version}}},
  };
}

// ---------------------------------------------------------------------------
// FromJson — tolerant readers (missing fields keep their defaults).
// ---------------------------------------------------------------------------

namespace {

template <typename T>
T GetOr(const Json &j, const char *key, T fallback) {
  auto it = j.find(key);
  if (it == j.end() || it->is_null()) return fallback;
  try {
    return it->get<T>();
  } catch (...) {
    return fallback;
  }
}

}  // namespace

void FromJson(const Json &j, Position &v) {
  v.line = GetOr<std::uint32_t>(j, "line", 0);
  v.character = GetOr<std::uint32_t>(j, "character", 0);
}

void FromJson(const Json &j, Range &v) {
  if (auto it = j.find("start"); it != j.end()) FromJson(*it, v.start);
  if (auto it = j.find("end"); it != j.end()) FromJson(*it, v.end);
}

void FromJson(const Json &j, Location &v) {
  v.uri = GetOr<std::string>(j, "uri", "");
  if (auto it = j.find("range"); it != j.end()) FromJson(*it, v.range);
}

void FromJson(const Json &j, Diagnostic &v) {
  if (auto it = j.find("range"); it != j.end()) FromJson(*it, v.range);
  v.severity =
      static_cast<DiagnosticSeverity>(GetOr<int>(j, "severity", 1));
  v.message = GetOr<std::string>(j, "message", "");
  v.source = GetOr<std::string>(j, "source", "");
  if (auto it = j.find("code"); it != j.end()) {
    if (it->is_string()) v.code = it->get<std::string>();
    else if (it->is_number_integer()) v.code = std::to_string(it->get<int>());
  }
}

void FromJson(const Json &j, TextEdit &v) {
  if (auto it = j.find("range"); it != j.end()) FromJson(*it, v.range);
  v.new_text = GetOr<std::string>(j, "newText", "");
}

void FromJson(const Json &j, WorkspaceEdit &v) {
  v.changes.clear();
  if (auto it = j.find("changes"); it != j.end() && it->is_object()) {
    for (auto entry = it->begin(); entry != it->end(); ++entry) {
      std::vector<TextEdit> edits;
      if (entry->is_array()) {
        for (const auto &e : *entry) {
          TextEdit te;
          FromJson(e, te);
          edits.push_back(std::move(te));
        }
      }
      v.changes[entry.key()] = std::move(edits);
    }
  }
}

void FromJson(const Json &j, MarkupContent &v) {
  v.kind = GetOr<std::string>(j, "kind", "markdown");
  v.value = GetOr<std::string>(j, "value", "");
}

void FromJson(const Json &j, Hover &v) {
  if (auto it = j.find("contents"); it != j.end() && it->is_object()) {
    FromJson(*it, v.contents);
  } else if (auto it2 = j.find("contents"); it2 != j.end() && it2->is_string()) {
    v.contents.kind = "plaintext";
    v.contents.value = it2->get<std::string>();
  }
  if (auto it = j.find("range"); it != j.end()) {
    Range r;
    FromJson(*it, r);
    v.range = r;
  }
}

void FromJson(const Json &j, CompletionItem &v) {
  v.label = GetOr<std::string>(j, "label", "");
  v.kind = static_cast<CompletionItemKind>(
      GetOr<int>(j, "kind", static_cast<int>(CompletionItemKind::kText)));
  v.detail = GetOr<std::string>(j, "detail", "");
  v.documentation = GetOr<std::string>(j, "documentation", "");
  v.insert_text = GetOr<std::string>(j, "insertText", "");
}

void FromJson(const Json &j, ParameterInformation &v) {
  v.label = GetOr<std::string>(j, "label", "");
  v.documentation = GetOr<std::string>(j, "documentation", "");
}

void FromJson(const Json &j, SignatureInformation &v) {
  v.label = GetOr<std::string>(j, "label", "");
  v.documentation = GetOr<std::string>(j, "documentation", "");
  v.parameters.clear();
  if (auto it = j.find("parameters"); it != j.end() && it->is_array()) {
    for (const auto &p : *it) {
      ParameterInformation pi;
      FromJson(p, pi);
      v.parameters.push_back(std::move(pi));
    }
  }
}

void FromJson(const Json &j, SignatureHelp &v) {
  v.signatures.clear();
  if (auto it = j.find("signatures"); it != j.end() && it->is_array()) {
    for (const auto &s : *it) {
      SignatureInformation si;
      FromJson(s, si);
      v.signatures.push_back(std::move(si));
    }
  }
  if (auto it = j.find("activeSignature"); it != j.end() && it->is_number_integer()) {
    v.active_signature = it->get<int>();
  }
  if (auto it = j.find("activeParameter"); it != j.end() && it->is_number_integer()) {
    v.active_parameter = it->get<int>();
  }
}

void FromJson(const Json &j, SymbolInformation &v) {
  v.name = GetOr<std::string>(j, "name", "");
  v.kind = static_cast<SymbolKind>(
      GetOr<int>(j, "kind", static_cast<int>(SymbolKind::kVariable)));
  if (auto it = j.find("location"); it != j.end()) FromJson(*it, v.location);
  v.container_name = GetOr<std::string>(j, "containerName", "");
}

void FromJson(const Json &j, DocumentSymbol &v) {
  v.name = GetOr<std::string>(j, "name", "");
  v.detail = GetOr<std::string>(j, "detail", "");
  v.kind = static_cast<SymbolKind>(
      GetOr<int>(j, "kind", static_cast<int>(SymbolKind::kVariable)));
  if (auto it = j.find("range"); it != j.end()) FromJson(*it, v.range);
  if (auto it = j.find("selectionRange"); it != j.end()) FromJson(*it, v.selection_range);
  v.children.clear();
  if (auto it = j.find("children"); it != j.end() && it->is_array()) {
    for (const auto &c : *it) {
      DocumentSymbol child;
      FromJson(c, child);
      v.children.push_back(std::move(child));
    }
  }
}

void FromJson(const Json &j, CodeAction &v) {
  v.title = GetOr<std::string>(j, "title", "");
  v.kind = GetOr<std::string>(j, "kind", "");
  if (auto it = j.find("edit"); it != j.end()) FromJson(*it, v.edit);
}

void FromJson(const Json &j, TextDocumentIdentifier &v) {
  v.uri = GetOr<std::string>(j, "uri", "");
}

void FromJson(const Json &j, VersionedTextDocumentIdentifier &v) {
  v.uri = GetOr<std::string>(j, "uri", "");
  v.version = GetOr<std::int32_t>(j, "version", 0);
}

void FromJson(const Json &j, TextDocumentItem &v) {
  v.uri = GetOr<std::string>(j, "uri", "");
  v.language_id = GetOr<std::string>(j, "languageId", "");
  v.version = GetOr<std::int32_t>(j, "version", 0);
  v.text = GetOr<std::string>(j, "text", "");
}

void FromJson(const Json &j, TextDocumentContentChangeEvent &v) {
  v.text = GetOr<std::string>(j, "text", "");
  if (auto it = j.find("range"); it != j.end()) {
    Range r;
    FromJson(*it, r);
    v.range = r;
  }
}

void FromJson(const Json &j, DidOpenParams &v) {
  if (auto it = j.find("textDocument"); it != j.end()) FromJson(*it, v.text_document);
}

void FromJson(const Json &j, DidChangeParams &v) {
  if (auto it = j.find("textDocument"); it != j.end()) FromJson(*it, v.text_document);
  v.content_changes.clear();
  if (auto it = j.find("contentChanges"); it != j.end() && it->is_array()) {
    for (const auto &c : *it) {
      TextDocumentContentChangeEvent ev;
      FromJson(c, ev);
      v.content_changes.push_back(std::move(ev));
    }
  }
}

void FromJson(const Json &j, DidCloseParams &v) {
  if (auto it = j.find("textDocument"); it != j.end()) FromJson(*it, v.text_document);
}

void FromJson(const Json &j, DidSaveParams &v) {
  if (auto it = j.find("textDocument"); it != j.end()) FromJson(*it, v.text_document);
  if (auto it = j.find("text"); it != j.end() && it->is_string()) {
    v.text = it->get<std::string>();
  }
}

void FromJson(const Json &j, PublishDiagnosticsParams &v) {
  v.uri = GetOr<std::string>(j, "uri", "");
  v.diagnostics.clear();
  if (auto it = j.find("diagnostics"); it != j.end() && it->is_array()) {
    for (const auto &d : *it) {
      Diagnostic dg;
      FromJson(d, dg);
      v.diagnostics.push_back(std::move(dg));
    }
  }
}

void FromJson(const Json &j, ServerCapabilities &v) {
  v.text_document_sync = GetOr<int>(j, "textDocumentSync", 1);
  v.hover_provider = GetOr<bool>(j, "hoverProvider", false);
  v.completion_provider = GetOr<bool>(j, "completionProvider", false);
  v.signature_help_provider = GetOr<bool>(j, "signatureHelpProvider", false);
  v.definition_provider = GetOr<bool>(j, "definitionProvider", false);
  v.references_provider = GetOr<bool>(j, "referencesProvider", false);
  v.document_symbol_provider = GetOr<bool>(j, "documentSymbolProvider", false);
  v.rename_provider = GetOr<bool>(j, "renameProvider", false);
  v.code_action_provider = GetOr<bool>(j, "codeActionProvider", false);
  v.diagnostic_provider = GetOr<bool>(j, "diagnosticProvider", true);
}

void FromJson(const Json &j, InitializeParams &v) {
  if (auto it = j.find("processId"); it != j.end() && it->is_number_integer()) {
    v.process_id = it->get<int>();
  }
  if (auto it = j.find("rootUri"); it != j.end() && it->is_string()) {
    v.root_uri = it->get<std::string>();
  }
  if (auto it = j.find("capabilities"); it != j.end()) v.capabilities.raw = *it;
  if (auto it = j.find("initializationOptions"); it != j.end()) {
    v.initialization_options = *it;
  }
}

// ---------------------------------------------------------------------------
// JSON-RPC envelopes
// ---------------------------------------------------------------------------

Json MakeRequest(int id, const std::string &method, const Json &params) {
  return Json{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
}

Json MakeNotification(const std::string &method, const Json &params) {
  return Json{{"jsonrpc", "2.0"}, {"method", method}, {"params", params}};
}

Json MakeResponse(int id, const Json &result) {
  return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

Json MakeErrorResponse(int id, int code, const std::string &message) {
  return Json{
      {"jsonrpc", "2.0"},
      {"id", id},
      {"error", {{"code", code}, {"message", message}}},
  };
}

// ---------------------------------------------------------------------------
// LSP wire framing — `Content-Length: N\r\n\r\n<payload>`
// ---------------------------------------------------------------------------

std::string EncodeFrame(const Json &payload) {
  const std::string body = payload.dump();
  std::string frame;
  frame.reserve(body.size() + 32);
  frame.append("Content-Length: ");
  frame.append(std::to_string(body.size()));
  frame.append("\r\n\r\n");
  frame.append(body);
  return frame;
}

namespace {

/// Case-insensitive prefix match for header parsing.
bool IEqualsPrefix(const std::string &haystack, std::size_t pos,
                   const std::string &needle) {
  if (pos + needle.size() > haystack.size()) return false;
  for (std::size_t i = 0; i < needle.size(); ++i) {
    char a = static_cast<char>(std::tolower(static_cast<unsigned char>(haystack[pos + i])));
    char b = static_cast<char>(std::tolower(static_cast<unsigned char>(needle[i])));
    if (a != b) return false;
  }
  return true;
}

}  // namespace

bool TryDecodeFrame(std::string &buffer, Json &out_payload) {
  // Locate end-of-headers (first "\r\n\r\n").
  const std::size_t header_end = buffer.find("\r\n\r\n");
  if (header_end == std::string::npos) return false;

  // Find Content-Length value within the header block.
  const std::string headers = buffer.substr(0, header_end);
  std::size_t cl_pos = 0;
  std::size_t scan = 0;
  bool found = false;
  while (scan < headers.size()) {
    if (IEqualsPrefix(headers, scan, "content-length:")) {
      cl_pos = scan + 15;
      found = true;
      break;
    }
    const std::size_t nl = headers.find("\r\n", scan);
    if (nl == std::string::npos) break;
    scan = nl + 2;
  }
  if (!found) {
    // Malformed: drop the header block to avoid an infinite loop.
    buffer.erase(0, header_end + 4);
    return false;
  }

  while (cl_pos < headers.size() && std::isspace(static_cast<unsigned char>(headers[cl_pos]))) {
    ++cl_pos;
  }
  std::size_t cl_end = cl_pos;
  while (cl_end < headers.size() && std::isdigit(static_cast<unsigned char>(headers[cl_end]))) {
    ++cl_end;
  }
  std::size_t length = 0;
  auto [ptr, ec] = std::from_chars(headers.data() + cl_pos, headers.data() + cl_end, length);
  (void)ptr;
  if (ec != std::errc{}) {
    buffer.erase(0, header_end + 4);
    return false;
  }

  const std::size_t body_start = header_end + 4;
  if (buffer.size() < body_start + length) return false;  // wait for more bytes

  const std::string body = buffer.substr(body_start, length);
  buffer.erase(0, body_start + length);

  try {
    out_payload = Json::parse(body);
  } catch (const std::exception &) {
    return false;  // drop malformed body, but the framing was consumed
  }
  return true;
}

}  // namespace polyglot::tools::ui::lsp
