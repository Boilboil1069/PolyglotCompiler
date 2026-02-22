#include "runtime/include/services/exception.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdlib>
#include <sstream>
#elif defined(__APPLE__) || (defined(__linux__) && defined(__GLIBC__))
// execinfo.h is available on macOS and glibc-based Linux distributions.
// musl-based systems (e.g. Alpine Linux) do not provide it.
#define HAS_EXECINFO 1
#include <execinfo.h>
#include <cstdlib>
#else
// Fallback: no backtrace support on this platform.
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
#elif defined(HAS_EXECINFO)
  std::vector<void *> buffer(max_frames);
  int captured = ::backtrace(buffer.data(), static_cast<int>(buffer.size()));
  char **symbols = ::backtrace_symbols(buffer.data(), captured);
  std::vector<std::string> frames;
  if (symbols) {
    for (int i = 0; i < captured; ++i) frames.emplace_back(symbols[i]);
    std::free(symbols);
  }
  return frames;
#else
  // No backtrace support — return empty trace.
  (void)max_frames;
  return {};
#endif
}

[[noreturn]] void ThrowRuntimeError(const std::string &message) {
  throw RuntimeError(message, CaptureStackTrace());
}

}  // namespace polyglot::runtime::services
