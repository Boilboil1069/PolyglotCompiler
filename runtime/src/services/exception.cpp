#include "runtime/include/services/exception.h"

#include <execinfo.h>
#include <cstdlib>
#include <sstream>

namespace polyglot::runtime::services {

std::vector<std::string> CaptureStackTrace(std::size_t max_frames) {
  std::vector<void *> buffer(max_frames);
  int captured = ::backtrace(buffer.data(), static_cast<int>(buffer.size()));
  char **symbols = ::backtrace_symbols(buffer.data(), captured);
  std::vector<std::string> frames;
  if (symbols) {
    for (int i = 0; i < captured; ++i) frames.emplace_back(symbols[i]);
    std::free(symbols);
  }
  return frames;
}

[[noreturn]] void ThrowRuntimeError(const std::string &message) {
  throw RuntimeError(message, CaptureStackTrace());
}

}  // namespace polyglot::runtime::services
