#pragma once

#include <iostream>
#include <string>

namespace polyglot::utils {

enum class LogLevel { kInfo, kWarning, kError };

class Logger {
 public:
  void Log(LogLevel level, const std::string &message) const {
    switch (level) {
      case LogLevel::kInfo:
        std::cout << "[info] " << message << '\n';
        break;
      case LogLevel::kWarning:
        std::cout << "[warn] " << message << '\n';
        break;
      case LogLevel::kError:
        std::cerr << "[error] " << message << '\n';
        break;
    }
  }
};

}  // namespace polyglot::utils
