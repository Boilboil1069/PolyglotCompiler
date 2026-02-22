#include "frontends/ploy/include/command_runner.h"

#include <cstdio>
#include <cstring>

namespace polyglot::ploy {

// ============================================================================
// DefaultCommandRunner
// ============================================================================

std::string DefaultCommandRunner::Run(const std::string &command) {
    std::string output;
#ifdef _WIN32
    FILE *pipe = _popen(command.c_str(), "r");
#else
    FILE *pipe = popen(command.c_str(), "r");
#endif
    if (!pipe) {
        return output;
    }

    char buffer[256];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return output;
}

} // namespace polyglot::ploy
