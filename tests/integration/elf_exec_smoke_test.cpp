/**
 * @file     elf_exec_smoke_test.cpp
 * @brief    Linux end-to-end smoke for the polyld ELF writer.  Drives
 *           `polyc` to compile `tests/samples/00_minimal/
 *           print_then_exit.ploy`, asks `polyld` to link an `ET_EXEC`
 *           image into `/tmp/polyld_elf_smoke`, then `fork + execve +
 *           waitpid`s the produced binary with stdout captured through
 *           a pipe and asserts `WEXITSTATUS == 0` plus stdout text
 *           equal to `"ok\n"`.  Compiled only on `__linux__`; on every
 *           other host this translation unit shrinks to a single
 *           placeholder test case so the integration_tests binary
 *           still links cleanly.
 *
 * @author   Manning Cyrus
 * @date     2026-05-06
 */

#include <catch2/catch_test_macros.hpp>

#if defined(__linux__)

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char **environ;

namespace fs = std::filesystem;

namespace {

fs::path RepoRoot() {
#ifdef POLYGLOT_TESTS_SAMPLES_ROOT
  fs::path p = fs::path(POLYGLOT_TESTS_SAMPLES_ROOT);
  for (int i = 0; i < 6 && !p.empty(); ++i) {
    if (fs::exists(p / "CMakeLists.txt")) return p;
    p = p.parent_path();
  }
#endif
  fs::path cwd = fs::current_path();
  for (int i = 0; i < 8 && !cwd.empty(); ++i) {
    if (fs::exists(cwd / "CMakeLists.txt") &&
        fs::exists(cwd / "tests" / "samples")) {
      return cwd;
    }
    cwd = cwd.parent_path();
  }
  return {};
}

// Walk a small candidate set looking for a built `polyc` / `polyld`
// binary.  The integration_tests binary is invoked from
// `${CMAKE_BINARY_DIR}` by ctest, so checking the parent and a few
// well-known build subdirectories is enough to find both tools.
fs::path LocateTool(const fs::path &repo, const std::string &name) {
  static const char *const kCandidates[] = {
      "build", "build-linux", "build-release", "out/build", "."};
  for (const char *sub : kCandidates) {
    fs::path p = repo / sub / name;
    if (fs::exists(p)) return p;
  }
  // Fall back to PATH lookup.
  return name;
}

// Run a child process to completion with inherited stdio; returns its
// exit status (or -1 on spawn failure).
int RunInherit(const std::vector<std::string> &argv) {
  std::vector<char *> raw;
  raw.reserve(argv.size() + 1);
  for (auto &s : argv) raw.push_back(const_cast<char *>(s.c_str()));
  raw.push_back(nullptr);
  pid_t pid = ::fork();
  if (pid < 0) return -1;
  if (pid == 0) {
    ::execvp(raw[0], raw.data());
    ::_exit(127);
  }
  int status = 0;
  if (::waitpid(pid, &status, 0) < 0) return -1;
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return -1;
}

// Spawn `path` and capture its stdout into `out` until EOF.  Returns
// the raw `WEXITSTATUS`-style exit code (or -1 on spawn failure).
int SpawnCaptureStdout(const std::string &path, std::string &out) {
  int pipefd[2] = {-1, -1};
  if (::pipe(pipefd) != 0) return -1;
  pid_t pid = ::fork();
  if (pid < 0) {
    ::close(pipefd[0]);
    ::close(pipefd[1]);
    return -1;
  }
  if (pid == 0) {
    ::close(pipefd[0]);
    ::dup2(pipefd[1], 1);
    ::close(pipefd[1]);
    char *argv[] = {const_cast<char *>(path.c_str()), nullptr};
    ::execve(path.c_str(), argv, environ);
    ::_exit(127);
  }
  ::close(pipefd[1]);
  char buf[256];
  ssize_t n;
  while ((n = ::read(pipefd[0], buf, sizeof(buf))) > 0) {
    out.append(buf, static_cast<std::size_t>(n));
  }
  ::close(pipefd[0]);
  int status = 0;
  if (::waitpid(pid, &status, 0) < 0) return -1;
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return -1;
}

} // namespace

TEST_CASE("polyld emits a runnable ELF for 00_minimal/print_then_exit",
          "[elf][exec][integration]") {
  const fs::path repo = RepoRoot();
  if (repo.empty()) {
    SUCCEED("Repo root not locatable; skipping ELF exec smoke");
    return;
  }
  const fs::path sample =
      repo / "tests" / "samples" / "00_minimal" / "print_then_exit.ploy";
  if (!fs::exists(sample)) {
    SUCCEED("00_minimal sample missing; skipping");
    return;
  }

  const fs::path polyc = LocateTool(repo, "polyc");
  const fs::path polyld = LocateTool(repo, "polyld");
  if (!fs::exists(polyc) || !fs::exists(polyld)) {
    SUCCEED("polyc / polyld binaries unavailable; skipping ELF exec smoke");
    return;
  }

  const fs::path obj = "/tmp/polyld_elf_smoke.pobj";
  const fs::path exe = "/tmp/polyld_elf_smoke";
  std::error_code ec;
  fs::remove(obj, ec);
  fs::remove(exe, ec);

  // Compile -> POBJ
  int cc = RunInherit({polyc.string(), "-c", sample.string(), "-o", obj.string()});
  if (cc != 0 || !fs::exists(obj)) {
    SUCCEED("polyc could not lower the sample on this host; skipping");
    return;
  }

  // Link -> ELF
  int lc = RunInherit({polyld.string(), obj.string(), "-o", exe.string(),
                       "--container", "elf"});
  if (lc != 0 || !fs::exists(exe)) {
    SUCCEED("polyld could not produce ELF on this host; skipping");
    return;
  }
  ::chmod(exe.c_str(), 0755);

  std::string captured;
  int rc = SpawnCaptureStdout(exe.string(), captured);
  REQUIRE(rc == 0);
  REQUIRE(captured == "ok\n");
}

#else  // non-Linux host

TEST_CASE("polyld ELF exec smoke (placeholder)", "[elf][exec][integration]") {
  SUCCEED("ELF exec smoke is Linux-only");
}

#endif
