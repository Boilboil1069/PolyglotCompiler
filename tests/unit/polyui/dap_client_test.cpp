/**
 * @file     dap_client_test.cpp
 * @brief    Unit tests for `MessageFramer` and `DapClient`.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/dap/dap_client.h"

using namespace polyglot::tools::ui::dap;

TEST_CASE("MessageFramer round-trips a single envelope", "[polyui][dap]") {
  Json env = {{"seq", 1}, {"type", "request"}, {"command", "initialize"}};
  std::string framed = MessageFramer::Frame(env);
  MessageFramer f;
  auto out = f.Feed(framed);
  REQUIRE(out.size() == 1);
  CHECK(out[0]["command"] == "initialize");
}

TEST_CASE("MessageFramer accumulates partial buffers", "[polyui][dap]") {
  Json env = {{"seq", 7}, {"type", "event"}, {"event", "stopped"},
              {"body", Json::object()}};
  std::string framed = MessageFramer::Frame(env);
  MessageFramer f;
  // Split mid-header.
  std::size_t mid = framed.size() / 2;
  auto first = f.Feed(framed.substr(0, mid));
  CHECK(first.empty());
  CHECK(f.buffered_bytes() > 0);
  auto rest = f.Feed(framed.substr(mid));
  REQUIRE(rest.size() == 1);
  CHECK(rest[0]["event"] == "stopped");
  CHECK(f.buffered_bytes() == 0);
}

TEST_CASE("MessageFramer decodes back-to-back frames", "[polyui][dap]") {
  std::string a =
      MessageFramer::Frame({{"seq", 1}, {"type", "event"}, {"event", "thread"},
                            {"body", Json::object()}});
  std::string b =
      MessageFramer::Frame({{"seq", 2}, {"type", "event"}, {"event", "output"},
                            {"body", {{"output", "hi"}}}});
  MessageFramer f;
  auto out = f.Feed(a + b);
  REQUIRE(out.size() == 2);
  CHECK(out[0]["event"] == "thread");
  CHECK(out[1]["event"] == "output");
}

TEST_CASE("DapClient routes responses back to their callbacks",
          "[polyui][dap]") {
  DapClient c;
  std::vector<std::string> sent;
  c.SetSendHandler([&](const std::string &s) { sent.push_back(s); });

  bool got = false;
  Response captured;
  c.Request("initialize", Json::object(),
            [&](const Response &r) { got = true; captured = r; });
  REQUIRE(sent.size() == 1);
  CHECK(sent[0].find("\"command\":\"initialize\"") != std::string::npos);

  // Simulate adapter response.
  Json reply = {{"seq", 100},
                {"type", "response"},
                {"request_seq", 1},
                {"command", "initialize"},
                {"success", true},
                {"body", {{"supportsConfigurationDoneRequest", true}}}};
  c.Receive(MessageFramer::Frame(reply));
  REQUIRE(got);
  CHECK(captured.success);
  CHECK(captured.command == "initialize");
  CHECK(captured.body["supportsConfigurationDoneRequest"] == true);
}

TEST_CASE("DapClient dispatches events through registered handlers",
          "[polyui][dap]") {
  DapClient c;
  std::string seen_event;
  c.OnEvent("stopped", [&](const std::string &n, const Json &) {
    seen_event = n;
  });
  Json evt = {{"seq", 5}, {"type", "event"}, {"event", "stopped"},
              {"body", {{"reason", "breakpoint"}}}};
  c.Receive(MessageFramer::Frame(evt));
  CHECK(seen_event == "stopped");
}

TEST_CASE("DapClient surfaces error responses", "[polyui][dap]") {
  DapClient c;
  c.SetSendHandler([](const std::string &) {});
  Response r;
  c.Request("launch", Json::object(), [&](const Response &x) { r = x; });
  Json err = {{"seq", 9},
              {"type", "response"},
              {"request_seq", 1},
              {"command", "launch"},
              {"success", false},
              {"message", "no program"}};
  c.Receive(MessageFramer::Frame(err));
  CHECK_FALSE(r.success);
  CHECK(r.error_message == "no program");
}
