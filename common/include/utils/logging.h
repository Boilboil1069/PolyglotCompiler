/**
 * @file     logging.h
 * @brief    Shared utility classes
 *
 * @ingroup  Common / Utils
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <iostream>
#include <string>

namespace polyglot::utils {

/** @brief LogLevel enumeration. */
enum class LogLevel { kInfo, kWarning, kError };

/** @brief Logger class. */
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
