/**
 * @file     pe_writer_test.cpp
 * @brief    Unit tests for the standalone PE32+ image writer.
 *
 * Validates the byte-level shape of the produced Windows PE32+ image that
 * the linker uses for native Windows AMD64 executable output.
 *
 * @ingroup  Tests / linker
 * @author   Manning Cyrus
 * @date     2026-04-28
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

#include "tools/polyld/include/pe_writer.h"

using namespace polyglot::linker::pe;

namespace {

// Decode a little-endian unsigned integer from a byte buffer at `offset`.
template <typename T> T LoadLE(const std::vector<std::uint8_t> &b, std::size_t offset) {
  T v{};
  std::memcpy(&v, b.data() + offset, sizeof(T));
  return v;
}

// PE on-disk layout constants we cross-check against the writer's output.
constexpr std::size_t kDosELFAnewOffset = 0x3C;
constexpr std::uint16_t kCoffFlagRelocsStripped = 0x0001;
constexpr std::uint16_t kCoffFlagExecutableImage = 0x0002;
constexpr std::uint16_t kPe32PlusMagic = 0x020B;
constexpr std::uint16_t kMachineAmd64 = 0x8664;
constexpr std::uint16_t kSubsystemWinCui = 3;
constexpr std::uint16_t kDllCharNxCompat = 0x0100;

} // namespace

TEST_CASE("BuildMinimalExitZeroImage produces a structurally valid PE32+",
          "[pe_writer][pe32plus]") {
  BuildResult r = BuildMinimalExitZeroImage();
  REQUIRE_FALSE(r.image.empty());

  const auto &b = r.image;

  // MZ magic at offset 0.
  REQUIRE(b.size() > 0x40);
  REQUIRE(b[0] == 'M');
  REQUIRE(b[1] == 'Z');

  // e_lfanew points at the PE signature.
  const std::uint32_t e_lfanew = LoadLE<std::uint32_t>(b, kDosELFAnewOffset);
  REQUIRE(e_lfanew + 4 <= b.size());
  REQUIRE(b[e_lfanew + 0] == 'P');
  REQUIRE(b[e_lfanew + 1] == 'E');
  REQUIRE(b[e_lfanew + 2] == 0);
  REQUIRE(b[e_lfanew + 3] == 0);

  // COFF File Header begins right after the PE signature.
  const std::size_t coff = e_lfanew + 4;
  REQUIRE(LoadLE<std::uint16_t>(b, coff + 0) == kMachineAmd64);
  // We emit exactly two sections (.text + .idata) for the minimal image.
  REQUIRE(LoadLE<std::uint16_t>(b, coff + 2) == 2);
  // Optional Header size is the PE32+ standard 240 bytes.
  REQUIRE(LoadLE<std::uint16_t>(b, coff + 16) == 240);
  const std::uint16_t characteristics = LoadLE<std::uint16_t>(b, coff + 18);
  REQUIRE((characteristics & kCoffFlagRelocsStripped) != 0);
  REQUIRE((characteristics & kCoffFlagExecutableImage) != 0);

  // Optional Header.
  const std::size_t opt = coff + 20;
  REQUIRE(LoadLE<std::uint16_t>(b, opt + 0) == kPe32PlusMagic);
  // AddressOfEntryPoint == BaseOfCode for the minimal image (entry sits at
  // the very first byte of .text).
  REQUIRE(LoadLE<std::uint32_t>(b, opt + 16) == LoadLE<std::uint32_t>(b, opt + 20));
  REQUIRE(LoadLE<std::uint32_t>(b, opt + 16) == r.entry_rva);
  // Subsystem == Windows console.
  REQUIRE(LoadLE<std::uint16_t>(b, opt + 68) == kSubsystemWinCui);
  // DllCharacteristics: NX compatible flag must be set so DEP allows
  // execution of .text.
  REQUIRE((LoadLE<std::uint16_t>(b, opt + 70) & kDllCharNxCompat) != 0);
  // 16 data directories.
  REQUIRE(LoadLE<std::uint32_t>(b, opt + 108) == 16);

  // Import Directory (data dir [1]) and IAT directory (data dir [12]) must
  // both be populated for an image that imports kernel32!ExitProcess.
  const std::size_t data_dirs = opt + 112;
  REQUIRE(LoadLE<std::uint32_t>(b, data_dirs + 1 * 8 + 0) != 0);
  REQUIRE(LoadLE<std::uint32_t>(b, data_dirs + 1 * 8 + 4) != 0);
  REQUIRE(LoadLE<std::uint32_t>(b, data_dirs + 12 * 8 + 0) != 0);
  REQUIRE(LoadLE<std::uint32_t>(b, data_dirs + 12 * 8 + 4) != 0);

  // The IAT slot for ExitProcess must be reported in the result.
  bool found = false;
  for (const auto &kv : r.iat_slot_rva) {
    if (kv.first == "kernel32.dll!ExitProcess") {
      REQUIRE(kv.second != 0);
      found = true;
      break;
    }
  }
  REQUIRE(found);
}

TEST_CASE("BuildExitProcessShim emits the canonical 13-byte sequence",
          "[pe_writer][shim]") {
  // Pick arbitrary RVAs and verify the byte pattern + encoded disp32.
  const std::uint32_t shim_rva = 0x1000;
  const std::uint32_t exit_iat_rva = 0x2038;
  std::vector<std::uint8_t> bytes = BuildExitProcessShim(shim_rva, exit_iat_rva);

  REQUIRE(bytes.size() == 13);
  // sub rsp, 0x28
  REQUIRE(bytes[0] == 0x48);
  REQUIRE(bytes[1] == 0x83);
  REQUIRE(bytes[2] == 0xEC);
  REQUIRE(bytes[3] == 0x28);
  // xor ecx, ecx
  REQUIRE(bytes[4] == 0x31);
  REQUIRE(bytes[5] == 0xC9);
  // call qword ptr [rip + disp32]
  REQUIRE(bytes[6] == 0xFF);
  REQUIRE(bytes[7] == 0x15);
  // disp32 = exit_iat_rva - (shim_rva + 4 + 2 + 6)
  const std::uint32_t expected_disp = exit_iat_rva - (shim_rva + 4 + 2 + 6);
  REQUIRE(LoadLE<std::uint32_t>(bytes, 8) == expected_disp);
  // int3
  REQUIRE(bytes[12] == 0xCC);
}

TEST_CASE("BuildExitZeroPE wraps user .text bytes and re-targets the entry",
          "[pe_writer][pe32plus]") {
  // 256 bytes of arbitrary user "code" (we only validate the wrapping
  // contract; the bytes themselves never execute because the entry is
  // pointed at the appended shim).
  std::vector<std::uint8_t> user_text(256, 0x90); // NOP-fill
  user_text[0] = 0xCC;                            // distinguishable head byte

  BuildResult r = BuildExitZeroPE(user_text);
  REQUIRE_FALSE(r.image.empty());

  const auto &b = r.image;
  const std::uint32_t e_lfanew = LoadLE<std::uint32_t>(b, kDosELFAnewOffset);
  const std::size_t opt = e_lfanew + 4 + 20;

  // Entry must NOT point at the start of user_text (RVA 0x1000); it must
  // point at the shim that the writer appended.
  const std::uint32_t entry_rva = LoadLE<std::uint32_t>(b, opt + 16);
  REQUIRE(entry_rva == r.entry_rva);
  REQUIRE(entry_rva > 0x1000);
  REQUIRE(entry_rva == 0x1000 + user_text.size());

  // Locate .text section header to verify the user bytes are preserved on
  // disk in the produced image.
  const std::size_t section_table = opt + 240; // OptionalHeader64 = 240 bytes
  // Find the section whose name starts with ".text".
  std::size_t text_sect = 0;
  for (int i = 0; i < 2; ++i) {
    const std::size_t hdr = section_table + i * 40;
    if (b[hdr + 0] == '.' && b[hdr + 1] == 't' && b[hdr + 2] == 'e' && b[hdr + 3] == 'x') {
      text_sect = hdr;
      break;
    }
  }
  REQUIRE(text_sect != 0);
  const std::uint32_t text_raw_off = LoadLE<std::uint32_t>(b, text_sect + 20);
  REQUIRE(text_raw_off + user_text.size() <= b.size());
  // Verify the head byte of user_text round-tripped.
  REQUIRE(b[text_raw_off] == 0xCC);
  // Verify a NOP somewhere in the middle round-tripped.
  REQUIRE(b[text_raw_off + 100] == 0x90);
}

TEST_CASE("BuildExitZeroPE on empty input is identical to the minimal image",
          "[pe_writer][edge]") {
  BuildResult empty_r = BuildExitZeroPE({});
  BuildResult min_r = BuildMinimalExitZeroImage();
  REQUIRE(empty_r.image.size() == min_r.image.size());
  REQUIRE(empty_r.image == min_r.image);
}

TEST_CASE("BuildHelloWorldPE produces a 3-section PE with .rdata + 3 imports",
          "[pe_writer][hello]") {
  const std::string msg = "hello\r\n";
  BuildResult r = BuildHelloWorldPE(msg);
  REQUIRE_FALSE(r.image.empty());

  const auto &b = r.image;
  const std::uint32_t e_lfanew = LoadLE<std::uint32_t>(b, kDosELFAnewOffset);
  const std::size_t coff = e_lfanew + 4;

  // Three sections: .text + .rdata + .idata.
  REQUIRE(LoadLE<std::uint16_t>(b, coff + 2) == 3);

  // rdata_rva must be the conventional 0x2000 (page after .text).
  REQUIRE(r.rdata_rva == 0x2000);

  // All three IAT slots must be reported.
  bool seen_get = false, seen_write = false, seen_exit = false;
  for (const auto &kv : r.iat_slot_rva) {
    if (kv.first == "kernel32.dll!GetStdHandle") seen_get = true;
    else if (kv.first == "kernel32.dll!WriteFile") seen_write = true;
    else if (kv.first == "kernel32.dll!ExitProcess") seen_exit = true;
  }
  REQUIRE(seen_get);
  REQUIRE(seen_write);
  REQUIRE(seen_exit);

  // Locate the .rdata section header and confirm the message bytes are on
  // disk byte-for-byte.
  const std::size_t section_table = e_lfanew + 4 + 20 + 240;
  std::size_t rdata_sect = 0;
  for (int i = 0; i < 3; ++i) {
    const std::size_t hdr = section_table + i * 40;
    if (b[hdr + 0] == '.' && b[hdr + 1] == 'r' && b[hdr + 2] == 'd' && b[hdr + 3] == 'a') {
      rdata_sect = hdr;
      break;
    }
  }
  REQUIRE(rdata_sect != 0);
  const std::uint32_t rdata_raw_off = LoadLE<std::uint32_t>(b, rdata_sect + 20);
  REQUIRE(rdata_raw_off + msg.size() <= b.size());
  for (std::size_t i = 0; i < msg.size(); ++i)
    REQUIRE(b[rdata_raw_off + i] == static_cast<std::uint8_t>(msg[i]));
}

TEST_CASE("BuildHelloWorldPE shim begins with the canonical 4-byte sub rsp,0x38",
          "[pe_writer][hello][shim]") {
  // The 68-byte hello shim's first instruction must be `sub rsp, 0x38`,
  // which encodes as 48 83 EC 38.  This is the prologue that reserves the
  // 32-byte shadow space + the WriteFile ARG5 slot + the bytes-written slot.
  BuildResult r = BuildHelloWorldPE("x");
  REQUIRE_FALSE(r.image.empty());

  const auto &b = r.image;
  const std::uint32_t e_lfanew = LoadLE<std::uint32_t>(b, kDosELFAnewOffset);
  const std::size_t section_table = e_lfanew + 4 + 20 + 240;
  std::size_t text_sect = 0;
  for (int i = 0; i < 3; ++i) {
    const std::size_t hdr = section_table + i * 40;
    if (b[hdr + 0] == '.' && b[hdr + 1] == 't' && b[hdr + 2] == 'e' && b[hdr + 3] == 'x') {
      text_sect = hdr;
      break;
    }
  }
  REQUIRE(text_sect != 0);
  const std::uint32_t text_raw_off = LoadLE<std::uint32_t>(b, text_sect + 20);
  REQUIRE(text_raw_off + 4 <= b.size());
  REQUIRE(b[text_raw_off + 0] == 0x48);
  REQUIRE(b[text_raw_off + 1] == 0x83);
  REQUIRE(b[text_raw_off + 2] == 0xEC);
  REQUIRE(b[text_raw_off + 3] == 0x38);
  // 68-byte shim ends with int3 (0xCC).
  REQUIRE(b[text_raw_off + 0x43] == 0xCC);
}

TEST_CASE("BuildPE32PlusImage rejects empty text input",
          "[pe_writer][edge]") {
  BuildRequest req;
  req.rdata_bytes = {0x01, 0x02, 0x03};
  // No text bytes -> empty image.
  BuildResult r = BuildPE32PlusImage(req);
  REQUIRE(r.image.empty());
}

TEST_CASE("BuildPE32PlusImage with rdata + no imports lays out 2 sections",
          "[pe_writer][rdata]") {
  // Smallest valid 1-byte text (a single ret instruction would be
  // semantically nonsense at entry, but layout-wise it's fine).
  BuildRequest req;
  req.text_bytes = {0xC3};
  req.rdata_bytes = std::vector<std::uint8_t>(16, 0xAB);
  BuildResult r = BuildPE32PlusImage(req);
  REQUIRE_FALSE(r.image.empty());

  const auto &b = r.image;
  const std::uint32_t e_lfanew = LoadLE<std::uint32_t>(b, kDosELFAnewOffset);
  const std::size_t coff = e_lfanew + 4;
  // Two sections (.text + .rdata), no .idata.
  REQUIRE(LoadLE<std::uint16_t>(b, coff + 2) == 2);
  // rdata_rva should be exposed and point past .text.
  REQUIRE(r.rdata_rva == 0x2000);
}
