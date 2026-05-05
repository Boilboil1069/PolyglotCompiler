/**
 * @file     debug_session.cpp
 * @brief    Implementation of the DAP-backed debug session model.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/dap/debug_session.h"

namespace polyglot::tools::ui::dap {

namespace {

StopReason DecodeStopReason(const std::string &raw) {
  if (raw == "breakpoint") return StopReason::kBreakpoint;
  if (raw == "step") return StopReason::kStep;
  if (raw == "exception") return StopReason::kException;
  if (raw == "pause") return StopReason::kPause;
  if (raw == "entry") return StopReason::kEntry;
  return StopReason::kUnknown;
}

}  // namespace

DebugSession::DebugSession(DapClient *client) : client_(client) {
  client_->OnEvent("initialized", [this](const std::string &n, const Json &b) {
    HandleEvent(n, b);
  });
  client_->OnEvent("stopped", [this](const std::string &n, const Json &b) {
    HandleEvent(n, b);
  });
  client_->OnEvent("continued", [this](const std::string &n, const Json &b) {
    HandleEvent(n, b);
  });
  client_->OnEvent("terminated", [this](const std::string &n, const Json &b) {
    HandleEvent(n, b);
  });
  client_->OnEvent("exited", [this](const std::string &n, const Json &b) {
    HandleEvent(n, b);
  });
  client_->OnEvent("output", [this](const std::string &n, const Json &b) {
    HandleEvent(n, b);
  });
  client_->OnEvent("thread", [this](const std::string &n, const Json &b) {
    HandleEvent(n, b);
  });
  client_->OnEvent("breakpoint", [this](const std::string &n, const Json &b) {
    HandleEvent(n, b);
  });
}

void DebugSession::SetBreakpoints(const std::string &source_path,
                                  std::vector<SourceBreakpoint> bps) {
  breakpoints_[source_path] = bps;
  Json args;
  args["source"]["path"] = source_path;
  Json arr = Json::array();
  for (const auto &bp : bps) {
    Json j;
    j["line"] = bp.line;
    if (bp.condition) j["condition"] = *bp.condition;
    if (bp.hit_condition) j["hitCondition"] = *bp.hit_condition;
    if (bp.log_message) j["logMessage"] = *bp.log_message;
    arr.push_back(j);
  }
  args["breakpoints"] = arr;
  client_->Request(requests::kSetBreakpoints, std::move(args));
}

void DebugSession::SetExceptionBreakpoints(std::vector<std::string> filters) {
  Json args;
  args["filters"] = filters;
  client_->Request(requests::kSetExceptionBreakpoints, std::move(args));
}

void DebugSession::SetFunctionBreakpoints(std::vector<std::string> names) {
  Json args;
  Json arr = Json::array();
  for (const auto &n : names) arr.push_back({{"name", n}});
  args["breakpoints"] = arr;
  client_->Request(requests::kSetFunctionBreakpoints, std::move(args));
}

void DebugSession::Initialize(const std::string &client_id) {
  Json args;
  args["clientID"] = client_id;
  args["adapterID"] = "polyui";
  args["linesStartAt1"] = true;
  args["columnsStartAt1"] = true;
  args["pathFormat"] = "path";
  client_->Request(requests::kInitialize, std::move(args),
                   [this](const Response &r) {
                     if (r.success) initialized_ = true;
                   });
}

void DebugSession::Launch(Json arguments) {
  client_->Request(requests::kLaunch, std::move(arguments));
}

void DebugSession::Attach(Json arguments) {
  client_->Request(requests::kAttach, std::move(arguments));
}

void DebugSession::ConfigurationDone() {
  client_->Request(requests::kConfigurationDone, Json::object());
}

void DebugSession::Disconnect(bool terminate_debuggee) {
  Json args;
  args["terminateDebuggee"] = terminate_debuggee;
  client_->Request(requests::kDisconnect, std::move(args));
}

void DebugSession::Continue(std::int64_t thread_id) {
  Json args;
  args["threadId"] = thread_id;
  inline_values_.clear();
  client_->Request(requests::kContinue, std::move(args));
}

void DebugSession::Next(std::int64_t thread_id) {
  Json args;
  args["threadId"] = thread_id;
  inline_values_.clear();
  client_->Request(requests::kNext, std::move(args));
}

void DebugSession::StepIn(std::int64_t thread_id) {
  Json args;
  args["threadId"] = thread_id;
  inline_values_.clear();
  client_->Request(requests::kStepIn, std::move(args));
}

void DebugSession::StepOut(std::int64_t thread_id) {
  Json args;
  args["threadId"] = thread_id;
  inline_values_.clear();
  client_->Request(requests::kStepOut, std::move(args));
}

void DebugSession::Pause(std::int64_t thread_id) {
  Json args;
  args["threadId"] = thread_id;
  client_->Request(requests::kPause, std::move(args));
}

void DebugSession::HandleEvent(const std::string &name, const Json &body) {
  if (name == "stopped") {
    last_stop_ = DecodeStopReason(body.value("reason", std::string{}));
    if (body.contains("threadId") && body["threadId"].is_number_integer())
      stopped_thread_ = body["threadId"].get<std::int64_t>();
    // Request the thread/stack/scope/variable hierarchy synchronously
    // so the IDE has data to render before the user blinks.
    client_->Request(requests::kThreads, Json::object(),
                     [this](const Response &r) {
                       threads_.clear();
                       if (r.success && r.body.contains("threads") &&
                           r.body["threads"].is_array()) {
                         for (const auto &t : r.body["threads"]) {
                           ThreadInfo ti;
                           ti.id = t.value("id", static_cast<std::int64_t>(0));
                           ti.name = t.value("name", std::string{});
                           threads_.push_back(std::move(ti));
                         }
                       }
                     });
    Json st_args;
    st_args["threadId"] = stopped_thread_;
    st_args["startFrame"] = 0;
    st_args["levels"] = 20;
    client_->Request(requests::kStackTrace, std::move(st_args),
                     [this](const Response &r) {
                       frames_.clear();
                       if (r.success && r.body.contains("stackFrames") &&
                           r.body["stackFrames"].is_array()) {
                         for (const auto &f : r.body["stackFrames"]) {
                           StackFrame sf;
                           sf.id = f.value("id", static_cast<std::int64_t>(0));
                           sf.name = f.value("name", std::string{});
                           if (f.contains("source") && f["source"].is_object()) {
                             sf.source_path =
                                 f["source"].value("path", std::string{});
                           }
                           sf.line = f.value("line", 0u);
                           sf.column = f.value("column", 0u);
                           frames_.push_back(std::move(sf));
                         }
                       }
                       if (!frames_.empty()) {
                         Json sc;
                         sc["frameId"] = frames_.front().id;
                         client_->Request(requests::kScopes, std::move(sc),
                                          [this](const Response &sr) {
                                            scopes_.clear();
                                            if (!sr.success ||
                                                !sr.body.contains("scopes"))
                                              return;
                                            for (const auto &s :
                                                 sr.body["scopes"]) {
                                              Scope sp;
                                              sp.name = s.value("name", std::string{});
                                              sp.variables_reference = s.value(
                                                  "variablesReference",
                                                  static_cast<std::int64_t>(0));
                                              sp.expensive = s.value("expensive", false);
                                              scopes_.push_back(std::move(sp));
                                            }
                                          });
                       }
                     });
  } else if (name == "continued") {
    inline_values_.clear();
    last_stop_ = StopReason::kUnknown;
  } else if (name == "terminated" || name == "exited") {
    terminated_ = true;
    inline_values_.clear();
  } else if (name == "output") {
    OutputEntry e;
    e.category = body.value("category", std::string{"console"});
    e.output = body.value("output", std::string{});
    console_.push_back(std::move(e));
  } else if (name == "thread") {
    // Thread add/remove — refresh the cached list lazily.
    client_->Request(requests::kThreads, Json::object(),
                     [this](const Response &r) {
                       if (!r.success || !r.body.contains("threads")) return;
                       threads_.clear();
                       for (const auto &t : r.body["threads"]) {
                         ThreadInfo ti;
                         ti.id = t.value("id", static_cast<std::int64_t>(0));
                         ti.name = t.value("name", std::string{});
                         threads_.push_back(std::move(ti));
                       }
                     });
  } else if (name == "breakpoint") {
    // Adapter resolved or invalidated a breakpoint — UI redraws on
    // its own; nothing to mutate in the session model itself.
  } else if (name == "initialized") {
    // Adapter ready to accept breakpoint configuration.
  }
}

}  // namespace polyglot::tools::ui::dap
