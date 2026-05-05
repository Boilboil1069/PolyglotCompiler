/**
 * @file     lsp_client.h
 * @brief    LSP client core: JSON-RPC over an abstract transport
 *
 * The client is intentionally Qt-free.  Production code drives it from
 * a Qt thread that owns a @ref StdioTransport (wrapping a QProcess);
 * unit tests drive it from a @ref LoopbackTransport that round-trips
 * directly to a paired server instance in the same process.
 *
 * Capabilities advertised in this revision (per demand 2026-04-28-19 §2)
 * are limited to lifecycle + text-document sync + publishDiagnostics;
 * additional methods may still be issued via @ref SendRequest /
 * @ref SendNotification, but the demand defers richer features to
 * 2026-04-28-21..23.
 *
 * @ingroup  Tool / polyui / LSP
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "tools/ui/common/lsp/lsp_message.h"

namespace polyglot::tools::ui::lsp {

// ---------------------------------------------------------------------------
// Transport abstraction
// ---------------------------------------------------------------------------

/// Bidirectional byte channel for LSP wire framing.  Implementations
/// must be thread-safe for concurrent calls between Send() and the
/// callback installed via SetOnReceive().
class ILspTransport {
 public:
  using ReceiveCallback = std::function<void(const std::string & /*chunk*/)>;

  virtual ~ILspTransport() = default;

  /// Send raw bytes (already LSP-framed) to the peer.
  virtual void Send(const std::string &bytes) = 0;

  /// Install the callback invoked for every received chunk; the chunk
  /// may carry zero, one, or multiple framed messages (the client owns
  /// the reassembly buffer).
  virtual void SetOnReceive(ReceiveCallback cb) = 0;

  /// True iff the transport is connected and able to send.
  virtual bool IsOpen() const = 0;

  /// Close the transport.  Subsequent Send() calls become no-ops.
  virtual void Close() = 0;
};

// ---------------------------------------------------------------------------
// Loopback transport — direct in-process pair, used by unit tests.
// ---------------------------------------------------------------------------

/// Pair of two transport endpoints whose writes feed each other's reads.
/// Construct with @ref CreatePair to obtain @ref Endpoint A (client side)
/// and Endpoint B (server side).
class LoopbackTransport : public ILspTransport {
 public:
  static std::pair<std::shared_ptr<LoopbackTransport>,
                   std::shared_ptr<LoopbackTransport>>
  CreatePair();

  void Send(const std::string &bytes) override;
  void SetOnReceive(ReceiveCallback cb) override;
  bool IsOpen() const override { return open_; }
  void Close() override;

 private:
  LoopbackTransport() = default;

  std::mutex mu_;
  std::weak_ptr<LoopbackTransport> peer_;
  ReceiveCallback on_receive_;
  std::atomic<bool> open_{true};
};

// ---------------------------------------------------------------------------
// LspClient
// ---------------------------------------------------------------------------

/// JSON-RPC client speaking LSP over an @ref ILspTransport.  The client
/// owns the receive buffer and the request/response correlation table.
class LspClient {
 public:
  using ResponseHandler = std::function<void(const Json & /*result_or_null*/,
                                             const Json & /*error_or_null*/)>;
  using NotificationHandler =
      std::function<void(const Json & /*params*/)>;
  using LogHandler = std::function<void(const std::string & /*direction*/,
                                        const Json & /*payload*/)>;

  explicit LspClient(std::shared_ptr<ILspTransport> transport);
  ~LspClient();

  /// Issue a JSON-RPC request.  The handler is invoked when the matching
  /// response arrives (or with an error JSON when the server returns
  /// one).  Returns the synthesised request id.
  int SendRequest(const std::string &method, const Json &params,
                  ResponseHandler on_response);

  /// Fire-and-forget notification.
  void SendNotification(const std::string &method, const Json &params);

  /// Register a handler for inbound notifications of @p method.  Replaces
  /// any previous handler.
  void OnNotification(const std::string &method, NotificationHandler handler);

  /// Optional log sink: called with direction = "tx" / "rx" for every
  /// payload that crosses the wire.  Used by the LspLogPanel UI.
  void SetLogHandler(LogHandler handler);

  /// Convenience wrappers for the lifecycle / sync messages required by
  /// demand 2026-04-28-19 §2.
  void Initialize(const InitializeParams &params, ResponseHandler on_response);
  void Initialized();
  void Shutdown(ResponseHandler on_response);
  void Exit();
  void DidOpen(const DidOpenParams &params);
  void DidChange(const DidChangeParams &params);
  void DidClose(const DidCloseParams &params);
  void DidSave(const DidSaveParams &params);

  /// Convenience: subscribe to publishDiagnostics with a typed callback.
  void OnPublishDiagnostics(
      std::function<void(const PublishDiagnosticsParams &)> cb);

 private:
  void HandleChunk(const std::string &chunk);
  void DispatchPayload(const Json &payload);

  std::shared_ptr<ILspTransport> transport_;
  std::string rx_buffer_;
  std::mutex mu_;
  int next_id_{1};
  std::unordered_map<int, ResponseHandler> pending_;
  std::unordered_map<std::string, NotificationHandler> notif_handlers_;
  LogHandler log_handler_;
};

}  // namespace polyglot::tools::ui::lsp
