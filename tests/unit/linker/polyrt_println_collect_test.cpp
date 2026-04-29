/**
 * @file     polyrt_println_collect_test.cpp
 * @brief    Unit tests for the polyrt_println call-site recovery analysis
 *           pass that turns linker-loaded ObjectFile state into the message
 *           vector consumed by `pe::BuildPrintlnSequencePE`.
 *
 * @ingroup  Tests / linker
 * @author   Manning Cyrus
 * @date     2026-04-29
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "tools/polyld/include/linker.h"

using namespace polyglot::linker;

namespace {

// ---------------------------------------------------------------------------
// Builders for in-memory ObjectFile fixtures.
//
// The recovery pass is a pure analysis over `std::vector<ObjectFile>`; each
// test below assembles the minimum-viable shape — one or more `.text`
// sections carrying the `(message-load, polyrt_println-call)` reloc pairs
// produced by the IR-layer interner, plus a `.rdata` section holding the
// raw decoded message bytes — and asserts that the recovered sequence
// matches the IR-call order, including the duplicate cases that exercise
// the linker's faithfulness to the IR interner contract.
// ---------------------------------------------------------------------------

// Append a defined data symbol pointing at `payload_offset .. +payload.size()`
// inside the supplied data section.  Returns the symbol's name.
std::string AppendDataSymbol(ObjectFile &obj, int data_section_index,
                             const std::string &name, std::size_t payload_offset,
                             std::size_t payload_size) {
  Symbol sym;
  sym.name = name;
  sym.section_index = data_section_index;
  sym.section = obj.sections[static_cast<std::size_t>(data_section_index)].name;
  sym.offset = payload_offset;
  sym.size = payload_size;
  sym.is_defined = true;
  sym.binding = SymbolBinding::kGlobal;
  sym.type = SymbolType::kObject;
  obj.symbols.push_back(sym);
  return name;
}

// Append a (lea-message, call-polyrt_println) reloc pair to `text_section`.
// The actual relocation offsets only need to be ordered so the analysis
// pass walks them in instruction order — the byte payload of `.text` is
// irrelevant to the analysis (the relocs alone describe the call shape).
void AppendPrintlnCall(InputSection &text_section, std::uint64_t lea_offset,
                       const std::string &message_symbol, std::uint64_t call_offset) {
  Relocation lea_reloc;
  lea_reloc.offset = lea_offset;
  lea_reloc.symbol = message_symbol;
  lea_reloc.is_pc_relative = true;
  lea_reloc.size = 4;
  text_section.relocations.push_back(lea_reloc);

  Relocation call_reloc;
  call_reloc.offset = call_offset;
  call_reloc.symbol = "polyrt_println";
  call_reloc.is_pc_relative = true;
  call_reloc.size = 4;
  text_section.relocations.push_back(call_reloc);
}

// Build a single-object fixture with the given IR-style messages.  Each
// message is interned exactly once into the object's `.rdata`; duplicates
// in `interned_payloads_in_call_order` are folded just like the IR layer
// does.  The .text relocs are emitted in call order.
ObjectFile MakeSingleObjectFixture(const std::vector<std::string> &calls_in_order) {
  ObjectFile obj;
  obj.path = "<in-memory>";
  obj.format = ObjectFormat::kPOBJ;
  obj.is_64bit = true;
  obj.is_little_endian = true;

  // Section 0 — .text (executable).
  InputSection text_section;
  text_section.name = ".text";
  text_section.flags = SectionFlags::kAlloc | SectionFlags::kExecInstr;
  text_section.type = SectionType::kProgbits;

  // Section 1 — .rdata (read-only data, holds the interned payloads).
  InputSection rdata_section;
  rdata_section.name = ".rdata";
  rdata_section.flags = SectionFlags::kAlloc;
  rdata_section.type = SectionType::kProgbits;

  obj.sections.push_back(std::move(text_section));
  obj.sections.push_back(std::move(rdata_section));
  constexpr int kRdataIndex = 1;

  // Intern unique payloads into .rdata and remember each's symbol name.
  std::vector<std::pair<std::string, std::string>> dedup_table;
  auto intern = [&](const std::string &payload) -> std::string {
    for (const auto &kv : dedup_table) {
      if (kv.first == payload)
        return kv.second;
    }
    const std::size_t off = obj.sections[kRdataIndex].data.size();
    obj.sections[kRdataIndex].data.insert(obj.sections[kRdataIndex].data.end(),
                                           payload.begin(), payload.end());
    const std::string sym_name =
        "println.msg" + std::to_string(dedup_table.size());
    AppendDataSymbol(obj, kRdataIndex, sym_name, off, payload.size());
    dedup_table.emplace_back(payload, sym_name);
    return sym_name;
  };

  // Issue one (lea, call) reloc pair per call, with strictly increasing
  // offsets so the analysis pass observes them in source order.
  std::uint64_t cursor = 0x10;
  for (const auto &payload : calls_in_order) {
    const std::string sym = intern(payload);
    AppendPrintlnCall(obj.sections[0], cursor, sym, cursor + 0x07);
    cursor += 0x10;
  }

  return obj;
}

} // namespace

// ===========================================================================
// CollectPolyrtPrintlnSequence — empty / no-PRINTLN cases
// ===========================================================================

TEST_CASE("CollectPolyrtPrintlnSequence on an empty object vector returns empty",
          "[linker][polyrt_println][b5]") {
  const auto seq = CollectPolyrtPrintlnSequence({});
  REQUIRE(seq.empty());
}

TEST_CASE("CollectPolyrtPrintlnSequence ignores objects without polyrt_println relocs",
          "[linker][polyrt_println][b5]") {
  // An object with `.text` and `.rdata` but zero relocs against the runtime
  // symbol must produce an empty sequence — the linker's PE writer falls
  // back to its legacy exit-zero shim for such inputs.
  ObjectFile obj;
  obj.path = "<noop>";
  InputSection text;
  text.name = ".text";
  text.flags = SectionFlags::kAlloc | SectionFlags::kExecInstr;
  text.data = std::vector<std::uint8_t>(16, 0x90); // NOPs
  obj.sections.push_back(std::move(text));

  const auto seq = CollectPolyrtPrintlnSequence({obj});
  REQUIRE(seq.empty());
}

// ===========================================================================
// CollectPolyrtPrintlnSequence — single / multi / duplicate call sites
// ===========================================================================

TEST_CASE("CollectPolyrtPrintlnSequence recovers a single PRINTLN call",
          "[linker][polyrt_println][b5]") {
  const std::string msg = "hello\r\n";
  ObjectFile obj = MakeSingleObjectFixture({msg});

  const auto seq = CollectPolyrtPrintlnSequence({obj});
  REQUIRE(seq.size() == 1);
  REQUIRE(seq[0] == msg);
}

TEST_CASE("CollectPolyrtPrintlnSequence preserves call order across multiple sites",
          "[linker][polyrt_println][b5]") {
  const std::vector<std::string> calls = {"alpha\r\n", "beta\r\n", "gamma\r\n"};
  ObjectFile obj = MakeSingleObjectFixture(calls);

  const auto seq = CollectPolyrtPrintlnSequence({obj});
  REQUIRE(seq == calls);
}

TEST_CASE("CollectPolyrtPrintlnSequence emits duplicate payloads at every call site",
          "[linker][polyrt_println][b5][dedup]") {
  // The interner only stores `alpha` once, but the .text issues two calls
  // against the same `println.msg0` symbol.  The recovered vector must
  // still report two `alpha` entries — `BuildPrintlnSequencePE` is the
  // layer that re-deduplicates the storage; the analysis pass faithfully
  // mirrors call-order semantics.
  const std::vector<std::string> calls = {"alpha\r\n", "beta\r\n", "alpha\r\n"};
  ObjectFile obj = MakeSingleObjectFixture(calls);

  const auto seq = CollectPolyrtPrintlnSequence({obj});
  REQUIRE(seq == calls);
}

// ===========================================================================
// CollectPolyrtPrintlnSequence — `.ptr` GEP-alias contract
// ===========================================================================

TEST_CASE("CollectPolyrtPrintlnSequence resolves the println.msg<N>.ptr GEP alias",
          "[linker][polyrt_println][b5][gep]") {
  // The IRBuilder::MakeStringLiteral contract returns the `.ptr` GEP-alias
  // global as the call's first argument; whether the back-end collapses
  // that into a direct reference to the data global or keeps it as a
  // separate symbol is a code-gen detail.  The analysis pass must accept
  // either spelling and locate the underlying data global by stripping
  // the `.ptr` suffix.
  const std::string payload = "ptr-aliased\r\n";

  ObjectFile obj;
  obj.path = "<gep>";
  InputSection text;
  text.name = ".text";
  text.flags = SectionFlags::kAlloc | SectionFlags::kExecInstr;
  InputSection rdata;
  rdata.name = ".rdata";
  rdata.flags = SectionFlags::kAlloc;
  rdata.data.assign(payload.begin(), payload.end());
  obj.sections.push_back(std::move(text));
  obj.sections.push_back(std::move(rdata));

  AppendDataSymbol(obj, 1, "println.msg0", 0, payload.size());
  AppendPrintlnCall(obj.sections[0], 0x10, "println.msg0.ptr", 0x17);

  const auto seq = CollectPolyrtPrintlnSequence({obj});
  REQUIRE(seq.size() == 1);
  REQUIRE(seq[0] == payload);
}

// ===========================================================================
// CollectPolyrtPrintlnSequence — cross-object and offset-ordering invariants
// ===========================================================================

TEST_CASE("CollectPolyrtPrintlnSequence locates the data global in another object",
          "[linker][polyrt_println][b5][cross-object]") {
  // The PRINTLN-bearing `.text` lives in object A, but its referenced
  // message global is defined in object B (e.g. the IR interner shared a
  // payload across translation units).  The analysis pass must scan all
  // loaded objects when resolving a message symbol.
  const std::string payload = "cross-object\r\n";

  ObjectFile data_obj;
  data_obj.path = "<data>";
  InputSection rdata;
  rdata.name = ".rdata";
  rdata.flags = SectionFlags::kAlloc;
  rdata.data.assign(payload.begin(), payload.end());
  data_obj.sections.push_back(std::move(rdata));
  AppendDataSymbol(data_obj, 0, "println.msg0", 0, payload.size());

  ObjectFile call_obj;
  call_obj.path = "<calls>";
  InputSection text;
  text.name = ".text";
  text.flags = SectionFlags::kAlloc | SectionFlags::kExecInstr;
  call_obj.sections.push_back(std::move(text));
  AppendPrintlnCall(call_obj.sections[0], 0x10, "println.msg0", 0x17);

  // Order matters for resolution? — No; the analysis pass scans both, so
  // either ordering must work.  Try both to lock the contract.
  {
    const auto seq = CollectPolyrtPrintlnSequence({data_obj, call_obj});
    REQUIRE(seq.size() == 1);
    REQUIRE(seq[0] == payload);
  }
  {
    const auto seq = CollectPolyrtPrintlnSequence({call_obj, data_obj});
    REQUIRE(seq.size() == 1);
    REQUIRE(seq[0] == payload);
  }
}

TEST_CASE("CollectPolyrtPrintlnSequence sorts unsorted relocations by offset",
          "[linker][polyrt_println][b5][offset-order]") {
  // The POBJ / COFF / ELF loaders may surface relocations in file order
  // rather than instruction order; the analysis pass owns the sort so
  // backends don't have to.  Insert two (lea, call) pairs in *reverse*
  // offset order and assert the recovered vector still reflects the
  // real instruction sequence.
  const std::string first  = "first\r\n";
  const std::string second = "second\r\n";

  ObjectFile obj;
  obj.path = "<unsorted>";
  InputSection text;
  text.name = ".text";
  text.flags = SectionFlags::kAlloc | SectionFlags::kExecInstr;
  InputSection rdata;
  rdata.name = ".rdata";
  rdata.flags = SectionFlags::kAlloc;

  rdata.data.insert(rdata.data.end(), first.begin(), first.end());
  rdata.data.insert(rdata.data.end(), second.begin(), second.end());
  obj.sections.push_back(std::move(text));
  obj.sections.push_back(std::move(rdata));

  AppendDataSymbol(obj, 1, "println.msg0", 0, first.size());
  AppendDataSymbol(obj, 1, "println.msg1", first.size(), second.size());

  // Emit the SECOND call's relocs first, FIRST call's relocs second.
  AppendPrintlnCall(obj.sections[0], 0x40, "println.msg1", 0x47);
  AppendPrintlnCall(obj.sections[0], 0x10, "println.msg0", 0x17);

  const auto seq = CollectPolyrtPrintlnSequence({obj});
  REQUIRE(seq.size() == 2);
  REQUIRE(seq[0] == first);
  REQUIRE(seq[1] == second);
}

// ===========================================================================
// CollectPolyrtPrintlnSequence — defensive cases
// ===========================================================================

TEST_CASE("CollectPolyrtPrintlnSequence skips data sections accidentally named .text",
          "[linker][polyrt_println][b5][defensive]") {
  // Only sections flagged `kExecInstr` may host call instructions.  A
  // misnamed data-only section that lacks the executable flag must NOT be
  // mined for relocs even if its name matches the heuristic.
  const std::string payload = "ignored\r\n";

  ObjectFile obj;
  obj.path = "<defensive>";
  InputSection fake_text;
  fake_text.name = ".text"; // Name matches but flags say "data only".
  fake_text.flags = SectionFlags::kAlloc;
  obj.sections.push_back(std::move(fake_text));
  InputSection rdata;
  rdata.name = ".rdata";
  rdata.flags = SectionFlags::kAlloc;
  rdata.data.assign(payload.begin(), payload.end());
  obj.sections.push_back(std::move(rdata));

  AppendDataSymbol(obj, 1, "println.msg0", 0, payload.size());
  AppendPrintlnCall(obj.sections[0], 0x10, "println.msg0", 0x17);

  const auto seq = CollectPolyrtPrintlnSequence({obj});
  REQUIRE(seq.empty());
}

TEST_CASE("CollectPolyrtPrintlnSequence skips a call that has no preceding message reloc",
          "[linker][polyrt_println][b5][defensive]") {
  // A `polyrt_println` call without a preceding message reloc cannot be
  // resolved (the message pointer must come from somewhere — likely a
  // computed expression that B5 does not yet handle); silently skipping
  // it lets the rest of the recovered sequence remain valid for the
  // current pipeline.
  ObjectFile obj;
  obj.path = "<orphan-call>";
  InputSection text;
  text.name = ".text";
  text.flags = SectionFlags::kAlloc | SectionFlags::kExecInstr;
  Relocation orphan_call;
  orphan_call.offset = 0x10;
  orphan_call.symbol = "polyrt_println";
  text.relocations.push_back(orphan_call);
  obj.sections.push_back(std::move(text));

  const auto seq = CollectPolyrtPrintlnSequence({obj});
  REQUIRE(seq.empty());
}
