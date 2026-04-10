/**
 * @file     command_runner.h
 * @brief    Ploy language frontend
 *
 * @ingroup  Frontend / Ploy
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <chrono>
#include <string>

namespace polyglot::ploy {

// ============================================================================
// CommandResult — structured output from an external command execution
// ============================================================================

/** @brief CommandResult data structure. */
struct CommandResult {
    std::string stdout_output;   // Captured stdout text
    int exit_code{-1};           // Process exit code (0 = success, -1 = unknown)
    bool timed_out{false};       // True if the command was killed after timeout
    bool failed{false};          // True if the command could not be launched

    // Convenience: true when the command ran successfully (no timeout, exit 0).
    bool Ok() const { return !failed && !timed_out && exit_code == 0; }
};

// ============================================================================
// ICommandRunner — abstraction layer for external command execution
//
// The default implementation spawns a child process via _popen / popen and
// captures its stdout.  Tests can inject a mock runner to avoid touching the
// real filesystem or spawning processes.
// ============================================================================

/** @brief ICommandRunner class. */
class ICommandRunner {
  public:
    virtual ~ICommandRunner() = default;

    // Execute `command` and return its captured stdout.
    // Returns an empty string on failure (command not found, non-zero exit, …).
    // Kept for backward compatibility — delegates to RunWithResult().
    virtual std::string Run(const std::string &command);

    // Execute `command` with a timeout and return a structured result.
    // The default timeout of 0 means "use the runner's configured default".
    virtual CommandResult RunWithResult(
        const std::string &command,
        std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) = 0;
};

// ============================================================================
// DefaultCommandRunner — real popen/pclose implementation with timeout
//
// The runner spawns a child process and reads its stdout.  If the timeout
// expires the child is terminated and the result is marked as timed_out.
// ============================================================================

/** @brief DefaultCommandRunner class. */
class DefaultCommandRunner : public ICommandRunner {
  public:
    // Construct with a default timeout applied to every RunWithResult() call
    // that does not specify its own timeout.  Pass 0 to disable timeouts.
    explicit DefaultCommandRunner(
        std::chrono::milliseconds default_timeout = std::chrono::seconds{10});

    CommandResult RunWithResult(
        const std::string &command,
        std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) override;

  private:
    std::chrono::milliseconds default_timeout_;
};

} // namespace polyglot::ploy
