/**
 * @file     debug_session_test.cpp
 * @brief    End-to-end test for `DebugSession` against a mock DAP
 *           adapter (demand 2026-04-28-28 §5).
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include <map>

#include "tools/ui/common/dap/debug_session.h"

using namespace polyglot::tools::ui::dap;

namespace {

/// Tiny DAP server replacement.  Records every outbound request and
/// canned-replies them on demand.
struct MockAdapter {
  DapClient *client{nullptr};
  std::vector<Json> requests;

  void OnSent(const std::string &framed) {
    MessageFramer f;
    auto envs = f.Feed(framed);
    for (auto &e : envs) requests.push_back(std::move(e));
  }

  void Reply(std::int64_t request_seq, const std::string &command, Json body,
             bool success = true) {
    Json env;
    env["seq"] = 1000 + request_seq;
    env["type"] = "response";
    env["request_seq"] = request_seq;
    env["command"] = command;
    env["success"] = success;
    env["body"] = std::move(body);
    client->Receive(MessageFramer::Frame(env));
  }

  void Event(const std::string &name, Json body) {
    Json env;
    env["seq"] = 2000;
    env["type"] = "event";
    env["event"] = name;
    env["body"] = std::move(body);
    client->Receive(MessageFramer::Frame(env));
  }
};

}  // namespace

TEST_CASE("DebugSession runs through a full breakpoint hit cycle",
          "[polyui][dap][session]") {
  DapClient client;
  MockAdapter adapter;
  adapter.client = &client;
  client.SetSendHandler([&](const std::string &s) { adapter.OnSent(s); });
  DebugSession session(&client);

  // initialize → response → launch → setBreakpoints → configurationDone
  session.Initialize("polyui");
  REQUIRE(adapter.requests.size() == 1);
  CHECK(adapter.requests[0]["command"] == "initialize");
  adapter.Reply(adapter.requests[0]["seq"].get<std::int64_t>(), "initialize",
                Json::object());
  CHECK(session.initialized());

  session.Launch({{"program", "/repo/main.ploy"}});
  REQUIRE(adapter.requests.size() == 2);
  CHECK(adapter.requests[1]["command"] == "launch");

  session.SetBreakpoints("/repo/main.ploy", {SourceBreakpoint{10, std::nullopt,
                                                              std::nullopt,
                                                              std::nullopt}});
  REQUIRE(adapter.requests.size() == 3);
  CHECK(adapter.requests[2]["command"] == "setBreakpoints");
  CHECK(adapter.requests[2]["arguments"]["breakpoints"][0]["line"] == 10);

  session.ConfigurationDone();
  REQUIRE(adapter.requests.size() == 4);

  // Adapter sends `stopped` — DebugSession should auto-request
  // threads + stackTrace + scopes.
  adapter.Event("stopped", {{"reason", "breakpoint"}, {"threadId", 1}});
  CHECK(session.last_stop_reason() == StopReason::kBreakpoint);
  CHECK(session.stopped_thread() == 1);

  // DebugSession sent `threads` and `stackTrace` synchronously.
  REQUIRE(adapter.requests.size() >= 6);
  // Reply to threads.
  std::int64_t threads_seq = 0, stack_seq = 0;
  for (const auto &r : adapter.requests) {
    if (r["command"] == "threads")
      threads_seq = r["seq"].get<std::int64_t>();
    else if (r["command"] == "stackTrace")
      stack_seq = r["seq"].get<std::int64_t>();
  }
  REQUIRE(threads_seq != 0);
  REQUIRE(stack_seq != 0);
  adapter.Reply(threads_seq, "threads",
                {{"threads", Json::array({{{"id", 1}, {"name", "main"}}})}});
  CHECK(session.threads().size() == 1);
  CHECK(session.threads()[0].name == "main");

  Json frames = Json::array({{{"id", 1000},
                              {"name", "main"},
                              {"source", {{"path", "/repo/main.ploy"}}},
                              {"line", 10},
                              {"column", 1}}});
  adapter.Reply(stack_seq, "stackTrace", {{"stackFrames", frames}});
  REQUIRE(session.stack_frames().size() == 1);
  CHECK(session.stack_frames()[0].source_path == "/repo/main.ploy");
  CHECK(session.stack_frames()[0].line == 10);

  // After stackTrace succeeds, DebugSession requests scopes for
  // frame 0.
  std::int64_t scopes_seq = 0;
  for (const auto &r : adapter.requests) {
    if (r["command"] == "scopes") scopes_seq = r["seq"].get<std::int64_t>();
  }
  REQUIRE(scopes_seq != 0);
  adapter.Reply(scopes_seq, "scopes",
                {{"scopes", Json::array({{{"name", "Locals"},
                                          {"variablesReference", 42},
                                          {"expensive", false}}})}});
  REQUIRE(session.scopes().size() == 1);
  CHECK(session.scopes()[0].name == "Locals");
  CHECK(session.scopes()[0].variables_reference == 42);
}

TEST_CASE("DebugSession tracks output and exit/terminated events",
          "[polyui][dap][session]") {
  DapClient client;
  MockAdapter adapter;
  adapter.client = &client;
  client.SetSendHandler([&](const std::string &s) { adapter.OnSent(s); });
  DebugSession session(&client);

  adapter.Event("output", {{"category", "stdout"}, {"output", "hello\n"}});
  adapter.Event("output", {{"category", "stderr"}, {"output", "boom\n"}});
  REQUIRE(session.console().size() == 2);
  CHECK(session.console()[0].category == "stdout");
  CHECK(session.console()[1].output == "boom\n");

  adapter.Event("terminated", Json::object());
  CHECK(session.terminated());
}

TEST_CASE("DebugSession step and continue clear inline values",
          "[polyui][dap][session]") {
  DapClient client;
  MockAdapter adapter;
  adapter.client = &client;
  client.SetSendHandler([&](const std::string &s) { adapter.OnSent(s); });
  DebugSession session(&client);

  // The session carries no inline values until a frame populates
  // them; step/continue must remain idempotent on an empty map.
  session.Continue(1);
  session.Next(1);
  session.StepIn(1);
  session.StepOut(1);
  session.Pause(1);
  // Five execution-control requests plus zero responses required.
  REQUIRE(adapter.requests.size() == 5);
  CHECK(adapter.requests[0]["command"] == "continue");
  CHECK(adapter.requests[1]["command"] == "next");
  CHECK(adapter.requests[2]["command"] == "stepIn");
  CHECK(adapter.requests[3]["command"] == "stepOut");
  CHECK(adapter.requests[4]["command"] == "pause");
  CHECK(session.inline_values().empty());
}
