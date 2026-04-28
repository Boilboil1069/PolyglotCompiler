/**
 * @file     emit_bitcode_roundtrip_test.cpp
 * @brief    Unit tests for the polyglot bitcode emission default path.
 *
 * Covers the contract delivered when ITargetBackend::EmitBitcode falls
 * through to the default implementation (i.e. a backend advertises
 * emits_bitcode but does not override the virtual): the call must produce
 * non-empty bytes, must serialise the input IRContext, must round-trip
 * back into an LTOModule that preserves function counts and names, and
 * must be byte-identical across every registered backend triple because
 * the default impl is shared.
 *
 * @ingroup  Tests / Backends / Common
 * @author   Manning Cyrus
 * @date     2026-04-28
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "backends/common/include/backend_registry.h"
#include "backends/common/include/target_backend.h"
#include "middle/include/ir/cfg.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/nodes/statements.h"
#include "middle/include/lto/link_time_optimizer.h"

using polyglot::backends::BackendRegistry;
using polyglot::backends::CompileResult;
using polyglot::backends::EmitKind;
using polyglot::backends::TargetOptions;

namespace {

std::string BytesToString(const std::vector<std::uint8_t>& bytes) {
  return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

}  // namespace

TEST_CASE("EmitBitcode default produces a valid header for an empty IRContext",
          "[backends][bitcode]") {
  auto* backend = BackendRegistry::Instance().Find("x86_64-unknown-elf");
  REQUIRE(backend != nullptr);

  polyglot::ir::IRContext module;
  TargetOptions options;
  options.emit = EmitKind::kBitcode;

  CompileResult result = backend->EmitBitcode(module, options);
  REQUIRE(result.ok);
  REQUIRE(result.diagnostics.empty());
  REQUIRE_FALSE(result.artifacts.bitcode_bytes.empty());

  const std::string text = BytesToString(result.artifacts.bitcode_bytes);
  // Header must begin with the literal "module " token.
  REQUIRE(text.rfind("module ", 0) == 0);

  polyglot::lto::LTOModule reloaded;
  REQUIRE(reloaded.DeserializeBitcode(text));
  REQUIRE(reloaded.functions.empty());
  REQUIRE(reloaded.globals.empty());
}

TEST_CASE("EmitBitcode default is byte-identical across all registered backends",
          "[backends][bitcode]") {
  polyglot::ir::IRContext module;
  module.CreateFunction("f0");
  module.CreateFunction("f1");
  module.CreateFunction("f2");

  TargetOptions options;
  options.emit = EmitKind::kBitcode;

  // Note: the default emitter uses TargetTriple() as the bitcode module
  // name, so the streams legitimately differ on that single token.  The
  // post-header payload (function table) must match exactly across triples.
  std::vector<std::string> payloads;
  for (const char* triple : {"x86_64-unknown-elf", "aarch64-unknown-elf",
                             "wasm32-unknown-unknown"}) {
    auto* backend = BackendRegistry::Instance().Find(triple);
    REQUIRE(backend != nullptr);
    CompileResult result = backend->EmitBitcode(module, options);
    REQUIRE(result.ok);
    const std::string text = BytesToString(result.artifacts.bitcode_bytes);
    // Strip the header line ("module <triple>\n") so we compare payloads.
    const auto nl = text.find('\n');
    REQUIRE(nl != std::string::npos);
    payloads.push_back(text.substr(nl + 1));
  }
  REQUIRE(payloads.size() == 3);
  REQUIRE(payloads[0] == payloads[1]);
  REQUIRE(payloads[1] == payloads[2]);
}

TEST_CASE("EmitBitcode default preserves block and instruction topology",
          "[backends][bitcode]") {
  auto* backend = BackendRegistry::Instance().Find("x86_64-unknown-elf");
  REQUIRE(backend != nullptr);

  polyglot::ir::IRContext module;
  auto fn = module.CreateFunction("ret_zero");
  REQUIRE(fn != nullptr);

  // Append an entry block with a single Return statement so the bitcode
  // payload genuinely round-trips a non-trivial function body.
  auto entry = std::make_shared<polyglot::ir::BasicBlock>();
  entry->name = "entry";
  auto ret = std::make_shared<polyglot::ir::Instruction>();
  ret->name = "ret_inst";
  ret->type = polyglot::ir::IRType();
  ret->operands.push_back("0");
  entry->instructions.push_back(ret);
  fn->blocks.push_back(entry);

  TargetOptions options;
  options.emit = EmitKind::kBitcode;
  CompileResult result = backend->EmitBitcode(module, options);
  REQUIRE(result.ok);
  REQUIRE_FALSE(result.artifacts.bitcode_bytes.empty());

  polyglot::lto::LTOModule reloaded;
  REQUIRE(reloaded.DeserializeBitcode(BytesToString(result.artifacts.bitcode_bytes)));
  REQUIRE(reloaded.functions.size() == 1);
  REQUIRE(reloaded.functions[0].name == "ret_zero");
  REQUIRE(reloaded.functions[0].blocks.size() == 1);
  REQUIRE(reloaded.functions[0].blocks[0]->name == "entry");
  REQUIRE(reloaded.functions[0].blocks[0]->instructions.size() == 1);
  REQUIRE(reloaded.functions[0].blocks[0]->instructions[0]->operands.size() == 1);
  REQUIRE(reloaded.functions[0].blocks[0]->instructions[0]->operands[0] == "0");

  // entry_points must include the function we created (FromIRContext rule).
  REQUIRE(reloaded.entry_points.count("ret_zero") == 1);
}
