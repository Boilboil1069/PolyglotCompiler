/**
 * @file     dap_client.cpp
 * @brief    Implementation of the DAP client (framing + RPC).
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/dap/dap_client.h"

#include <cctype>
#include <cstdlib>

namespace polyglot::tools::ui::dap {

namespace {

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

}  // namespace

std::vector<Json> MessageFramer::Feed(std::string_view bytes) {
  buffer_.append(bytes.data(), bytes.size());
  std::vector<Json> out;
  while (true) {
    // Find header terminator.
    auto sep = buffer_.find("\r\n\r\n");
    if (sep == std::string::npos) break;
    // Parse Content-Length.
    std::size_t content_length = 0;
    bool found_length = false;
    std::size_t cursor = 0;
    while (cursor < sep) {
      auto eol = buffer_.find("\r\n", cursor);
      if (eol == std::string::npos || eol > sep) eol = sep;
      std::string header = buffer_.substr(cursor, eol - cursor);
      auto colon = header.find(':');
      if (colon != std::string::npos) {
        std::string name = header.substr(0, colon);
        std::string value = header.substr(colon + 1);
        // Trim leading/trailing whitespace.
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
          value.erase(0, 1);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
          value.pop_back();
        if (name == "Content-Length") {
          content_length = static_cast<std::size_t>(std::strtoull(
              value.c_str(), nullptr, 10));
          found_length = true;
        }
      }
      cursor = eol + 2;
    }
    if (!found_length) {
      // Malformed frame — discard up to the separator and continue.
      buffer_.erase(0, sep + 4);
      continue;
    }
    std::size_t body_start = sep + 4;
    if (buffer_.size() < body_start + content_length) {
      // Wait for the rest of the body.
      break;
    }
    std::string body = buffer_.substr(body_start, content_length);
    buffer_.erase(0, body_start + content_length);
    try {
      out.push_back(Json::parse(body));
    } catch (const Json::parse_error &) {
      // Drop malformed payload.
    }
  }
  return out;
}

std::string MessageFramer::Frame(const Json &envelope) {
  std::string body = envelope.dump();
  std::string out;
  out.reserve(body.size() + 32);
  out += "Content-Length: ";
  out += std::to_string(body.size());
  out += "\r\n\r\n";
  out += body;
  return out;
}

DapClient::DapClient() = default;

void DapClient::OnEvent(std::string event_name, EventCallback handler) {
  event_handlers_[std::move(event_name)] = std::move(handler);
}

std::int64_t DapClient::Request(const std::string &command, Json arguments,
                                ResponseCallback cb) {
  std::int64_t seq = next_seq_++;
  Json envelope;
  envelope["seq"] = seq;
  envelope["type"] = "request";
  envelope["command"] = command;
  if (!arguments.is_null()) envelope["arguments"] = std::move(arguments);
  pending_[seq] = {command, std::move(cb)};
  if (send_) send_(MessageFramer::Frame(envelope));
  return seq;
}

void DapClient::Receive(std::string_view bytes) {
  for (const auto &env : framer_.Feed(bytes)) {
    if (!env.is_object()) continue;
    const std::string type = env.value("type", std::string{});
    if (type == "response") {
      Response r;
      r.request_seq = env.value("request_seq", static_cast<std::int64_t>(0));
      r.command = env.value("command", std::string{});
      r.success = env.value("success", false);
      if (env.contains("body")) r.body = env["body"];
      if (env.contains("message") && env["message"].is_string())
        r.error_message = env["message"].get<std::string>();
      else if (!r.success && r.body.is_object() && r.body.contains("error") &&
               r.body["error"].is_object() &&
               r.body["error"].contains("format"))
        r.error_message = r.body["error"]["format"].get<std::string>();
      auto it = pending_.find(r.request_seq);
      if (it != pending_.end()) {
        auto cb = std::move(it->second.second);
        pending_.erase(it);
        if (cb) cb(r);
      }
    } else if (type == "event") {
      const std::string event_name = env.value("event", std::string{});
      Json body = env.contains("body") ? env["body"] : Json::object();
      auto it = event_handlers_.find(event_name);
      if (it != event_handlers_.end()) {
        it->second(event_name, body);
      } else if (event_handler_) {
        event_handler_(event_name, body);
      }
    }
    // Reverse-direction `request`s (e.g. runInTerminal) are ignored
    // by this revision; the IDE shell can extend `event_handler_`-
    // style hooks if it ever needs to handle them.
  }
}

}  // namespace polyglot::tools::ui::dap
