/**
 * @file     polyls_refactor.cpp
 * @brief    Refactoring feature handlers for polyls
 *
 * Implements `textDocument/prepareRename`, `textDocument/rename` and
 * `textDocument/codeAction` on top of the workspace refactor engine
 * (@ref refactor.h).  See demand 2026-04-28-23 for the contract.
 *
 * @ingroup  Tool / polyls
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <string>

#include "tools/polyls/polyls_core/polyls_server.h"
#include "tools/polyls/polyls_core/refactor.h"
#include "tools/polyls/polyls_core/symbol_index.h"
#include "tools/ui/common/lsp/lsp_message.h"

namespace polyglot::polyls {

namespace lsp = polyglot::tools::ui::lsp;

namespace {

constexpr int kInvalidParams = -32602;

/// Build the in-memory document snapshot that the refactor engine
/// consumes.  Matches the open-document store layout exactly.
std::vector<DocumentView> SnapshotForRefactor(PolylsServer &srv) {
  std::vector<DocumentView> out;
  for (const auto &doc : srv.SnapshotDocuments()) {
    DocumentView dv;
    dv.uri = doc.uri;
    dv.language_id = doc.language_id;
    dv.text = doc.text;
    out.push_back(std::move(dv));
  }
  return out;
}

bool ExtractPosition(const Json &params, std::string &uri,
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

}  // namespace

// ---------------------------------------------------------------------------
// textDocument/prepareRename
// ---------------------------------------------------------------------------

void PolylsServer::HandlePrepareRename(int id, const Json &params) {
  std::string uri;
  std::uint32_t line = 0;
  std::uint32_t character = 0;
  if (!ExtractPosition(params, uri, line, character)) {
    SendError(id, kInvalidParams, "prepareRename: invalid params");
    return;
  }
  auto docs = SnapshotForRefactor(*this);
  auto range = PrepareRename(docs, uri, line, character);
  if (!range) {
    SendResponse(id, Json{});  // null result = not renameable.
    return;
  }
  SendResponse(id, lsp::ToJson(*range));
}

// ---------------------------------------------------------------------------
// textDocument/rename
// ---------------------------------------------------------------------------

void PolylsServer::HandleRename(int id, const Json &params) {
  std::string uri;
  std::uint32_t line = 0;
  std::uint32_t character = 0;
  if (!ExtractPosition(params, uri, line, character)) {
    SendError(id, kInvalidParams, "rename: invalid params");
    return;
  }
  const std::string new_name = params.value("newName", std::string{});
  if (!IsValidIdentifier(new_name)) {
    SendError(id, kInvalidParams, "rename: invalid newName");
    return;
  }
  auto docs = SnapshotForRefactor(*this);
  auto edit =
      BuildRenameEdit(*index_, docs, uri, line, character, new_name);
  if (!edit) {
    SendResponse(id, Json{});
    return;
  }
  SendResponse(id, lsp::ToJson(*edit));
}

// ---------------------------------------------------------------------------
// textDocument/codeAction
// ---------------------------------------------------------------------------

void PolylsServer::HandleCodeAction(int id, const Json &params) {
  if (!params.is_object() || !params.contains("textDocument") ||
      !params.contains("range")) {
    SendError(id, kInvalidParams, "codeAction: invalid params");
    return;
  }
  const std::string uri =
      params["textDocument"].value("uri", std::string{});
  lsp::Range range;
  lsp::FromJson(params["range"], range);

  auto docs = SnapshotForRefactor(*this);
  auto actions = BuildCodeActions(*index_, docs, uri, range);
  Json arr = Json::array();
  for (const auto &ca : actions) arr.push_back(lsp::ToJson(ca));
  SendResponse(id, arr);
}

}  // namespace polyglot::polyls
