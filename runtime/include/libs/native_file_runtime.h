/**
 * @file     native_file_runtime.h
 * @brief    Self-contained native file/CSV integer runtime payloads.
 *
 * The compiler appends these bytes to a generated object's text section when
 * the lowered IR references one of the public C ABI symbols below.  The
 * payload contains no relocations and performs raw kernel system calls, so a
 * polyc/polyld build never has to invoke clang, CPython, libc, or a dynamic
 * loader to make file input work.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace polyglot::runtime {

inline constexpr const char kFileOpenReadSymbol[] = "polyrt_open_read";
inline constexpr const char kFileNextIntSymbol[] = "polyrt_read_i64_or";
inline constexpr const char kFileCloseReadSymbol[] = "polyrt_close_read";
inline constexpr const char kFileOpenWriteSymbol[] = "polyrt_open_write";
inline constexpr const char kFileWriteTextSymbol[] = "polyrt_write_text";
inline constexpr const char kFileWriteIntSymbol[] = "polyrt_write_i64";

/** One function exported from a NativeFileRuntimeBlob. */
struct NativeFileRuntimeSymbol {
  std::string name;
  std::size_t offset{0};
  std::size_t size{0};
};

/** Relocation-free text bytes plus their public symbol ranges. */
struct NativeFileRuntimeBlob {
  std::vector<std::uint8_t> text;
  std::vector<NativeFileRuntimeSymbol> symbols;
};

/**
 * Build the syscall-only runtime for a target.
 *
 * Supported today: x86_64 and ARM64 on Linux and Darwin/macOS.
 * ARM64 additionally exports bounded integer arrays and argument parsing.  `target_arch` and
 * `target_os` are case-insensitive and accept common aliases.  On an
 * unsupported target the returned blob is empty and `error` explains why;
 * callers must surface that as a compile error rather than emit unresolved
 * symbols.
 */
NativeFileRuntimeBlob BuildNativeFileRuntime(const std::string &target_arch,
                                             const std::string &target_os,
                                             std::string *error = nullptr);

/** True when `name` is one of the automatically linked runtime symbols. */
bool IsNativeFileRuntimeSymbol(const std::string &name);

} // namespace polyglot::runtime
