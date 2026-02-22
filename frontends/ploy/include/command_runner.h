#pragma once

#include <string>

namespace polyglot::ploy {

// ============================================================================
// ICommandRunner — abstraction layer for external command execution
//
// The default implementation spawns a child process via _popen / popen and
// captures its stdout.  Tests can inject a mock runner to avoid touching the
// real filesystem or spawning processes.
// ============================================================================

class ICommandRunner {
  public:
    virtual ~ICommandRunner() = default;

    // Execute `command` and return its captured stdout.
    // Returns an empty string on failure (command not found, non-zero exit, …).
    virtual std::string Run(const std::string &command) = 0;
};

// ============================================================================
// DefaultCommandRunner — real popen/pclose implementation
// ============================================================================

class DefaultCommandRunner : public ICommandRunner {
  public:
    std::string Run(const std::string &command) override;
};

} // namespace polyglot::ploy
