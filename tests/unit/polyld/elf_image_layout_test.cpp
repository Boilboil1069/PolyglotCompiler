/**
 * @file     elf_image_layout_test.cpp
 * @brief    Coverage for the polyld ELF64 writer in
 *           `tools/polyld/src/linker_elf.cpp`.  Builds a minimal
 *           `ET_EXEC` image for both x86_64 and aarch64 and asserts:
 *             * `e_ident` magic / class / data / version are correct;
 *             * `e_machine` matches the requested architecture axis;
 *             * a `PT_LOAD(R+X)` segment exists and its `[p_vaddr,
 *               p_vaddr + p_filesz)` window covers `e_entry`;
 *             * `PT_GNU_STACK` is emitted with `PF_R | PF_W` (i.e.
 *               the kernel will deny mapping the stack executable);
 *             * the first 16 bytes of `.text` match the
 *               architecture-specific `_start` byte template polyld
 *               injects ahead of every user `main`.
 *
 * @author   Manning Cyrus
 * @date     2026-05-06
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include "tools/polyld/include/linker_elf.h"

using namespace polyglot::linker::elf;

namespace {

// ---------------------------------------------------------------------------
// Little-endian readers.  Avoiding `<elf.h>` keeps this test buildable on
// every host (including Windows / macOS) rather than only on Linux.
// ---------------------------------------------------------------------------

std::uint16_t U16(const std::vector<std::uint8_t> &b, std::size_t off) {
  REQUIRE(off + 2 <= b.size());
  return static_cast<std::uint16_t>(b[off]) |
         (static_cast<std::uint16_t>(b[off + 1]) << 8);
}

std::uint32_t U32(const std::vector<std::uint8_t> &b, std::size_t off) {
  REQUIRE(off + 4 <= b.size());
  return static_cast<std::uint32_t>(b[off]) |
         (static_cast<std::uint32_t>(b[off + 1]) << 8) |
         (static_cast<std::uint32_t>(b[off + 2]) << 16) |
         (static_cast<std::uint32_t>(b[off + 3]) << 24);
}

std::uint64_t U64(const std::vector<std::uint8_t> &b, std::size_t off) {
  REQUIRE(off + 8 <= b.size());
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i)
    v |= static_cast<std::uint64_t>(b[off + i]) << (8 * i);
  return v;
}

// One decoded program-header entry (Elf64_Phdr layout).
struct Phdr {
  std::uint32_t type;
  std::uint32_t flags;
  std::uint64_t offset;
  std::uint64_t vaddr;
  std::uint64_t paddr;
  std::uint64_t filesz;
  std::uint64_t memsz;
  std::uint64_t align;
};

Phdr ReadPhdr(const std::vector<std::uint8_t> &img, std::size_t off) {
  Phdr p{};
  p.type   = U32(img, off + 0);
  p.flags  = U32(img, off + 4);
  p.offset = U64(img, off + 8);
  p.vaddr  = U64(img, off + 16);
  p.paddr  = U64(img, off + 24);
  p.filesz = U64(img, off + 32);
  p.memsz  = U64(img, off + 40);
  p.align  = U64(img, off + 48);
  return p;
}

// Build a minimal request whose `.text` contains a single architecture-
// appropriate `ret` so the writer emits a self-consistent payload
// without needing the rest of the linker pipeline to be alive.
BuildRequest MakeMinimalRequest(Arch arch) {
  BuildRequest req;
  req.arch = arch;
  req.base_address = kDefaultExeBase;
  if (arch == Arch::kAArch64) {
    req.text = {0xC0, 0x03, 0x5F, 0xD6}; // ret
  } else {
    req.text = {0xC3};                   // retq
  }
  return req;
}

// Generic per-arch validation.  Re-decodes the program header table and
// checks every invariant called out in the demand verbatim.
void ValidateImage(Arch arch, const BuildResult &br,
                   const std::vector<std::uint8_t> &expected_stub,
                   std::uint16_t expected_machine) {
  const auto &img = br.image;
  REQUIRE(img.size() > 64u);

  // ---- e_ident -----------------------------------------------------
  REQUIRE(img[0] == 0x7F);
  REQUIRE(img[1] == 'E');
  REQUIRE(img[2] == 'L');
  REQUIRE(img[3] == 'F');
  REQUIRE(img[4] == kElfClass64);
  REQUIRE(img[5] == kElfData2Lsb);
  REQUIRE(img[6] == kEvCurrent);

  // ---- e_type / e_machine / e_entry --------------------------------
  REQUIRE(U16(img, 16) == kEtExec);
  REQUIRE(U16(img, 18) == expected_machine);
  const std::uint64_t e_entry  = U64(img, 24);
  const std::uint64_t e_phoff  = U64(img, 32);
  REQUIRE(e_entry == br.entry_vaddr);
  REQUIRE(e_phoff == 64u); // Ehdr size

  const std::uint16_t e_phentsize = U16(img, 54);
  const std::uint16_t e_phnum     = U16(img, 56);
  REQUIRE(e_phentsize == 56u);
  REQUIRE(e_phnum >= 3u);

  // ---- Walk program headers ----------------------------------------
  std::optional<Phdr> rx_load;
  std::optional<Phdr> gnu_stack;
  for (std::uint16_t i = 0; i < e_phnum; ++i) {
    Phdr p = ReadPhdr(img, e_phoff + i * e_phentsize);
    if (p.type == kPtLoad && (p.flags & kPfX) != 0 && (p.flags & kPfR) != 0) {
      rx_load = p;
    }
    if (p.type == kPtGnuStack) {
      gnu_stack = p;
    }
  }

  REQUIRE(rx_load.has_value());
  REQUIRE(e_entry >= rx_load->vaddr);
  REQUIRE(e_entry <  rx_load->vaddr + rx_load->filesz);

  REQUIRE(gnu_stack.has_value());
  // PT_GNU_STACK with PF_R | PF_W explicitly marks the stack as
  // non-executable, satisfying the demand's W^X clause.
  REQUIRE(gnu_stack->flags == (kPfR | kPfW));
  REQUIRE((gnu_stack->flags & kPfX) == 0u);

  // ---- First kStartStubSize bytes of .text == architecture stub ----
  REQUIRE(br.text_file_offset + kStartStubSize <= img.size());
  REQUIRE(expected_stub.size() == kStartStubSize);
  for (std::size_t i = 0; i < kStartStubSize; ++i) {
    REQUIRE(img[br.text_file_offset + i] == expected_stub[i]);
  }
}

} // namespace

TEST_CASE("ELF writer emits valid ET_EXEC for x86_64", "[elf][polyld]") {
  BuildResult br = BuildELFImage(MakeMinimalRequest(Arch::kX86_64));
  REQUIRE_FALSE(br.image.empty());
  ValidateImage(Arch::kX86_64, br, BuildStartStubX86_64(), kEmX86_64);
}

TEST_CASE("ELF writer emits valid ET_EXEC for aarch64", "[elf][polyld]") {
  BuildResult br = BuildELFImage(MakeMinimalRequest(Arch::kAArch64));
  REQUIRE_FALSE(br.image.empty());
  ValidateImage(Arch::kAArch64, br, BuildStartStubArm64(), kEmAarch64);
}

TEST_CASE("ELF _start stub byte templates are exact", "[elf][polyld]") {
  // x86_64: call rel32=+11 ; mov rdi,rax ; mov eax,60 ; syscall ; nop
  const std::vector<std::uint8_t> x86 = {
      0xE8, 0x0B, 0x00, 0x00, 0x00,
      0x48, 0x89, 0xC7,
      0xB8, 0x3C, 0x00, 0x00, 0x00,
      0x0F, 0x05,
      0x90,
  };
  REQUIRE(BuildStartStubX86_64() == x86);

  // aarch64: bl +16 ; movz w8,#93 ; svc #0 ; nop
  const std::vector<std::uint8_t> arm = {
      0x04, 0x00, 0x00, 0x94,
      0xA8, 0x0B, 0x80, 0x52,
      0x01, 0x00, 0x00, 0xD4,
      0x1F, 0x20, 0x03, 0xD5,
  };
  REQUIRE(BuildStartStubArm64() == arm);
}

TEST_CASE("ELF writer emits PT_LOAD R+W when data is non-empty",
          "[elf][polyld]") {
  BuildRequest req = MakeMinimalRequest(Arch::kX86_64);
  req.data = {0xDE, 0xAD, 0xBE, 0xEF};
  req.bss_size = 16;

  BuildResult br = BuildELFImage(req);
  REQUIRE_FALSE(br.image.empty());

  const auto &img = br.image;
  const std::uint64_t e_phoff = U64(img, 32);
  const std::uint16_t e_phnum = U16(img, 56);

  bool saw_rw = false;
  for (std::uint16_t i = 0; i < e_phnum; ++i) {
    Phdr p = ReadPhdr(img, e_phoff + i * 56u);
    if (p.type == kPtLoad && (p.flags & kPfW) != 0) {
      saw_rw = true;
      REQUIRE(p.filesz == req.data.size());
      REQUIRE(p.memsz  == req.data.size() + req.bss_size);
      // Spec-mandated congruence between p_offset and p_vaddr modulo p_align.
      REQUIRE((p.offset & (p.align - 1)) == (p.vaddr & (p.align - 1)));
    }
  }
  REQUIRE(saw_rw);
}

TEST_CASE("ELF PRINTLN synthesis emits Linux syscall payloads",
          "[elf][polyld][println]") {
  const std::vector<std::uint8_t> x86 =
      BuildPrintlnSequenceELF(Arch::kX86_64, {"ok"});
  REQUIRE_FALSE(x86.empty());
  REQUIRE(x86[0] == 0xBF); // mov edi, 1
  bool saw_x86_write = false;
  bool saw_x86_exit = false;
  for (std::size_t i = 0; i + 4 < x86.size(); ++i) {
    if (x86[i] == 0xB8 && x86[i + 1] == 0x01 && x86[i + 2] == 0x00 &&
        x86[i + 3] == 0x00 && x86[i + 4] == 0x00) {
      saw_x86_write = true;
    }
    if (x86[i] == 0xB8 && x86[i + 1] == 0x3C && x86[i + 2] == 0x00 &&
        x86[i + 3] == 0x00 && x86[i + 4] == 0x00) {
      saw_x86_exit = true;
    }
  }
  REQUIRE(saw_x86_write);
  REQUIRE(saw_x86_exit);
  REQUIRE(x86[x86.size() - 3] == 'o');
  REQUIRE(x86[x86.size() - 2] == 'k');
  REQUIRE(x86[x86.size() - 1] == '\n');

  const std::vector<std::uint8_t> arm =
      BuildPrintlnSequenceELF(Arch::kAArch64, {"ok"});
  REQUIRE_FALSE(arm.empty());
  REQUIRE(arm.size() >= 35u);
  // movz x0, #1
  REQUIRE(arm[4] == 0x20);
  REQUIRE(arm[5] == 0x00);
  REQUIRE(arm[6] == 0x80);
  REQUIRE(arm[7] == 0xD2);
  // movz x8, #64 (__NR_write)
  REQUIRE(arm[12] == 0x08);
  REQUIRE(arm[13] == 0x08);
  REQUIRE(arm[14] == 0x80);
  REQUIRE(arm[15] == 0xD2);
  REQUIRE(arm[arm.size() - 3] == 'o');
  REQUIRE(arm[arm.size() - 2] == 'k');
  REQUIRE(arm[arm.size() - 1] == '\n');
}
