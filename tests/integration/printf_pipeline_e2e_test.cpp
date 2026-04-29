/**
 * @file     printf_pipeline_e2e_test.cpp
 * @brief    Compile a `.ploy` program with two `PRINTLN` calls, link it,
 *           execute the produced image with stdout captured into a file,
 *           and assert the captured bytes exactly equal the concatenation
 *           of the two message payloads.  This pins the entire frontend
 *           lowering -> backend emit -> packaging -> linker -> loader path
 *           that delivers user-visible stdout from a real PRINTLN.
 *
 * @ingroup  Tests / integration
 * @author   Manning Cyrus
 * @date     2026-04-29
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
namespace fs = std::filesystem;

namespace {

fs::path TestBinaryDir() {
    char buf[MAX_PATH];
    DWORD n = ::GetModuleFileNameA(nullptr, buf, MAX_PATH);
    REQUIRE(n > 0);
    REQUIRE(n < MAX_PATH);
    return fs::path(std::string(buf, n)).parent_path();
}

fs::path UniqueScratch(const std::string &stem) {
    static int counter = 0;
    char name[256];
    std::snprintf(name, sizeof(name), "polyglot_pe7_%s_%d_%d", stem.c_str(),
                  static_cast<int>(::_getpid()), ++counter);
    fs::path p = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::create_directories(p, ec);
    return p;
}

void WriteAll(const fs::path &path, const std::string &content) {
    std::ofstream f(path, std::ios::binary);
    REQUIRE(f.good());
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    REQUIRE(f.good());
}

std::string ReadAllBinary(const fs::path &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.good()) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int RunWrapped(const std::string &cmd) {
    return std::system(("\"" + cmd + "\"").c_str());
}

} // namespace

TEST_CASE("PE-7 end-to-end: two PRINTLN calls deliver concatenated bytes to stdout",
          "[printf][pe7][integration]") {
    const fs::path bin_dir = TestBinaryDir();
    const fs::path polyc  = bin_dir / "polyc.exe";
    const fs::path polyld = bin_dir / "polyld.exe";
    REQUIRE(fs::exists(polyc));
    REQUIRE(fs::exists(polyld));

    const fs::path scratch = UniqueScratch("printf_e2e");
    const fs::path src = scratch / "two_prints.ploy";
    const fs::path obj = scratch / "two_prints.obj";
    const fs::path exe = scratch / "two_prints.exe";
    const fs::path cap = scratch / "two_prints.stdout";

    // Two literal PRINTLN payloads at top-level (the .ploy script form
    // used throughout the sample matrix); the runtime lowering writes each
    // payload verbatim, so the captured stdout must equal their
    // concatenation.
    const std::string ploy =
        "PRINTLN \"alpha\\r\\n\";\n"
        "PRINTLN \"beta\\r\\n\";\n";
    WriteAll(src, ploy);

    const std::string compile_cmd =
        "\"" + polyc.string() + "\" \"" + src.string() +
        "\" --emit-obj=\"" + obj.string() + "\" --obj-format=coff";
    const int compile_rc = RunWrapped(compile_cmd);
    INFO("polyc exit: " << compile_rc);
    REQUIRE(fs::exists(obj));

    const std::string link_cmd =
        "\"" + polyld.string() + "\" \"" + obj.string() +
        "\" -o \"" + exe.string() + "\"";
    const int link_rc = RunWrapped(link_cmd);
    INFO("polyld exit: " << link_rc);
    REQUIRE(fs::exists(exe));

    // Outer quoted redirect: cmd /c strips one outer quote pair before
    // re-tokenising, so wrap the whole `"<exe>" > "<cap>"` in a second
    // pair to keep both the program path and the redirection target
    // quoted.
    const std::string run_cmd =
        "\"" + exe.string() + "\" > \"" + cap.string() + "\"";
    const int run_rc = RunWrapped(run_cmd);
    REQUIRE(run_rc == 0);

    const std::string captured = ReadAllBinary(cap);
    const std::string expected = "alpha\r\nbeta\r\n";

    std::error_code ec;
    fs::remove(exe, ec);
    fs::remove(cap, ec);

    REQUIRE(captured == expected);
}

#else // !_WIN32

#include <sys/wait.h>
#include <unistd.h>
namespace fs = std::filesystem;

namespace {

fs::path TestBinaryDirPosix() {
    char buf[4096];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return fs::current_path();
    buf[n] = '\0';
    return fs::path(buf).parent_path();
}

void WriteAllPosix(const fs::path &p, const std::string &c) {
    std::ofstream f(p, std::ios::binary);
    REQUIRE(f.good());
    f.write(c.data(), static_cast<std::streamsize>(c.size()));
}

std::string ReadAllPosix(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    if (!f.good()) return {};
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

// fork/pipe/dup2/exec/waitpid equivalent of the Win32 stdout redirection.
int RunCaptureStdout(const fs::path &exe, std::string *out_bytes) {
    int pipefd[2];
    REQUIRE(::pipe(pipefd) == 0);
    pid_t pid = ::fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        ::close(pipefd[0]);
        ::dup2(pipefd[1], STDOUT_FILENO);
        ::close(pipefd[1]);
        ::execl(exe.c_str(), exe.c_str(), static_cast<char *>(nullptr));
        ::_exit(127);
    }
    ::close(pipefd[1]);
    std::string buf;
    char tmp[4096];
    ssize_t r;
    while ((r = ::read(pipefd[0], tmp, sizeof(tmp))) > 0) {
        buf.append(tmp, static_cast<size_t>(r));
    }
    ::close(pipefd[0]);
    int status = 0;
    ::waitpid(pid, &status, 0);
    if (out_bytes) *out_bytes = std::move(buf);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

} // namespace

TEST_CASE("PE-7 end-to-end: two PRINTLN calls deliver concatenated bytes to stdout",
          "[printf][pe7][integration]") {
    const fs::path bin_dir = TestBinaryDirPosix();
    const fs::path polyc  = bin_dir / "polyc";
    const fs::path polyld = bin_dir / "polyld";
    REQUIRE(fs::exists(polyc));
    REQUIRE(fs::exists(polyld));

    const fs::path scratch = fs::temp_directory_path() / ("polyglot_pe7_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::create_directories(scratch, ec);

    const fs::path src = scratch / "two_prints.ploy";
    const fs::path obj = scratch / "two_prints.o";
    const fs::path exe = scratch / "two_prints.elf";
    const std::string ploy =
        "PRINTLN \"alpha\\r\\n\";\n"
        "PRINTLN \"beta\\r\\n\";\n";
    WriteAllPosix(src, ploy);

    const std::string compile_cmd =
        polyc.string() + " " + src.string() + " --emit-obj=" + obj.string();
    REQUIRE(std::system(compile_cmd.c_str()) == 0);
    REQUIRE(fs::exists(obj));

    const std::string link_cmd =
        polyld.string() + " " + obj.string() + " -o " + exe.string();
    REQUIRE(std::system(link_cmd.c_str()) == 0);
    REQUIRE(fs::exists(exe));

    std::string captured;
    int rc = RunCaptureStdout(exe, &captured);
    REQUIRE(rc == 0);
    REQUIRE(captured == "alpha\r\nbeta\r\n");
}

#endif // _WIN32
