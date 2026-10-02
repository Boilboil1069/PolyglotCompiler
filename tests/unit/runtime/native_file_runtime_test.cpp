/**
 * @file native_file_runtime_test.cpp
 * @brief Unit and host-execution coverage for the embedded file runtime.
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

#include "runtime/include/libs/native_file_runtime.h"

#if (defined(__x86_64__) || defined(__aarch64__)) && (defined(__APPLE__) || defined(__linux__))
#include <sys/mman.h>
#include <unistd.h>
#endif

using namespace polyglot::runtime;

namespace {
#if defined(__aarch64__)
constexpr const char *kHostArch = "arm64";
#else
constexpr const char *kHostArch = "x86_64";
#endif

const NativeFileRuntimeSymbol &FindRuntimeSymbol(const NativeFileRuntimeBlob &blob,
                                                 const std::string &name) {
  const auto found = std::find_if(blob.symbols.begin(), blob.symbols.end(),
                                  [&](const auto &symbol) { return symbol.name == name; });
  REQUIRE(found != blob.symbols.end());
  return *found;
}

} // namespace

TEST_CASE("native file runtime emits six bounded x86_64 symbols",
          "[runtime][native_file]") {
  for (const std::string os : {"darwin", "linux"}) {
    INFO("target OS: " << os);
    std::string error;
    const auto blob = BuildNativeFileRuntime("x86_64", os, &error);
    REQUIRE(error.empty());
    REQUIRE_FALSE(blob.text.empty());
    REQUIRE(blob.symbols.size() == 6);
    for (const auto &symbol : blob.symbols) {
      REQUIRE(symbol.size > 0);
      REQUIRE(symbol.offset <= blob.text.size());
      REQUIRE(symbol.size <= blob.text.size() - symbol.offset);
      REQUIRE(IsNativeFileRuntimeSymbol(symbol.name));
    }
    CHECK(FindRuntimeSymbol(blob, kFileOpenReadSymbol).offset == 0);
  }

  std::string error;
  CHECK(BuildNativeFileRuntime("riscv64", "darwin", &error).text.empty());
  CHECK(error.find("not available") != std::string::npos);
}

#if (defined(__x86_64__) || defined(__aarch64__)) && (defined(__APPLE__) || defined(__linux__))

TEST_CASE("native file runtime reads signed integers from real CSV bytes",
          "[runtime][native_file][exec]") {
#if defined(__APPLE__)
  const char *target_os = "darwin";
#else
  const char *target_os = "linux";
#endif
  std::string error;
  const auto blob = BuildNativeFileRuntime(kHostArch, target_os, &error);
  REQUIRE(error.empty());
  REQUIRE_FALSE(blob.text.empty());

#if defined(__APPLE__)
  constexpr int kAnonymousMap = MAP_ANON;
#else
  constexpr int kAnonymousMap = MAP_ANONYMOUS;
#endif
  void *mapping = ::mmap(nullptr, blob.text.size(), PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | kAnonymousMap, -1, 0);
  REQUIRE(mapping != MAP_FAILED);
  std::copy(blob.text.begin(), blob.text.end(), static_cast<std::uint8_t *>(mapping));
  __builtin___clear_cache(static_cast<char *>(mapping), static_cast<char *>(mapping) + blob.text.size());
  REQUIRE(::mprotect(mapping, blob.text.size(), PROT_READ | PROT_EXEC) == 0);

  using OpenFn = long (*)(const char *);
  using NextFn = long (*)(long, long);
  using CloseFn = long (*)(long);
  auto *base = static_cast<std::uint8_t *>(mapping);
  const auto open = reinterpret_cast<OpenFn>(
      base + FindRuntimeSymbol(blob, kFileOpenReadSymbol).offset);
  const auto next = reinterpret_cast<NextFn>(
      base + FindRuntimeSymbol(blob, kFileNextIntSymbol).offset);
  const auto close = reinterpret_cast<CloseFn>(
      base + FindRuntimeSymbol(blob, kFileCloseReadSymbol).offset);

  static unsigned fixture_id = 0;
  const auto path = std::filesystem::temp_directory_path() /
                    ("polyrt_native_csv_" + std::to_string(::getpid()) + "_" +
                     std::to_string(++fixture_id) + ".csv");
  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.good());
    output << "order_id,amount,delta\n10,7,-3\n4,2,1\n";
    REQUIRE(output.good());
  }

  const long fd = open(path.c_str());
  REQUIRE(fd >= 0);
  CHECK(next(fd, -9999) == 10);
  CHECK(next(fd, -9999) == 7);
  CHECK(next(fd, -9999) == -3);
  CHECK(next(fd, -9999) == 4);
  CHECK(next(fd, -9999) == 2);
  CHECK(next(fd, -9999) == 1);
  CHECK(next(fd, -9999) == -9999);
  CHECK(close(fd) == 0);

  CHECK(::munmap(mapping, blob.text.size()) == 0);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

TEST_CASE("native file runtime writes and reads back text and signed integers",
          "[runtime][native_file][exec][write]") {
#if defined(__APPLE__)
  const char *target_os = "darwin";
  constexpr int kAnonymousMap = MAP_ANON;
#else
  const char *target_os = "linux";
  constexpr int kAnonymousMap = MAP_ANONYMOUS;
#endif
  std::string error;
  const auto blob = BuildNativeFileRuntime(kHostArch, target_os, &error);
  REQUIRE(error.empty());
  REQUIRE_FALSE(blob.text.empty());

  void *mapping = ::mmap(nullptr, blob.text.size(), PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | kAnonymousMap, -1, 0);
  REQUIRE(mapping != MAP_FAILED);
  std::copy(blob.text.begin(), blob.text.end(), static_cast<std::uint8_t *>(mapping));
  __builtin___clear_cache(static_cast<char *>(mapping), static_cast<char *>(mapping) + blob.text.size());
  REQUIRE(::mprotect(mapping, blob.text.size(), PROT_READ | PROT_EXEC) == 0);

  using OpenFn = long (*)(const char *);
  using NextFn = long (*)(long, long);
  using CloseFn = long (*)(long);
  using WriteTextFn = long (*)(long, const char *);
  using WriteIntFn = long (*)(long, long, long);
  auto *base = static_cast<std::uint8_t *>(mapping);
  const auto open_read = reinterpret_cast<OpenFn>(
      base + FindRuntimeSymbol(blob, kFileOpenReadSymbol).offset);
  const auto next = reinterpret_cast<NextFn>(
      base + FindRuntimeSymbol(blob, kFileNextIntSymbol).offset);
  const auto close = reinterpret_cast<CloseFn>(
      base + FindRuntimeSymbol(blob, kFileCloseReadSymbol).offset);
  const auto open_write = reinterpret_cast<OpenFn>(
      base + FindRuntimeSymbol(blob, kFileOpenWriteSymbol).offset);
  const auto write_text = reinterpret_cast<WriteTextFn>(
      base + FindRuntimeSymbol(blob, kFileWriteTextSymbol).offset);
  const auto write_int = reinterpret_cast<WriteIntFn>(
      base + FindRuntimeSymbol(blob, kFileWriteIntSymbol).offset);

  static unsigned fixture_id = 0;
  const auto path = std::filesystem::temp_directory_path() /
                    ("polyrt_native_write_" + std::to_string(::getpid()) + "_" +
                     std::to_string(++fixture_id) + ".txt");
  // Prove that polyrt_open_write truncates rather than appends.
  {
    std::ofstream stale(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stale.good());
    stale << "stale bytes that must disappear";
  }

  const long fd = open_write(path.c_str());
  REQUIRE(fd >= 0);
  CHECK(write_text(fd, nullptr) == -1);
  CHECK(write_text(fd, "model=") == 6);
  CHECK(write_int(fd, -42, ',') == 4);
  CHECK(write_int(fd, 0, '\n') == 2);
  const long minimum = std::numeric_limits<long>::min();
  const std::string minimum_text = std::to_string(minimum);
  CHECK(write_int(fd, minimum, 0) == static_cast<long>(minimum_text.size()));
  REQUIRE(close(fd) == 0);

  const std::string expected = "model=-42,0\n" + minimum_text;
  {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    const std::string actual((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    CHECK(actual == expected);
  }

  const long read_fd = open_read(path.c_str());
  REQUIRE(read_fd >= 0);
  CHECK(next(read_fd, 777) == -42);
  CHECK(next(read_fd, 777) == 0);
  CHECK(next(read_fd, 777) == minimum);
  CHECK(next(read_fd, 777) == 777);
  CHECK(close(read_fd) == 0);

  CHECK(::munmap(mapping, blob.text.size()) == 0);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

#else

TEST_CASE("native file runtime execution requires an x86_64 or ARM64 Unix host",
          "[runtime][native_file][exec]") {
  SUCCEED("host cannot execute the currently supported embedded runtime");
}

#endif

#if defined(__aarch64__) && (defined(__APPLE__) || defined(__linux__))
TEST_CASE("ARM64 native runtime bounds arrays and parses full signed arguments", "[runtime][native_file][exec][args][array]") {
#if defined(__APPLE__)
  const auto blob = BuildNativeFileRuntime("arm64", "darwin");
#else
  const auto blob = BuildNativeFileRuntime("arm64", "linux");
#endif
  void *mapping = ::mmap(nullptr, blob.text.size(), PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANON, -1, 0);
  REQUIRE(mapping != MAP_FAILED);
  std::copy(blob.text.begin(), blob.text.end(), static_cast<std::uint8_t *>(mapping));
  __builtin___clear_cache(static_cast<char *>(mapping), static_cast<char *>(mapping) + blob.text.size());
  REQUIRE(::mprotect(mapping, blob.text.size(), PROT_READ | PROT_EXEC) == 0);
  auto address = [&](const char *name) { return static_cast<char *>(mapping) + FindRuntimeSymbol(blob, name).offset; };
  auto alloc = reinterpret_cast<long (*)(long)>(address("polyrt_array_new"));
  auto length = reinterpret_cast<long (*)(long)>(address("polyrt_array_len"));
  auto get = reinterpret_cast<long (*)(long,long,long)>(address("polyrt_array_get"));
  auto set = reinterpret_cast<long (*)(long,long,long)>(address("polyrt_array_set"));
  auto free = reinterpret_cast<long (*)(long)>(address("polyrt_array_free"));
  CHECK(alloc(-1) == 0);
  CHECK(alloc(1LL << 40) == 0);
  CHECK(length(0) == 0);
  CHECK(free(0) == 0);
  const long a = alloc(4);
  REQUIRE(a != 0);
  CHECK(length(a) == 4);
  CHECK(get(a, 0, -1) == 0);
  CHECK(set(a, 3, std::numeric_limits<long>::min()) == 0);
  CHECK(get(a, 3, -1) == std::numeric_limits<long>::min());
  CHECK(set(a, -1, 42) == -1);
  CHECK(set(a, 4, 42) == -1);
  CHECK(get(a, 4, 888) == 888);
  CHECK(free(a) == 0);
  const char *args[] = {"program", "-9223372036854775808", "9223372036854775807",
                       "9223372036854775808", "-9223372036854775809", "12x", "", "+42"};
  auto integer = reinterpret_cast<long (*)(long,const char **,long,long)>(address("polyrt_arg_int"));
  auto text = reinterpret_cast<const char *(*)(long,const char **,long)>(address("polyrt_arg_text"));
  CHECK(integer(8,args,1,777) == std::numeric_limits<long>::min());
  CHECK(integer(8,args,2,777) == std::numeric_limits<long>::max());
  for (long i : {3,4,5,6,-1,8}) CHECK(integer(8,args,i,777) == 777);
  CHECK(integer(8,args,7,777) == 42);
  CHECK(std::string(text(8,args,0)) == "program");
  CHECK(std::string(text(8,args,-1)).empty());
  CHECK(std::string(text(8,nullptr,0)).empty());
  CHECK(::munmap(mapping,blob.text.size()) == 0);
}
#endif
