/**
 * @file     repl_session.h
 * @brief    Multi-language REPL session abstraction.
 *
 * Wraps a long-lived child process (or in-process kernel) speaking
 * one of the built-in REPL protocols (`.ploy` via `polyc --repl`,
 * Python, IRust, IRB, dotnet-script).  The session keeps a
 * transcript of inputs and outputs so the Notebook view can replay
 * results across reloads.
 *
 * Engines are described declaratively (argv, prompt regex, exit
 * command).  The actual sub-process I/O is performed via a
 * pluggable `ReplTransport`, which makes the session unit-testable
 * with an in-process echo transport.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace polyglot::tools::ui::notebook {

enum class ReplEngine {
  kPloy,
  kPython,
  kIRust,
  kIRB,
  kDotnetScript,
};

struct ReplEngineSpec {
  ReplEngine engine;
  std::string display_name;
  std::vector<std::string> argv;     ///< Command line invocation.
  std::string prompt_regex;          ///< Used by transports to detect prompts.
  std::string exit_command;          ///< Sent on `Stop()`.
};

ReplEngineSpec DefaultSpec(ReplEngine e);

struct ReplTurn {
  std::string input;
  std::string stdout_text;
  std::string stderr_text;
  bool error{false};
  std::chrono::milliseconds duration{0};
};

/// Transport contract.  A real implementation spawns the engine
/// described by `spec` and writes/reads its standard streams.
class ReplTransport {
 public:
  virtual ~ReplTransport() = default;
  virtual bool Start(const ReplEngineSpec &spec) = 0;
  virtual ReplTurn Eval(const std::string &input) = 0;
  virtual void Stop() = 0;
  virtual bool running() const = 0;
};

class ReplSession {
 public:
  ReplSession(ReplEngineSpec spec,
              std::unique_ptr<ReplTransport> transport);
  ~ReplSession();

  bool Start();
  void Stop();

  const ReplEngineSpec &spec() const { return spec_; }
  bool running() const { return transport_ && transport_->running(); }

  /// Submit one statement / expression; appends a `ReplTurn` to the
  /// transcript and returns it.
  const ReplTurn &Eval(const std::string &input);

  const std::vector<ReplTurn> &transcript() const { return turns_; }
  void ClearTranscript() { turns_.clear(); }

 private:
  ReplEngineSpec spec_;
  std::unique_ptr<ReplTransport> transport_;
  std::vector<ReplTurn> turns_;
};

}  // namespace polyglot::tools::ui::notebook
