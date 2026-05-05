/**
 * @file     repl_session.cpp
 * @brief    Implementation of `repl_session.h`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/notebook/repl_session.h"

namespace polyglot::tools::ui::notebook {

ReplEngineSpec DefaultSpec(ReplEngine e) {
  ReplEngineSpec s;
  s.engine = e;
  switch (e) {
    case ReplEngine::kPloy:
      s.display_name = ".ploy";
      s.argv = {"polyc", "--repl"};
      s.prompt_regex = R"(^ploy>\s*$)";
      s.exit_command = ":quit";
      break;
    case ReplEngine::kPython:
      s.display_name = "Python";
      s.argv = {"python", "-iq"};
      s.prompt_regex = R"(^>>>\s*$)";
      s.exit_command = "exit()";
      break;
    case ReplEngine::kIRust:
      s.display_name = "IRust";
      s.argv = {"irust"};
      s.prompt_regex = R"(^In:\s*$)";
      s.exit_command = ":exit";
      break;
    case ReplEngine::kIRB:
      s.display_name = "IRB";
      s.argv = {"irb", "--simple-prompt"};
      s.prompt_regex = R"(^>>\s*$)";
      s.exit_command = "exit";
      break;
    case ReplEngine::kDotnetScript:
      s.display_name = "dotnet-script";
      s.argv = {"dotnet", "script"};
      s.prompt_regex = R"(^>\s*$)";
      s.exit_command = "#exit";
      break;
  }
  return s;
}

ReplSession::ReplSession(ReplEngineSpec spec,
                         std::unique_ptr<ReplTransport> transport)
    : spec_(std::move(spec)), transport_(std::move(transport)) {}

ReplSession::~ReplSession() {
  if (transport_ && transport_->running()) transport_->Stop();
}

bool ReplSession::Start() {
  if (!transport_) return false;
  return transport_->Start(spec_);
}

void ReplSession::Stop() {
  if (transport_ && transport_->running()) transport_->Stop();
}

const ReplTurn &ReplSession::Eval(const std::string &input) {
  ReplTurn t;
  if (!transport_ || !transport_->running()) {
    t.input = input;
    t.error = true;
    t.stderr_text = "REPL not running";
  } else {
    t = transport_->Eval(input);
    t.input = input;
  }
  turns_.push_back(std::move(t));
  return turns_.back();
}

}  // namespace polyglot::tools::ui::notebook
