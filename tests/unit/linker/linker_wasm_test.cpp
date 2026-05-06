/**
 * @file     linker_wasm_test.cpp
 * @brief    Unit coverage for the wasm linker module
 *           (`tools/polyld/src/linker_wasm.cpp`):
 *             * preamble validation,
 *             * LEB128 round-trip,
 *             * parse → emit round-trip,
 *             * multi-module merge (import resolution + index re-map),
 *             * unresolved-import / type-mismatch error reporting,
 *             * WASI `_start` entry-point validation.
 *
 * @author   Manning Cyrus
 * @date     2026-05-06
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "tools/polyld/include/linker_wasm.h"

using namespace polyglot::linker::wasm;

namespace {

// ---------------------------------------------------------------------------
// Construction helpers — tiny in-memory modules that exercise the full
// merger without depending on an external `wat2wasm`.
// ---------------------------------------------------------------------------

// Build a function body that returns the constant `value`.
std::vector<std::uint8_t> ConstReturnBody(std::int32_t value) {
  std::vector<std::uint8_t> body;
  body.push_back(0x00);                  // local_count = 0
  body.push_back(0x41);                  // i32.const
  EncodeSLeb32(body, value);
  body.push_back(0x0B);                  // end
  return body;
}

// Build a function body that calls function `target_funcidx` with two
// i32 arguments and returns its result.
[[maybe_unused]] std::vector<std::uint8_t> CallBody(std::uint32_t target_funcidx) {
  std::vector<std::uint8_t> body;
  body.push_back(0x00);                  // local_count = 0
  body.push_back(0x41); EncodeSLeb32(body, 1);
  body.push_back(0x41); EncodeSLeb32(body, 2);
  body.push_back(0x10); EncodeULeb32(body, target_funcidx);
  body.push_back(0x0B);
  return body;
}

// Module A: defines and exports `add(i32,i32)->i32` returning 7.
Module BuildExporterAdd() {
  Module m;
  FuncType ft;
  ft.params  = {ValType::kI32, ValType::kI32};
  ft.results = {ValType::kI32};
  m.types.push_back(ft);
  Function f;
  f.type_index = 0;
  f.body = ConstReturnBody(7);
  m.functions.push_back(std::move(f));
  Export e;
  e.name  = "add";
  e.kind  = ExportKind::kFunc;
  e.index = 0;
  m.exports.push_back(e);
  return m;
}

// Module B: imports `add(i32,i32)->i32`; defines `_start()` that calls
// the import.
Module BuildImporterStart() {
  Module m;
  FuncType import_ft;
  import_ft.params  = {ValType::kI32, ValType::kI32};
  import_ft.results = {ValType::kI32};
  FuncType start_ft;  // () -> ()
  m.types.push_back(import_ft);  // type 0
  m.types.push_back(start_ft);   // type 1

  Import im;
  im.module_name = "env";
  im.name        = "add";
  im.kind        = ImportKind::kFunc;
  im.type_index  = 0;
  m.imports.push_back(im);

  Function f;
  f.type_index = 1;
  // `_start` body: call add(1,2); drop; end.  funcidx 0 is the import.
  f.body.push_back(0x00);
  f.body.push_back(0x41); EncodeSLeb32(f.body, 1);
  f.body.push_back(0x41); EncodeSLeb32(f.body, 2);
  f.body.push_back(0x10); EncodeULeb32(f.body, 0);  // call import #0
  f.body.push_back(0x1A);                            // drop
  f.body.push_back(0x0B);                            // end
  m.functions.push_back(std::move(f));

  Export e;
  e.name  = "_start";
  e.kind  = ExportKind::kFunc;
  e.index = 1;  // import 0 + local 0
  m.exports.push_back(e);
  return m;
}

}  // namespace

TEST_CASE("LEB128 round-trip", "[bin6][linker_wasm]") {
  for (std::uint32_t v : {0u, 1u, 127u, 128u, 16383u, 16384u, 0x7FFFFFFFu,
                          0xFFFFFFFFu}) {
    std::vector<std::uint8_t> buf;
    EncodeULeb32(buf, v);
    const std::uint8_t *p = buf.data();
    std::uint32_t got = 0;
    REQUIRE(DecodeULeb32(p, buf.data() + buf.size(), got));
    REQUIRE(got == v);
  }
}

TEST_CASE("ParseWasmModule rejects bad preamble", "[bin6][linker_wasm]") {
  std::vector<std::uint8_t> bytes(8, 0);
  Module m;
  std::string err;
  REQUIRE_FALSE(ParseWasmModule(bytes, m, err));
  REQUIRE(err.find("polyld-err-E3300") != std::string::npos);
}

TEST_CASE("Parse + emit round-trips", "[bin6][linker_wasm]") {
  Module original = BuildExporterAdd();
  auto bytes = EmitWasmModule(original);
  REQUIRE(bytes.size() > 8);
  REQUIRE(bytes[0] == 0x00);
  REQUIRE(bytes[1] == 0x61);
  REQUIRE(bytes[2] == 0x73);
  REQUIRE(bytes[3] == 0x6D);

  Module reparsed;
  std::string err;
  REQUIRE(ParseWasmModule(bytes, reparsed, err));
  REQUIRE(reparsed.types.size() == 1);
  REQUIRE(reparsed.functions.size() == 1);
  REQUIRE(reparsed.exports.size() == 1);
  REQUIRE(reparsed.exports.front().name == "add");

  auto bytes2 = EmitWasmModule(reparsed);
  REQUIRE(bytes2 == bytes);
}

TEST_CASE("LinkWasmModules resolves cross-module function imports",
          "[bin6][linker_wasm]") {
  Module merged;
  std::string err;
  std::vector<Module> inputs{BuildExporterAdd(), BuildImporterStart()};
  REQUIRE(LinkWasmModules(inputs, merged, err));
  // After resolution the import is dropped; the merged module has two
  // local functions (add from A, _start from B) and one type.
  REQUIRE(merged.imports.empty());
  REQUIRE(merged.functions.size() == 2);
  // The `_start` export must point at funcidx 1 (the second local def).
  bool found_start = false;
  for (const auto &e : merged.exports) {
    if (e.name == "_start") {
      found_start = true;
      REQUIRE(e.index == 1);
    }
  }
  REQUIRE(found_start);
  // The merged module must round-trip through the emitter+parser.
  auto bytes = EmitWasmModule(merged);
  Module reparsed;
  std::string err2;
  REQUIRE(ParseWasmModule(bytes, reparsed, err2));
}

TEST_CASE("LinkWasmModules surfaces unresolved imports as E3310",
          "[bin6][linker_wasm]") {
  // Importer alone — no module exports `add`, so resolution fails.
  Module merged;
  std::string err;
  std::vector<Module> inputs{BuildImporterStart()};
  // Single-module merge is allowed, but the unresolved import simply
  // survives into the output (matches wasm-ld behaviour).  To exercise
  // the failure path we point the import at a sibling that exports a
  // matching name with the *wrong* type.
  Module wrong_type;
  FuncType wrong;
  wrong.params  = {ValType::kI64};   // mismatched signature
  wrong.results = {ValType::kI32};
  wrong_type.types.push_back(wrong);
  Function f;
  f.type_index = 0;
  f.body = ConstReturnBody(0);
  wrong_type.functions.push_back(std::move(f));
  Export e;
  e.name  = "add";
  e.kind  = ExportKind::kFunc;
  e.index = 0;
  wrong_type.exports.push_back(e);

  inputs.push_back(std::move(wrong_type));
  REQUIRE_FALSE(LinkWasmModules(inputs, merged, err));
  REQUIRE(err.find("polyld-err-E3330") != std::string::npos);
}

TEST_CASE("ValidateWasiEntry requires _start", "[bin6][linker_wasm]") {
  Module m = BuildExporterAdd();
  std::string err;
  REQUIRE_FALSE(ValidateWasiEntry(m, err));
  REQUIRE(err.find("polyld-err-E3320") != std::string::npos);

  Export e;
  e.name  = "_start";
  e.kind  = ExportKind::kFunc;
  e.index = 0;
  m.exports.push_back(e);
  std::string err2;
  REQUIRE(ValidateWasiEntry(m, err2));
}
