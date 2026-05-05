/**
 * @file     polyls_semantic.cpp
 * @brief    `textDocument/semanticTokens/{full,range}` handlers.
 *
 * Wires the tree-sitter-shaped runtime (@ref tree_sitter_runtime.h)
 * into the polyls JSON-RPC server.  The runtime produces absolute
 * tokens; we delta-encode them according to the LSP 3.16 wire format
 * and ship them as a `SemanticTokens` payload.
 *
 * @ingroup  Tool / polyls
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <string>

#include "tools/polyls/grammar/grammar_descriptor.h"
#include "tools/polyls/polyls_core/polyls_server.h"
#include "tools/ui/common/lsp/lsp_message.h"
#include "tools/ui/common/syntax/tree_sitter_runtime.h"

namespace polyglot::polyls {

namespace lsp = polyglot::tools::ui::lsp;
namespace tsr = polyglot::polyls::ts;
namespace gr = polyglot::polyls::grammar;

namespace {

constexpr int kInvalidParams = -32602;

bool LookupDocument(PolylsServer &srv, const std::string &uri,
                    std::string &text, std::string &language_id) {
  for (const auto &doc : srv.SnapshotDocuments()) {
    if (doc.uri == uri) {
      text = doc.text;
      language_id = doc.language_id;
      return true;
    }
  }
  return false;
}

}  // namespace

void PolylsServer::HandleSemanticTokensFull(int id, const Json &params) {
  if (!params.is_object() || !params.contains("textDocument")) {
    SendError(id, kInvalidParams,
              "semanticTokens/full: missing textDocument");
    return;
  }
  const std::string uri =
      params["textDocument"].value("uri", std::string{});
  std::string text, language_id;
  if (!LookupDocument(*this, uri, text, language_id)) {
    SendResponse(id, Json{});
    return;
  }
  if (!gr::FindGrammar(language_id)) {
    // Unknown language → empty token stream so the editor falls back
    // to its regex highlighter.
    lsp::SemanticTokens st;
    SendResponse(id, lsp::ToJson(st));
    return;
  }
  auto tree = tsr::Parse(language_id, text);
  if (!tree) {
    SendResponse(id, Json{});
    return;
  }
  lsp::SemanticTokens st;
  st.data = tsr::EncodeSemanticTokens(tree->Tokens());
  SendResponse(id, lsp::ToJson(st));
}

void PolylsServer::HandleSemanticTokensRange(int id, const Json &params) {
  if (!params.is_object() || !params.contains("textDocument") ||
      !params.contains("range")) {
    SendError(id, kInvalidParams,
              "semanticTokens/range: missing textDocument or range");
    return;
  }
  const std::string uri =
      params["textDocument"].value("uri", std::string{});
  std::string text, language_id;
  if (!LookupDocument(*this, uri, text, language_id)) {
    SendResponse(id, Json{});
    return;
  }
  lsp::Range range;
  lsp::FromJson(params["range"], range);
  auto tree = tsr::Parse(language_id, text);
  if (!tree) {
    SendResponse(id, Json{});
    return;
  }
  lsp::SemanticTokens st;
  st.data = tsr::EncodeSemanticTokensRange(
      tree->Tokens(), range.start.line, range.end.line);
  SendResponse(id, lsp::ToJson(st));
}

}  // namespace polyglot::polyls
