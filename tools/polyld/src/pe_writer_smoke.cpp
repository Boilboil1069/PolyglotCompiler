/**
 * @file     pe_writer_smoke.cpp
 * @brief    Standalone harness: write a MinimalExitZero PE to disk and run it.
 *
 * Throwaway tool used during PE writer bring-up.  Not registered in CMake by
 * default; build manually via:
 *   cl /std:c++20 /EHsc /I . tools/polyld/src/pe_writer.cpp \
 *      tools/polyld/src/pe_writer_smoke.cpp /Fe:pe_smoke.exe
 *
 * @ingroup  Tool / polyld
 * @author   Manning Cyrus
 * @date     2026-04-28
 */

#include "tools/polyld/include/pe_writer.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>

int main() {
  using namespace polyglot::linker::pe;

  // -----------------------------------------------------------------------
  // Test 1: minimal ExitProcess(0) image.
  // -----------------------------------------------------------------------
  {
    BuildResult R = BuildMinimalExitZeroImage();
    if (R.image.empty()) {
      std::fprintf(stderr, "BuildMinimalExitZeroImage produced empty image\n");
      return 2;
    }
    const char *path = "pe_smoke_out.exe";
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char *>(R.image.data()),
            static_cast<std::streamsize>(R.image.size()));
    f.close();
    std::printf("[smoke] wrote %zu bytes to %s, entry_rva=0x%X\n", R.image.size(), path,
                R.entry_rva);
    for (const auto &kv : R.iat_slot_rva) {
      std::printf("[smoke]   IAT slot for %s @ RVA 0x%X\n", kv.first.c_str(), kv.second);
    }
    int rc = std::system(path);
    std::printf("[smoke] %s exit code = %d\n", path, rc);
    if (rc != 0)
      return rc;
  }

  // -----------------------------------------------------------------------
  // Test 2: hello-world image (must print the message and exit 0).
  // -----------------------------------------------------------------------
  {
    const std::string message = "Hello from PolyglotCompiler PE writer\r\n";
    BuildResult R = BuildHelloWorldPE(message);
    if (R.image.empty()) {
      std::fprintf(stderr, "BuildHelloWorldPE produced empty image\n");
      return 3;
    }
    const char *path = "pe_smoke_hello.exe";
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char *>(R.image.data()),
            static_cast<std::streamsize>(R.image.size()));
    f.close();
    std::printf("[smoke] wrote %zu bytes to %s, entry_rva=0x%X, rdata_rva=0x%X\n",
                R.image.size(), path, R.entry_rva, R.rdata_rva);
    for (const auto &kv : R.iat_slot_rva) {
      std::printf("[smoke]   IAT slot for %s @ RVA 0x%X\n", kv.first.c_str(), kv.second);
    }
    int rc = std::system(path);
    std::printf("[smoke] %s exit code = %d\n", path, rc);
    if (rc != 0)
      return rc;
  }

  return 0;
}
