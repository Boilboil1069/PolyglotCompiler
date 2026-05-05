/**
 * @file     lsp_navigation_e2e_test.cpp
 * @brief    End-to-end LSP navigation against the 09_mixed_pipeline sample
 *
 * Drives a full LSP client ↔ polyls round trip through the existing
 * @ref polyglot::tools::ui::lsp::LoopbackTransport pair.  The harness
 * opens the four files of `tests/samples/09_mixed_pipeline/` (the .ploy
 * pipeline plus its C++ / Python / Rust host modules) and validates
 * cross-language navigation: a `definition` request on the .ploy LINK
 * qualifier "image_processor::enhance" must return a location inside
 * `image_processor.cpp`, and a reverse `references` query inside the
 * C++ file must list the .ploy LINK call site.
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

TEST_CASE("LSP nav e2e: cross-language definition through the sample pipeline",
          "[lsp][integration][navigation]") {
  const std::filesystem::path samples_root = POLYGLOT_TESTS_SAMPLES_ROOT;
  const std::filesystem::path dir = samples_root / "09_mixed_pipeline";
  REQUIRE(std::filesystem::exists(dir));
  const auto ploy = dir / "mixed_pipeline.ploy";
  const auto cpp = dir / "image_processor.cpp";
  const auto py = dir / "ml_model.py";
  const auto rs = dir / "data_loader.rs";
  REQUIRE(std::filesystem::exists(ploy));
  REQUIRE(std::filesystem::exists(cpp));

  Harness h;
  std::atomic<bool> init_done{false};
  lsp::InitializeParams ip;
  ip.process_id = 0;
  ip.root_uri = PathToUri(dir);
  h.client()->Initialize(ip, [&](const lsp::Json &result, const lsp::Json &err) {
    REQUIRE(err.is_null());
    REQUIRE(result["capabilities"]["definitionProvider"].get<bool>());
    REQUIRE(result["capabilities"]["referencesProvider"].get<bool>());
    init_done = true;
  });
  REQUIRE(init_done.load());
  h.client()->Initialized();

  // Open every file: the .ploy pipeline + its host-language modules.
  // Order matters only insofar as the cpp/python/rust files must be
  // indexed before the cross-language query is issued.
  OpenDoc(*h.client(), cpp, "cpp");
  if (std::filesystem::exists(py)) OpenDoc(*h.client(), py, "python");
  if (std::filesystem::exists(rs)) OpenDoc(*h.client(), rs, "rust");
  OpenDoc(*h.client(), ploy, "ploy");

  // Locate the substring "image_processor::enhance" inside the .ploy
  // file so we can drive the cursor onto the host-language qualifier.
  const std::string ploy_text = ReadFile(ploy);
  const std::string needle = "image_processor::enhance";
  const std::size_t off = ploy_text.find(needle);
  REQUIRE(off != std::string::npos);
  // Convert byte offset → (line, character) (0-based).
  std::uint32_t line = 0;
  std::uint32_t character = 0;
  for (std::size_t i = 0; i < off; ++i) {
    if (ploy_text[i] == '\n') { ++line; character = 0; }
    else { ++character; }
  }
  // Place the cursor inside "enhance" (skip past "image_processor::").
  const std::uint32_t enh_col =
      character + static_cast<std::uint32_t>(std::string("image_processor::").size()) + 2;

  // textDocument/definition.
  std::atomic<bool> got_def{false};
  bool saw_cpp_target = false;
  const lsp::Json def_params = lsp::Json{
      {"textDocument", {{"uri", PathToUri(ploy)}}},
      {"position", {{"line", line}, {"character", enh_col}}}};
  h.client()->SendRequest(
      "textDocument/definition", def_params,
      [&](const lsp::Json &result, const lsp::Json &err) {
        REQUIRE(err.is_null());
        REQUIRE(result.is_array());
        for (const auto &loc : result) {
          if (loc.value("uri", std::string{}) == PathToUri(cpp)) {
            saw_cpp_target = true;
          }
        }
        got_def = true;
      });
  REQUIRE(got_def.load());
  REQUIRE(saw_cpp_target);

  // textDocument/references issued from inside the C++ host file should
  // surface the .ploy LINK call site (reverse cross-language).
  const std::string cpp_text = ReadFile(cpp);
  const std::size_t cpp_off = cpp_text.find("enhance");
  REQUIRE(cpp_off != std::string::npos);
  std::uint32_t cline = 0, ccol = 0;
  for (std::size_t i = 0; i < cpp_off + 2; ++i) {
    if (cpp_text[i] == '\n') { ++cline; ccol = 0; }
    else { ++ccol; }
  }
  const lsp::Json ref_params = lsp::Json{
      {"textDocument", {{"uri", PathToUri(cpp)}}},
      {"position", {{"line", cline}, {"character", ccol}}},
      {"context", {{"includeDeclaration", true}}}};
  std::atomic<bool> got_refs{false};
  bool saw_ploy_backref = false;
  h.client()->SendRequest(
      "textDocument/references", ref_params,
      [&](const lsp::Json &result, const lsp::Json &err) {
        REQUIRE(err.is_null());
        REQUIRE(result.is_array());
        for (const auto &loc : result) {
          if (loc.value("uri", std::string{}) == PathToUri(ploy)) {
            saw_ploy_backref = true;
          }
        }
        got_refs = true;
      });
  REQUIRE(got_refs.load());
  REQUIRE(saw_ploy_backref);
}
