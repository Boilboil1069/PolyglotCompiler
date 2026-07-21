/**
 * @file     dap_client.h
 * @brief    Debug Adapter Protocol client (demand 2026-04-28-28 §1).
 *
 * Pure protocol layer: takes a stream of bytes (Content-Length framed
 * JSON, identical to LSP) and produces decoded request / response /
 * event callbacks.  Outbound messages are produced as `std::string`
 * frames so the IDE can write them to whatever transport (stdio,
 * socket, pipe) the launch configuration selected.
 *
 * The client owns no I/O of its own — all I/O lives in the IDE shell
 * — which keeps this module Qt-free and trivially testable.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace polyglot::tools::ui::dap {

using Json = nlohmann::json;

/// Wraps `Content-Length: N\r\n\r\n<json>` framing.  Unframe one or
/// more complete messages; partial buffers are retained until the
/// next call.
class MessageFramer {
 public:
  /// Append raw bytes received from the adapter.  Returns the list
  /// of decoded JSON envelopes; the buffer keeps any partial frame.
  std::vector<Json> Feed(std::string_view bytes);

  /// Encode a single JSON envelope into a framed string suitable
  /// for direct write() to the adapter's stdin.
  static std::string Frame(const Json &envelope);

  std::size_t buffered_bytes() const { return buffer_.size(); }

 private:
  std::string buffer_;
};

/// Names of every DAP request the client knows how to issue.  The
/// list mirrors demand 2026-04-28-28 §1.
namespace requests {
constexpr const char *kInitialize = "initialize";
constexpr const char *kLaunch = "launch";
constexpr const char *kAttach = "attach";
constexpr const char *kSetBreakpoints = "setBreakpoints";
constexpr const char *kSetExceptionBreakpoints = "setExceptionBreakpoints";
constexpr const char *kSetDataBreakpoints = "setDataBreakpoints";
constexpr const char *kSetFunctionBreakpoints = "setFunctionBreakpoints";
constexpr const char *kConfigurationDone = "configurationDone";
constexpr const char *kThreads = "threads";
constexpr const char *kStackTrace = "stackTrace";
constexpr const char *kScopes = "scopes";
constexpr const char *kVariables = "variables";
constexpr const char *kEvaluate = "evaluate";
constexpr const char *kContinue = "continue";
constexpr const char *kNext = "next";
constexpr const char *kStepIn = "stepIn";
constexpr const char *kStepOut = "stepOut";
constexpr const char *kPause = "pause";
constexpr const char *kDisconnect = "disconnect";
constexpr const char *kTerminate = "terminate";
}  // namespace requests

/// Names of the DAP events the client recognises.
namespace events {
constexpr const char *kStopped = "stopped";
constexpr const char *kContinued = "continued";
constexpr const char *kExited = "exited";
constexpr const char *kTerminated = "terminated";
constexpr const char *kOutput = "output";
constexpr const char *kBreakpoint = "breakpoint";
constexpr const char *kThread = "thread";
constexpr const char *kInitialized = "initialized";
}  // namespace events

/// Result of a DAP response.  Either a parsed `body` JSON object on
/// success, or an `error_message` extracted from `message`/`body`
/// when `success == false`.
struct Response {
  std::int64_t request_seq{0};
  std::string command;
  bool success{true};
  std::string error_message;
  Json body;
};

using ResponseCallback = std::function<void(const Response &)>;
using EventCallback = std::function<void(const std::string &event,
                                         const Json &body)>;
using SendHandler = std::function<void(const std::string &framed)>;

/// Stateful DAP client that owns the request sequence counter and
/// pending-callback table.  All handlers run synchronously on the
/// thread that calls `Receive`.
class DapClient {
 public:
  DapClient();

  /// Bind the transport.  Every outbound JSON is framed and passed
  /// to `handler`.  The handler must perform actual I/O.
  void SetSendHandler(SendHandler handler) { send_ = std::move(handler); }

  /// Default callback invoked for unhandled events.
  void SetEventHandler(EventCallback handler) {
    event_handler_ = std::move(handler);
  }

  /// Per-event handler override (registered by name).  Replaces any
  /// existing handler for `event_name`.
  void OnEvent(std::string event_name, EventCallback handler);

  /// Issue a DAP request.  `arguments` may be `nullptr`.  The
  /// response (success or failure) is delivered to `cb` when the
  /// adapter answers.
  std::int64_t Request(const std::string &command, Json arguments,
                       ResponseCallback cb = {});

  /// Feed raw bytes received from the adapter.  Decoded envelopes
  /// are routed to the registered response / event callbacks.
  void Receive(std::string_view bytes);

  /// Bytes currently buffered awaiting a complete frame.
  std::size_t buffered_bytes() const { return framer_.buffered_bytes(); }

  /// Sequence number of the next outbound request.  Useful for
  /// tests asserting on request ordering.
  std::int64_t next_seq() const { return next_seq_; }

 private:
  MessageFramer framer_;
  SendHandler send_;
  EventCallback event_handler_;
  std::map<std::string, EventCallback> event_handlers_;
  std::map<std::int64_t, std::pair<std::string, ResponseCallback>> pending_;
  std::int64_t next_seq_{1};
};

}  // namespace polyglot::tools::ui::dap
