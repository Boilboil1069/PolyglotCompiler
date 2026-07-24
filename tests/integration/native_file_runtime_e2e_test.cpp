/**
 * @file native_file_runtime_e2e_test.cpp
 * @brief polyc -> polyld -> live CSV input regression without host compilers.
 */

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(__x86_64__) && (defined(__APPLE__) || defined(__linux__))
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace fs = std::filesystem;

#if defined(__x86_64__) && (defined(__APPLE__) || defined(__linux__))
namespace {

int RunProcess(const std::vector<std::string> &arguments) {
  std::vector<char *> argv;
  argv.reserve(arguments.size() + 1);
  for (const auto &argument : arguments)
    argv.push_back(const_cast<char *>(argument.c_str()));
  argv.push_back(nullptr);
  pid_t child = 0;
  if (::posix_spawn(&child, argv.front(), nullptr, nullptr, argv.data(), environ) != 0)
    return -1;
  int wait_status = 0;
  if (::waitpid(child, &wait_status, 0) < 0 || !WIFEXITED(wait_status))
    return -1;
  return WEXITSTATUS(wait_status);
}

void WriteText(const fs::path &path, const std::string &text) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  REQUIRE(output.good());
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
  REQUIRE(output.good());
}

} // namespace
#endif

TEST_CASE("polyc executable reads real CSV through its automatically linked runtime",
          "[integration][native_file][exec]") {
#if defined(__x86_64__) && (defined(__APPLE__) || defined(__linux__))
  const fs::path repo = fs::path(POLYGLOT_TESTS_SAMPLES_ROOT).parent_path().parent_path();
  const fs::path polyc = repo / "build" / "polyc";
  REQUIRE(fs::exists(polyc));

  static unsigned fixture_id = 0;
  const fs::path scratch = fs::temp_directory_path() /
                           ("polyc_native_file_e2e_" + std::to_string(::getpid()) + "_" +
                            std::to_string(++fixture_id));
  std::error_code ignored;
  fs::create_directories(scratch, ignored);
  REQUIRE(fs::exists(scratch));
  const fs::path csv = scratch / "orders.csv";
  const fs::path source = scratch / "reader.poly";
  const fs::path executable = scratch / "reader";

  WriteText(csv, "order_id,amount,delta\n10,7,-3\n4,2,1\n");
  WriteText(source,
            "FUNC main() -> INT {\n"
            "  LET fd = file_open_ints(\"" + csv.string() + "\");\n"
            "  IF fd < 0 { RETURN 90; }\n"
            "  LET a = file_next_int(fd, -9999);\n"
            "  LET b = file_next_int(fd, -9999);\n"
            "  LET c = file_next_int(fd, -9999);\n"
            "  LET d = file_next_int(fd, -9999);\n"
            "  LET e = file_next_int(fd, -9999);\n"
            "  LET f = file_next_int(fd, -9999);\n"
            "  LET end = file_next_int(fd, -9999);\n"
            "  IF a != 10 { RETURN 101; }\n"
            "  IF b != 7 { RETURN 102; }\n"
            "  IF c != -3 { RETURN 103; }\n"
            "  IF d != 4 { RETURN 104; }\n"
            "  IF e != 2 { RETURN 105; }\n"
            "  IF f != 1 { RETURN 106; }\n"
            "  IF end != -9999 { RETURN 107; }\n"
            "  LET closed = file_close(fd);\n"
            "  IF closed != 0 { RETURN 108; }\n"
            "  RETURN a + b + c + d + e + f;\n"
            "}\n");

  // This invokes only the project's own polyc/polyld pair.  No clang,
  // CPython, or ecosystem compiler participates in the build.
  REQUIRE(RunProcess({polyc.string(), source.string(), "-o", executable.string(),
                      "--no-aux"}) == 0);
  REQUIRE(fs::exists(executable));
  REQUIRE(::chmod(executable.c_str(), 0755) == 0);
  CHECK(RunProcess({executable.string()}) == 21);

  // Mutate the external data after compilation.  The unchanged executable
  // must observe it and take a different business branch, proving that the
  // file was not replaced by a hard-coded compiler fixture.
  WriteText(csv, "order_id,amount,delta\n11,7,-3\n4,2,1\n");
  CHECK(RunProcess({executable.string()}) == 101);

  fs::remove_all(scratch, ignored);
#else
  SUCCEED("embedded file runtime execution currently requires x86_64 Linux/macOS");
#endif
}
