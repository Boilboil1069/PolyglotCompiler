/**
 * @file     e2e_wasm_smoke.cpp
 * @brief    End-to-end smoke check for the wasm linker pipeline:
 *           build a single-module image and a two-module merge image,
 *           write them to disk, and (when `wasmtime` is on `PATH`)
 *           execute them to confirm the loader accepts the layout.
 *
 * @author   Manning Cyrus
 * @date     2026-05-06
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "tools/polyld/include/linker_wasm.h"

using namespace polyglot::linker::wasm;

namespace {

std::string TempPath(const char *tag) {
  std::string base = "/tmp/polyld_wasm_smoke_";
  base += std::to_string(static_cast<long long>(::getpid()));
  base += "_";
  base += tag;
  base += ".wasm";
  return base;
}

void WriteImage(const std::string &path, const std::vector<std::uint8_t> &b) {
  std::ofstream out(path, std::ios::binary);
  REQUIRE(out);
  out.write(reinterpret_cast<const char *>(b.data()),
            static_cast<std::streamsize>(b.size()));
  REQUIRE(out);
}

bool HaveWasmtime() {
  // POSIX-only: redirect stderr/stdout so the probe is silent.
  return std::system("command -v wasmtime > /dev/null 2>&1") == 0;
}

// `_start()` returning unit; body just executes `nop` then `end`.
Module BuildEmptyStartModule() {
  Module m;
  FuncType ft;  // () -> ()
  m.types.push_back(ft);
  Function f;
  f.type_index = 0;
  f.body = {0x00, 0x01, 0x0B};  // local_count=0, nop, end
  m.functions.push_back(std::move(f));
  Export e;
  e.name  = "_start";
  e.kind  = ExportKind::kFunc;
  e.index = 0;
  m.exports.push_back(e);
  return m;
}

}  // namespace

TEST_CASE("Single-module wasm round-trip writes a parseable file",
          "[bin6][wasm][e2e]") {
  Module m = BuildEmptyStartModule();
  auto bytes = EmitWasmModule(m);
  std::string path = TempPath("single");
  WriteImage(path, bytes);

  std::ifstream in(path, std::ios::binary);
  REQUIRE(in);
  std::vector<std::uint8_t> read((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
  REQUIRE(read == bytes);

  Module reparsed;
  std::string err;
  REQUIRE(ParseWasmModule(read, reparsed, err));
  REQUIRE(reparsed.exports.size() == 1);
  REQUIRE(reparsed.exports.front().name == "_start");

  if (HaveWasmtime()) {
    std::string cmd = "wasmtime run --invoke _start " + path + " > /dev/null 2>&1";
    int rc = std::system(cmd.c_str());
    // Only assert that the loader accepted the file (exit code 0).
    REQUIRE(rc == 0);
  }
  std::remove(path.c_str());
}

TEST_CASE("Two-module merge produces a runnable wasm file",
          "[bin6][wasm][e2e]") {
  // Module A: exports `add(i32,i32)->i32` returning constant 7.
  Module a;
  {
    FuncType ft;
    ft.params  = {ValType::kI32, ValType::kI32};
    ft.results = {ValType::kI32};
    a.types.push_back(ft);
    Function f;
    f.type_index = 0;
    f.body = {0x00, 0x41, 0x07, 0x0B};  // i32.const 7; end
    a.functions.push_back(std::move(f));
    Export e;
    e.name = "add"; e.kind = ExportKind::kFunc; e.index = 0;
    a.exports.push_back(e);
  }
  // Module B: imports `add`, exports `_start` that calls it and drops.
  Module b;
  {
    FuncType ft0;  // (i32,i32)->i32
    ft0.params  = {ValType::kI32, ValType::kI32};
    ft0.results = {ValType::kI32};
    FuncType ft1;  // ()->()
    b.types.push_back(ft0);
    b.types.push_back(ft1);
    Import im;
    im.module_name = "env"; im.name = "add"; im.kind = ImportKind::kFunc;
    im.type_index = 0;
    b.imports.push_back(im);
    Function f;
    f.type_index = 1;
    f.body = {0x00,                      // local_count = 0
              0x41, 0x01,                // i32.const 1
              0x41, 0x02,                // i32.const 2
              0x10, 0x00,                // call funcidx 0 (the import)
              0x1A,                      // drop
              0x0B};                     // end
    b.functions.push_back(std::move(f));
    Export e;
    e.name = "_start"; e.kind = ExportKind::kFunc; e.index = 1;
    b.exports.push_back(e);
  }

  Module merged;
  std::string err;
  REQUIRE(LinkWasmModules({a, b}, merged, err));
  auto bytes = EmitWasmModule(merged);
  std::string path = TempPath("merge");
  WriteImage(path, bytes);

  Module reparsed;
  std::string err2;
  REQUIRE(ParseWasmModule(bytes, reparsed, err2));
  REQUIRE(reparsed.imports.empty());
  REQUIRE(reparsed.functions.size() == 2);

  if (HaveWasmtime()) {
    std::string cmd = "wasmtime run --invoke _start " + path + " > /dev/null 2>&1";
    int rc = std::system(cmd.c_str());
    REQUIRE(rc == 0);
  }
  std::remove(path.c_str());
}
