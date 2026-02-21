#include "runtime/include/services/exception.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdlib>
#include <sstream>
#else
#include <execinfo.h>
#include <cstdlib>
#endif
#include <sstream>

namespace polyglot::runtime::services {

std::vector<std::string> CaptureStackTrace(std::size_t max_frames) {
#ifdef _WIN32
  std::vector<void *> buffer(max_frames);
  USHORT captured = ::CaptureStackBackTrace(
      1, static_cast<DWORD>(max_frames), buffer.data(), nullptr);
  std::vector<std::string> frames;
  for (USHORT i = 0; i < captured; ++i) {
    std::ostringstream oss;
    oss << "frame " << i << ": 0x" << std::hex << reinterpret_cast<uintptr_t>(buffer[i]);
    frames.push_back(oss.str());
  }
  return frames;
#else
  std::vector<void *> buffer(max_frames);
  int captured = ::backtrace(buffer.data(), static_cast<int>(buffer.size()));
  char **symbols = ::backtrace_symbols(buffer.data(), captured);
  std::vector<std::string> frames;
  if (symbols) {
    for (int i = 0; i < captured; ++i) frames.emplace_back(symbols[i]);
    std::free(symbols);
  }
  return frames;
#endif
}

[[noreturn]] void ThrowRuntimeError(const std::string &message) {
  throw RuntimeError(message, CaptureStackTrace());
}

}  // namespace polyglot::runtime::services
