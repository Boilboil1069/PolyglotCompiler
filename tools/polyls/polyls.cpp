/**
 * @file     polyls.cpp
 * @brief    polyls — PolyglotCompiler self-hosted Language Server (stdio driver)
 *
 * Reads framed JSON-RPC messages from stdin, dispatches them through
 * @ref polyglot::polyls::PolylsServer, and writes replies/notifications
 * to stdout using the standard LSP `Content-Length` framing.  No
 * threading is required because LSP clients (e.g. polyui's
 * StdioTransport) are themselves serialised by the editor's debounce
 * timer.
 *
 * @ingroup  Tool / polyls
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include <cstdio>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "tools/polyls/polyls_core/polyls_server.h"
#include "tools/ui/common/lsp/lsp_message.h"

int main(int /*argc*/, char ** /*argv*/) {
#ifdef _WIN32
  // Force binary mode on stdin/stdout so CRLF translation does not corrupt
  // LSP framing on Windows hosts.
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif

  using polyglot::tools::ui::lsp::EncodeFrame;
  using polyglot::tools::ui::lsp::Json;
  using polyglot::tools::ui::lsp::TryDecodeFrame;

  polyglot::polyls::PolylsServer server;
  server.SetSendHandler([](const Json &payload) {
    const std::string frame = EncodeFrame(payload);
    std::fwrite(frame.data(), 1, frame.size(), stdout);
    std::fflush(stdout);
  });

  std::string buffer;
  buffer.reserve(8 * 1024);
  char chunk[4096];

  while (!server.ExitRequested()) {
    const std::size_t n = std::fread(chunk, 1, sizeof(chunk), stdin);
    if (n == 0) {
      if (std::feof(stdin)) break;
      if (std::ferror(stdin)) break;
      continue;
    }
    buffer.append(chunk, n);

    Json payload;
    while (TryDecodeFrame(buffer, payload)) {
      server.HandleIncoming(payload);
      if (server.ExitRequested()) break;
    }
  }

  // Per LSP spec, exit code 0 only when shutdown was requested first.
  return server.ShutdownRequested() ? 0 : 1;
}
