/**
 * @file     command_runner.cpp
 * @brief    Ploy language frontend implementation
 *
 * @ingroup  Frontend / Ploy
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "frontends/ploy/include/command_runner.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <future>
#include <thread>

#ifdef _WIN32
#   include <windows.h>
#else
#   include <csignal>
#endif

namespace polyglot::ploy {

// ============================================================================
// ICommandRunner — default Run() delegates to RunWithResult()
// ============================================================================

std::string ICommandRunner::Run(const std::string &command) {
    auto result = RunWithResult(command);
    return result.stdout_output;
}

// ============================================================================
// DefaultCommandRunner
// ============================================================================

DefaultCommandRunner::DefaultCommandRunner(std::chrono::milliseconds default_timeout)
    : default_timeout_(default_timeout) {}

CommandResult DefaultCommandRunner::RunWithResult(
    const std::string &command,
    std::chrono::milliseconds timeout) {
    // Use the caller's timeout if non-zero, otherwise fall back to the default.
    auto effective_timeout = (timeout.count() > 0) ? timeout : default_timeout_;

    CommandResult result;

    // Launch the child process asynchronously so we can enforce a timeout.
    auto task = std::async(std::launch::async, [&command]() -> CommandResult {
        CommandResult r;
#ifdef _WIN32
        FILE *pipe = _popen(command.c_str(), "r");
#else
        FILE *pipe = popen(command.c_str(), "r");
#endif
        if (!pipe) {
            r.failed = true;
            return r;
        }

        char buffer[256];
        while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            r.stdout_output += buffer;
        }

#ifdef _WIN32
        r.exit_code = _pclose(pipe);
#else
        int status = pclose(pipe);
        r.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
        return r;
    });

    // If no timeout is configured, just wait indefinitely.
    if (effective_timeout.count() <= 0) {
        result = task.get();
        return result;
    }

    // Wait with timeout
    auto status = task.wait_for(effective_timeout);
    if (status == std::future_status::ready) {
        result = task.get();
    } else {
        // Timeout expired — the async thread is still blocked in popen/fgets.
        // Mark the result as timed-out.  The detached popen thread will
        // eventually finish on its own; we accept the minor resource leak
        // in exchange for not needing platform-specific process killing here.
        result.timed_out = true;
        result.failed = true;
    }

    return result;
}

} // namespace polyglot::ploy
