// =====================================================================
// section_merge_test.cpp
//
// Regression unit tests for Linker::LayoutSections / MergeInputSections.
// Pins the invariant that user `.text` bytes from a loaded COFF object
// survive the load → resolve → layout pipeline and reach the merged
// `output_sections_['.text']` accumulator intact, byte-for-byte and at
// the head of the output section.
//
// This is the contract the Win32 PE writer relies on when it pulls
// user code out of the linker to compose the final image.
// =====================================================================

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "backends/common/include/object_file.h"
#include "tools/polyld/include/linker.h"

using polyglot::backends::COFFBuilder;
using polyglot::backends::MachOBuilder;
using polyglot::backends::Section;
using polyglot::backends::Symbol;
using polyglot::linker::Linker;
using polyglot::linker::LinkerConfig;
using polyglot::linker::OutputFormat;
using polyglot::linker::TargetArch;

namespace {

// A unique, easily searchable sentinel byte sequence for the user `.text`.
// 0x90 = NOP (x86), padded with 0xCC (INT3) so an accidental jump still
// traps deterministically rather than silently executing.
const std::vector<std::uint8_t> &Sentinel() {
  static const std::vector<std::uint8_t> bytes = {
      0x90, 0xCC, 0xDE, 0xAD, 0xBE, 0xEF, 0x90, 0xCC,
      0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
      0xC3 // RET
  };
  return bytes;
}

// Build a real AMD64 COFF .obj on disk holding `.text = sentinel` plus a
// global function symbol pointing at offset 0 of `.text`. Returns the
// temp path; caller is responsible for std::remove.
std::string EmitSyntheticCoffObject() {
  COFFBuilder builder(/*is_arm64=*/false);

  Section text;
  text.name = ".text";
  text.data = Sentinel();
  builder.AddSection(text);

  Symbol main_sym;
  main_sym.name = "synthmain"; // 9 chars — long-name path also exercised
  main_sym.section = ".text";
  main_sym.offset = 0;
  main_sym.size = Sentinel().size();
  main_sym.is_global = true;
  main_sym.is_function = true;
  builder.AddSymbol(main_sym);

  std::vector<std::uint8_t> obj = builder.Build();
  REQUIRE(!obj.empty());

  // Write to a unique temp path next to the test binary.
  static int counter = 0;
  std::string path = "polyld_section_merge_" + std::to_string(++counter) + ".obj";
  std::ofstream f(path, std::ios::binary);
  REQUIRE(f.good());
  f.write(reinterpret_cast<const char *>(obj.data()),
          static_cast<std::streamsize>(obj.size()));
  REQUIRE(f.good());
  f.close();
  return path;
}

// Build a Mach-O object containing a real x86-64 `jmp rel32` relocation.
// The target can be either a defined local label (the regression case) or
// an undefined global symbol (the hard-failure case).
std::string EmitSyntheticMachOBranchObject(bool define_local_target) {
  MachOBuilder builder(/*is_arm64=*/false);

  Section text;
  text.name = ".text";
  text.data = {
      0xE9, 0x00, 0x00, 0x00, 0x00, // jmp rel32
      0xCC, 0xCC, 0xCC,             // bytes skipped by the branch
      0xC3                            // local_target: ret
  };

  polyglot::backends::Relocation branch;
  branch.offset = 1; // Four-byte displacement field after the E9 opcode.
  branch.symbol = define_local_target ? "local_target" : "missing_target";
  branch.type = 1; // MachOBuilder's generic PC-relative branch encoding.
  branch.addend = 0;
  text.relocations.push_back(branch);
  builder.AddSection(text);

  Symbol entry;
  entry.name = "macho_entry";
  entry.section = ".text";
  entry.offset = 0;
  entry.size = text.data.size();
  entry.is_global = true;
  entry.is_function = true;
  builder.AddSymbol(entry);

  Symbol target;
  target.name = define_local_target ? "local_target" : "missing_target";
  target.section = define_local_target ? ".text" : "";
  target.offset = define_local_target ? 8 : 0;
  target.size = define_local_target ? 1 : 0;
  target.is_global = !define_local_target;
  target.is_function = true;
  builder.AddSymbol(target);

  std::vector<std::uint8_t> object = builder.Build();
  REQUIRE(!object.empty());

  static int counter = 0;
  const std::string path =
      "polyld_local_branch_" + std::to_string(++counter) + ".o";
  std::ofstream file(path, std::ios::binary);
  REQUIRE(file.good());
  file.write(reinterpret_cast<const char *>(object.data()),
             static_cast<std::streamsize>(object.size()));
  REQUIRE(file.good());
  file.close();
  return path;
}

// Emit an earlier contribution with the same local-label spelling.  This
// ensures the branch regression also proves object scoping and contribution
// offset handling, rather than succeeding accidentally at output offset zero.
std::string EmitSyntheticMachOPrefixObject() {
  MachOBuilder builder(/*is_arm64=*/false);

  Section text;
  text.name = ".text";
  text.data.assign(16, 0x90);
  builder.AddSection(text);

  Symbol entry;
  entry.name = "prefix_entry";
  entry.section = ".text";
  entry.offset = 0;
  entry.size = text.data.size();
  entry.is_global = true;
  entry.is_function = true;
  builder.AddSymbol(entry);

  Symbol same_named_local;
  same_named_local.name = "local_target";
  same_named_local.section = ".text";
  same_named_local.offset = 0;
  same_named_local.size = 1;
  same_named_local.is_global = false;
  same_named_local.is_function = true;
  builder.AddSymbol(same_named_local);

  const std::vector<std::uint8_t> object = builder.Build();
  REQUIRE(!object.empty());

  const std::string path = "polyld_local_branch_prefix.o";
  std::ofstream file(path, std::ios::binary);
  REQUIRE(file.good());
  file.write(reinterpret_cast<const char *>(object.data()),
             static_cast<std::streamsize>(object.size()));
  REQUIRE(file.good());
  file.close();
  return path;
}

} // namespace

TEST_CASE("Linker preserves user .text bytes through layout",
          "[linker][section_merge]") {
  const std::string obj_path = EmitSyntheticCoffObject();

  LinkerConfig config;
  config.input_files = {obj_path};
  config.output_file = obj_path + ".out";
  config.output_format = OutputFormat::kExecutable; // ELF path, no PE writer
  config.target_arch = TargetArch::kX86_64;
  config.verbose = false;

  Linker linker(config);

  REQUIRE(linker.LoadObjectFiles());
  REQUIRE(linker.GetObjects().size() == 1);

  // The COFF loader must have parsed our `.text` exactly.
  const auto &obj = linker.GetObjects()[0];
  bool text_seen = false;
  for (const auto &s : obj.sections) {
    if (s.name == ".text") {
      REQUIRE(s.data == Sentinel());
      text_seen = true;
    }
  }
  REQUIRE(text_seen);

  // Layout merges per-object sections into output_sections_.
  // ResolveSymbols may warn on undefined externs; we don't gate on it.
  (void)linker.ResolveSymbols();
  REQUIRE(linker.LayoutSections());

  bool out_text_seen = false;
  for (const auto &out : linker.GetOutputSections()) {
    if (out.name != ".text") {
      continue;
    }
    out_text_seen = true;
    // The user bytes must appear at the head of the merged section.
    REQUIRE(out.data.size() >= Sentinel().size());
    for (std::size_t i = 0; i < Sentinel().size(); ++i) {
      REQUIRE(out.data[i] == Sentinel()[i]);
    }
  }
  REQUIRE(out_text_seen);

  std::remove(obj_path.c_str());
}

TEST_CASE("Linker concatenates .text from multiple COFF inputs",
          "[linker][section_merge]") {
  // First object: sentinel.
  const std::string a_path = EmitSyntheticCoffObject();

  // Second object: a different deterministic blob.
  std::vector<std::uint8_t> blob_b = {0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3};
  COFFBuilder builder(false);
  Section text;
  text.name = ".text";
  text.data = blob_b;
  builder.AddSection(text);
  Symbol s;
  s.name = "blob_b_entry";
  s.section = ".text";
  s.offset = 0;
  s.size = blob_b.size();
  s.is_global = true;
  s.is_function = true;
  builder.AddSymbol(s);
  std::vector<std::uint8_t> obj_b = builder.Build();
  std::string b_path = "polyld_section_merge_blob_b.obj";
  {
    std::ofstream f(b_path, std::ios::binary);
    f.write(reinterpret_cast<const char *>(obj_b.data()),
            static_cast<std::streamsize>(obj_b.size()));
  }

  LinkerConfig config;
  config.input_files = {a_path, b_path};
  config.output_file = "polyld_section_merge_combined.out";
  config.output_format = OutputFormat::kExecutable;
  config.target_arch = TargetArch::kX86_64;

  Linker linker(config);
  REQUIRE(linker.LoadObjectFiles());
  (void)linker.ResolveSymbols();
  REQUIRE(linker.LayoutSections());

  // Combined `.text` must contain BOTH input blobs (in some order, but each
  // contiguously). Search for both as substrings.
  const auto find_blob = [](const std::vector<std::uint8_t> &hay,
                            const std::vector<std::uint8_t> &needle) {
    if (needle.size() > hay.size()) {
      return false;
    }
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
      bool ok = true;
      for (std::size_t j = 0; j < needle.size(); ++j) {
        if (hay[i + j] != needle[j]) {
          ok = false;
          break;
        }
      }
      if (ok) {
        return true;
      }
    }
    return false;
  };

  bool checked = false;
  for (const auto &out : linker.GetOutputSections()) {
    if (out.name != ".text") {
      continue;
    }
    checked = true;
    REQUIRE(out.data.size() >= Sentinel().size() + blob_b.size());
    REQUIRE(find_blob(out.data, Sentinel()));
    REQUIRE(find_blob(out.data, blob_b));
  }
  REQUIRE(checked);

  std::remove(a_path.c_str());
  std::remove(b_path.c_str());
}

TEST_CASE("Linker applies a Mach-O branch relocation to a same-object local symbol",
          "[linker][relocation][macho][local-symbol]") {
  const std::string prefix_path = EmitSyntheticMachOPrefixObject();
  const std::string object_path = EmitSyntheticMachOBranchObject(true);

  LinkerConfig config;
  config.input_files = {prefix_path, object_path};
  config.output_file = object_path + ".out";
  config.output_format = OutputFormat::kExecutable;
  config.target_arch = TargetArch::kX86_64;

  Linker linker(config);
  REQUIRE(linker.LoadObjectFiles());
  REQUIRE(linker.ResolveSymbols());

  // Local labels must remain object-scoped instead of polluting the global
  // table, even though relocations still need to resolve them.
  REQUIRE(linker.LookupSymbol("_local_target") == nullptr);

  REQUIRE(linker.LayoutSections());
  REQUIRE(linker.ApplyRelocations());
  REQUIRE(linker.GetStats().relocations_processed == 1);

  bool text_seen = false;
  for (const auto &section : linker.GetOutputSections()) {
    if (section.name != "__text") {
      continue;
    }
    text_seen = true;
    std::uint64_t branch_contribution = 0;
    bool branch_contribution_seen = false;
    for (const auto &contribution : section.contributions) {
      if (contribution.object_index == 1) {
        branch_contribution = contribution.output_offset;
        branch_contribution_seen = true;
        break;
      }
    }
    REQUIRE(branch_contribution_seen);
    REQUIRE(branch_contribution > 0);
    REQUIRE(section.data.size() >= branch_contribution + 9);
    // target(8) - next_instruction(5) = +3.  A zero displacement is the
    // original bug and would branch to the first skipped byte instead.  The
    // earlier contribution also ensures an address computed without this
    // object's output offset cannot pass.
    REQUIRE(section.data[branch_contribution + 1] == 0x03);
    REQUIRE(section.data[branch_contribution + 2] == 0x00);
    REQUIRE(section.data[branch_contribution + 3] == 0x00);
    REQUIRE(section.data[branch_contribution + 4] == 0x00);
  }
  REQUIRE(text_seen);

  std::remove(prefix_path.c_str());
  std::remove(object_path.c_str());
}

TEST_CASE("Linker rejects an unresolved relocation instead of writing zero",
          "[linker][relocation][unresolved]") {
  const std::string object_path = EmitSyntheticMachOBranchObject(false);

  LinkerConfig config;
  config.input_files = {object_path};
  config.output_file = object_path + ".out";
  config.output_format = OutputFormat::kExecutable;
  config.target_arch = TargetArch::kX86_64;

  Linker linker(config);
  REQUIRE(linker.LoadObjectFiles());
  // Undefined symbols are warnings at this stage unless no_undefined is set;
  // relocation application must nevertheless refuse to encode address zero.
  REQUIRE(linker.ResolveSymbols());
  REQUIRE(linker.LayoutSections());
  REQUIRE_FALSE(linker.ApplyRelocations());
  REQUIRE(linker.GetStats().relocations_processed == 0);

  bool diagnostic_seen = false;
  for (const std::string &error : linker.GetErrors()) {
    if (error.find("Unresolved symbol in relocation: _missing_target") !=
        std::string::npos) {
      diagnostic_seen = true;
      break;
    }
  }
  REQUIRE(diagnostic_seen);

  std::remove(object_path.c_str());
}
