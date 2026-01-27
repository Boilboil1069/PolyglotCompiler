#pragma once

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace polyglot::runtime::services {

class RuntimeError : public std::runtime_error {
 public:
  RuntimeError(const std::string &message, std::vector<std::string> stack)
      : std::runtime_error(message), stack_trace_(std::move(stack)) {}

  const std::vector<std::string> &StackTrace() const { return stack_trace_; }

 private:
  std::vector<std::string> stack_trace_;
};

std::vector<std::string> CaptureStackTrace(std::size_t max_frames = 32);
[[noreturn]] void ThrowRuntimeError(const std::string &message);

}  // namespace polyglot::runtime::services
