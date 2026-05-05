/**
 * @file     lsp_refactor_e2e_test.cpp
 * @brief    End-to-end LSP refactoring against the 09_mixed_pipeline sample
 *           (demand 2026-04-28-23)
 *
 * Drives a full LSP client ↔ polyls round trip and asserts that a
 * rename initiated inside the C++ host file `image_processor.cpp`
 * propagates back into the .ploy LINK declaration that imports it.
 *
 * @ingroup  Tests / integration / LSP
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "tools/polyls/polyls_core/polyls_server.h"
#include "tools/ui/common/lsp/lsp_client.h"
#include "tools/ui/common/lsp/lsp_message.h"

namespace lsp = polyglot::tools::ui::lsp;

namespace {

std::string ReadFile(const std::filesystem::path &p) {
  std::ifstream f(p);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

class Harness {
 public:
  Harness() {
    auto pair = lsp::LoopbackTransport::CreatePair();
    client_transport_ = pair.first;
    server_transport_ = pair.second;
    client_ = std::make_shared<lsp::LspClient>(client_transport_);
    server_transport_->SetOnReceive([this](const std::string &chunk) {
      server_buffer_.append(chunk);
      lsp::Json payload;
      while (lsp::TryDecodeFrame(server_buffer_, payload)) {
        server_.HandleIncoming(payload);
      }
    });
    server_.SetSendHandler([this](const lsp::Json &payload) {
      server_transport_->Send(lsp::EncodeFrame(payload));
    });
  }
  std::shared_ptr<lsp::LspClient> client() { return client_; }

 private:
  std::shared_ptr<lsp::LoopbackTransport> client_transport_;
  std::shared_ptr<lsp::LoopbackTransport> server_transport_;
  std::shared_ptr<lsp::LspClient> client_;
  polyglot::polyls::PolylsServer server_;
  std::string server_buffer_;
};

std::string PathToUri(const std::filesystem::path &p) {
  return "file://" + p.string();
}

void OpenDoc(lsp::LspClient &c, const std::filesystem::path &p,
             const std::string &lang) {
  lsp::DidOpenParams open;
  open.text_document.uri = PathToUri(p);
  open.text_document.language_id = lang;
  open.text_document.version = 1;
  open.text_document.text = ReadFile(p);
  c.DidOpen(open);
}

}  // namespace

TEST_CASE("LSP refactor e2e: cross-language rename through the sample pipeline",
          "[lsp][integration][refactor]") {
  const std::filesystem::path samples_root = POLYGLOT_TESTS_SAMPLES_ROOT;
  const std::filesystem::path dir = samples_root / "09_mixed_pipeline";
  REQUIRE(std::filesystem::exists(dir));
  const auto ploy = dir / "mixed_pipeline.ploy";
  const auto cpp = dir / "image_processor.cpp";
  REQUIRE(std::filesystem::exists(ploy));
  REQUIRE(std::filesystem::exists(cpp));

  Harness h;
  std::atomic<bool> init_done{false};
  lsp::InitializeParams ip;
  ip.process_id = 0;
  ip.root_uri = PathToUri(dir);
  h.client()->Initialize(ip, [&](const lsp::Json &result, const lsp::Json &err) {
    REQUIRE(err.is_null());
    REQUIRE(result["capabilities"]["renameProvider"].get<bool>());
    REQUIRE(result["capabilities"]["codeActionProvider"].get<bool>());
    init_done = true;
  });
  REQUIRE(init_done.load());
  h.client()->Initialized();

  OpenDoc(*h.client(), cpp, "cpp");
  OpenDoc(*h.client(), ploy, "ploy");

  // Rename the host-language symbol `enhance` from inside the C++ file
  // and confirm the resulting WorkspaceEdit also rewrites the .ploy
  // LINK qualifier that imports it.
  const std::string cpp_text = ReadFile(cpp);
  const std::size_t off = cpp_text.find("enhance");
  REQUIRE(off != std::string::npos);
  std::uint32_t cline = 0, ccol = 0;
  for (std::size_t i = 0; i < off + 2; ++i) {
    if (cpp_text[i] == '\n') { ++cline; ccol = 0; }
    else { ++ccol; }
  }

  std::atomic<bool> got_prepare{false};
  h.client()->SendRequest(
      "textDocument/prepareRename",
      lsp::Json{{"textDocument", {{"uri", PathToUri(cpp)}}},
                {"position", {{"line", cline}, {"character", ccol}}}},
      [&](const lsp::Json &result, const lsp::Json &err) {
        REQUIRE(err.is_null());
        REQUIRE(result.contains("start"));
        got_prepare = true;
      });
  REQUIRE(got_prepare.load());

  std::atomic<bool> got_rename{false};
  bool touched_cpp = false;
  bool touched_ploy = false;
  h.client()->SendRequest(
      "textDocument/rename",
      lsp::Json{{"textDocument", {{"uri", PathToUri(cpp)}}},
                {"position", {{"line", cline}, {"character", ccol}}},
                {"newName", "boost"}},
      [&](const lsp::Json &result, const lsp::Json &err) {
        REQUIRE(err.is_null());
        const auto &changes = result["changes"];
        REQUIRE(changes.is_object());
        if (changes.contains(PathToUri(cpp))) touched_cpp = true;
        if (changes.contains(PathToUri(ploy))) touched_ploy = true;
        got_rename = true;
      });
  REQUIRE(got_rename.load());
  REQUIRE(touched_cpp);
  REQUIRE(touched_ploy);

  // codeAction over a non-empty selection in the .ploy file should
  // surface at least the extract / inline / change-signature / move
  // entries from the refactor catalogue.
  std::atomic<bool> got_actions{false};
  std::size_t action_count = 0;
  h.client()->SendRequest(
      "textDocument/codeAction",
      lsp::Json{{"textDocument", {{"uri", PathToUri(ploy)}}},
                {"range",
                 {{"start", {{"line", 0}, {"character", 0}}},
                  {"end", {{"line", 0}, {"character", 1}}}}}},
      [&](const lsp::Json &result, const lsp::Json &err) {
        REQUIRE(err.is_null());
        REQUIRE(result.is_array());
        action_count = result.size();
        got_actions = true;
      });
  REQUIRE(got_actions.load());
  REQUIRE(action_count >= 4);
}
