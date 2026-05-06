/**
 * @file     linker_elf.cpp
 * @brief    ELF64 `ET_EXEC` writer used by polyld on Linux x86_64 and
 *           aarch64 hosts.  The image produced here is intentionally
 *           libc-free and self-contained: a single `_start` stub
 *           prepended to `.text` calls user `main`, forwards the
 *           return value into the kernel `exit` syscall, and is the
 *           only piece of code the kernel needs in order to load and
 *           execute the binary via `execve(2)`.
 *
 *           Free helpers in the inner `elf` namespace handle the
 *           ELF / program-header / section-header arithmetic; the
 *           `Linker::GenerateELFExecutable` member in `linker.cpp`
 *           is a thin shim that fills in a `BuildRequest` from the
 *           shared `OutputSection` table and writes the resulting
 *           bytes to disk.
 *
 * @ingroup  Tool / polyld
 * @author   Manning Cyrus
 * @date     2026-05-06
 */

#include "tools/polyld/include/linker_elf.h"

#include <cstring>

namespace polyglot::linker::elf {

namespace {

// ---------------------------------------------------------------------------
// Little-endian append helpers.  ELF64 fields are all aligned on their
// natural width once the header is at offset 0, but writing through a
// byte append routine keeps the writer endian-safe even on big-endian
// hosts (which we do not target today but cost nothing to support).
// ---------------------------------------------------------------------------

void AppendU8(std::vector<std::uint8_t> &out, std::uint8_t v) {
  out.push_back(v);
}

void AppendU16(std::vector<std::uint8_t> &out, std::uint16_t v) {
  out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
}

void AppendU32(std::vector<std::uint8_t> &out, std::uint32_t v) {
  for (int i = 0; i < 4; ++i)
    out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
}

void AppendU64(std::vector<std::uint8_t> &out, std::uint64_t v) {
  for (int i = 0; i < 8; ++i)
    out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
}

void PadTo(std::vector<std::uint8_t> &out, std::size_t target) {
  while (out.size() < target) out.push_back(0);
}

// ---------------------------------------------------------------------------
// Architecture-specific `_start` stub assembly.  The stubs are 16 bytes
// each so that the user `main` symbol always lives at exactly
// `text_vaddr + kStartStubSize`.  Both stubs:
//   1. transfer control to `main` (a relative call / branch-link whose
//      immediate is computed from the fact that `main` is the very
//      next instruction after the stub);
//   2. take `main`'s return value out of the architecture's first
//      return register and feed it as the first argument to the
//      kernel exit syscall;
//   3. invoke the syscall and trap if it ever returns.
// ---------------------------------------------------------------------------

// x86_64 SysV ABI:
//   * `call rel32`               (5 bytes)   target = main (offset 16
//                                            from start, so rel32 = 11)
//   * `mov  rdi, rax`            (3 bytes)   forward main's return value
//   * `mov  eax, 60`             (5 bytes)   __NR_exit
//   * `syscall`                  (2 bytes)
//   * `nop`                      (1 byte)    pad to 16
//
// Total: 5 + 3 + 5 + 2 + 1 = 16.
std::vector<std::uint8_t> StubX86_64() {
  return {
      0xE8, 0x0B, 0x00, 0x00, 0x00,       // call main (rel32 = +11)
      0x48, 0x89, 0xC7,                   // mov rdi, rax
      0xB8, 0x3C, 0x00, 0x00, 0x00,       // mov eax, 60
      0x0F, 0x05,                         // syscall
      0x90,                               // nop
  };
}

// AArch64 AAPCS:
//   * `bl  main`           (4 bytes)  imm26 = 4 (offset = 16 bytes)
//                                     (return value already in x0)
//   * `mov w8, #93`        (4 bytes)  __NR_exit on aarch64
//   * `svc #0`             (4 bytes)
//   * `nop`                (4 bytes)  pad to 16
//
// Encoding details (all little-endian on disk):
//   * BL:    0x94000000 | (imm26 & 0x03FFFFFF) ; here imm26 = 4
//            -> 0x94000004
//   * MOVZ w8, #93 (LSL 0):
//            opcode = 0x52800000
//            imm16 = 93, shift in bits[20:5]
//            rd = 8 (low five bits)
//            -> 0x52800000 | (93 << 5) | 8 = 0x52800BA8
//   * SVC #0:
//            0xD4000001
//   * NOP:   0xD503201F
std::vector<std::uint8_t> StubArm64() {
  auto append32 = [](std::vector<std::uint8_t> &b, std::uint32_t insn) {
    for (int i = 0; i < 4; ++i)
      b.push_back(static_cast<std::uint8_t>((insn >> (8 * i)) & 0xFFu));
  };
  std::vector<std::uint8_t> out;
  out.reserve(16);
  append32(out, 0x94000004u);  // bl main
  append32(out, 0x52800BA8u);  // movz w8, #93
  append32(out, 0xD4000001u);  // svc #0
  append32(out, 0xD503201Fu);  // nop
  return out;
}

// Round `value` up to the nearest multiple of `align` (which must be a
// power of two).  Used for page-aligning PT_LOAD file offsets.
inline std::uint64_t AlignUp(std::uint64_t value, std::uint64_t align) {
  return (value + align - 1) & ~(align - 1);
}

// Encoded sizes of fixed ELF64 records.
constexpr std::size_t kEhdr64Size = 64;
constexpr std::size_t kPhdr64Size = 56;
constexpr std::size_t kShdr64Size = 64;

// The writer always emits exactly four program headers so the e_entry
// arithmetic below stays trivial (no two-pass relayout):
//   [0] PT_PHDR        — describes the phdr table itself
//   [1] PT_LOAD R+X    — covers ELF header + phdrs + .text + .rodata
//   [2] PT_LOAD R+W    — covers .data + .bss   (size 0 when unused)
//   [3] PT_GNU_STACK   — non-executable stack marker
constexpr std::size_t kNumPhdrs = 4;

// ---------------------------------------------------------------------------
// Section header table.  Linux `execve(2)` does not consult section
// headers, but emitting a self-consistent table keeps `readelf -S`,
// `objdump -h` and `gdb` happy.  Names are interned into a single
// `.shstrtab` blob whose first byte is the mandatory NUL.
// ---------------------------------------------------------------------------

struct PendingShdr {
  std::uint32_t name_off{0};
  std::uint32_t sh_type{0};
  std::uint64_t sh_flags{0};
  std::uint64_t sh_addr{0};
  std::uint64_t sh_offset{0};
  std::uint64_t sh_size{0};
  std::uint32_t sh_link{0};
  std::uint32_t sh_info{0};
  std::uint64_t sh_addralign{0};
  std::uint64_t sh_entsize{0};
};

void EmitShdr(std::vector<std::uint8_t> &out, const PendingShdr &s) {
  AppendU32(out, s.name_off);
  AppendU32(out, s.sh_type);
  AppendU64(out, s.sh_flags);
  AppendU64(out, s.sh_addr);
  AppendU64(out, s.sh_offset);
  AppendU64(out, s.sh_size);
  AppendU32(out, s.sh_link);
  AppendU32(out, s.sh_info);
  AppendU64(out, s.sh_addralign);
  AppendU64(out, s.sh_entsize);
}

} // namespace

// ---------------------------------------------------------------------------
// Public stub helpers
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> BuildStartStubX86_64() { return StubX86_64(); }
std::vector<std::uint8_t> BuildStartStubArm64()  { return StubArm64();  }

// ---------------------------------------------------------------------------
// BuildELFImage
// ---------------------------------------------------------------------------

BuildResult BuildELFImage(const BuildRequest &req) {
  BuildResult result;

  // ---- Compose .text payload (= start stub || user main) -----------------
  std::vector<std::uint8_t> text;
  text.reserve(kStartStubSize + req.text.size());
  std::vector<std::uint8_t> stub =
      (req.arch == Arch::kAArch64) ? StubArm64() : StubX86_64();
  text.insert(text.end(), stub.begin(), stub.end());
  text.insert(text.end(), req.text.begin(), req.text.end());

  // ---- Phase 1: file offsets and virtual addresses -----------------------
  // Read/execute segment lays out as:
  //   offset 0                    : Ehdr (64)
  //   offset 64                   : 4 * Phdr (224)
  //   offset rx_payload_off       : .text bytes
  //   offset rx_payload_off + |t| : .rodata bytes (if any)
  //
  // The R/X PT_LOAD covers the entire span [0 .. end_of_rodata) so the
  // ELF header itself is mapped read+exec, matching what `ld --static`
  // does for a `-N`-style image.
  const std::uint64_t rx_payload_off =
      static_cast<std::uint64_t>(kEhdr64Size + kNumPhdrs * kPhdr64Size);
  const std::uint64_t text_file_off  = rx_payload_off;
  const std::uint64_t text_vaddr     = req.base_address + text_file_off;
  const std::uint64_t text_size      = text.size();

  const std::uint64_t rodata_file_off = text_file_off + text_size;
  const std::uint64_t rodata_vaddr    = req.base_address + rodata_file_off;
  const std::uint64_t rodata_size     = req.rodata.size();

  const std::uint64_t rx_end_off  = rodata_file_off + rodata_size;
  const std::uint64_t rx_load_filesz = rx_end_off; // segment starts at 0

  // R/W segment lives on the next page boundary so the kernel can
  // place it at a different VM permission tier than R/X.
  const bool rw_present = !req.data.empty() || req.bss_size != 0;
  const std::uint64_t rw_file_off = AlignUp(rx_end_off, kPageSize);
  // Per ELF spec, p_offset and p_vaddr must be congruent modulo
  // p_align.  Because rw_file_off is page-aligned we choose a vaddr
  // that is also page-aligned but lives one page above the R/X
  // segment's last touched byte.
  const std::uint64_t rw_vaddr =
      AlignUp(req.base_address + rx_end_off + kPageSize, kPageSize);
  const std::uint64_t rw_data_size = req.data.size();
  const std::uint64_t rw_filesz    = rw_present ? rw_data_size : 0;
  const std::uint64_t rw_memsz     = rw_present ? rw_data_size + req.bss_size : 0;

  // Section header table follows the last loadable byte.
  const std::uint64_t after_rw_off =
      rw_present ? (rw_file_off + rw_data_size) : rx_end_off;

  // .shstrtab blob (NUL + names + NUL terminators).
  // Layout: \0 .shstrtab\0 .text\0 .rodata\0 .data\0 .bss\0
  std::vector<std::uint8_t> shstrtab;
  shstrtab.push_back(0);
  auto intern = [&shstrtab](const char *name) -> std::uint32_t {
    auto off = static_cast<std::uint32_t>(shstrtab.size());
    while (*name) shstrtab.push_back(static_cast<std::uint8_t>(*name++));
    shstrtab.push_back(0);
    return off;
  };
  const std::uint32_t name_shstrtab = intern(".shstrtab");
  const std::uint32_t name_text     = intern(".text");
  const std::uint32_t name_rodata   = rodata_size ? intern(".rodata") : 0;
  const std::uint32_t name_data     = rw_data_size ? intern(".data")  : 0;
  const std::uint32_t name_bss      = req.bss_size ? intern(".bss")   : 0;

  const std::uint64_t shstrtab_off  = after_rw_off;
  const std::uint64_t shstrtab_size = shstrtab.size();
  const std::uint64_t shdr_off =
      AlignUp(shstrtab_off + shstrtab_size, 8u);

  // Section count: SHN_UNDEF + .text + (.rodata?) + (.data?) + (.bss?) +
  // .shstrtab.
  std::uint16_t shnum = 2; // null + .text
  if (rodata_size)        ++shnum;
  if (rw_data_size)       ++shnum;
  if (req.bss_size)       ++shnum;
  ++shnum;                 // .shstrtab
  const std::uint16_t shstrndx = static_cast<std::uint16_t>(shnum - 1);

  const std::uint64_t total_image_size = shdr_off + shnum * kShdr64Size;

  // ---- Phase 2: emit bytes ------------------------------------------------
  std::vector<std::uint8_t> &img = result.image;
  img.reserve(total_image_size);

  // Ehdr (64 bytes).
  AppendU8(img, kElfMag0);
  AppendU8(img, kElfMag1);
  AppendU8(img, kElfMag2);
  AppendU8(img, kElfMag3);
  AppendU8(img, kElfClass64);
  AppendU8(img, kElfData2Lsb);
  AppendU8(img, kEvCurrent);
  AppendU8(img, 0);            // EI_OSABI = SYSV
  AppendU8(img, 0);            // EI_ABIVERSION
  for (int i = 0; i < 7; ++i)  // EI_PAD
    AppendU8(img, 0);
  AppendU16(img, kEtExec);
  AppendU16(img, req.arch == Arch::kAArch64 ? kEmAarch64 : kEmX86_64);
  AppendU32(img, kEvCurrent);
  AppendU64(img, text_vaddr);   // e_entry — points at the start stub
  AppendU64(img, kEhdr64Size);  // e_phoff
  AppendU64(img, shdr_off);     // e_shoff
  AppendU32(img, 0);            // e_flags
  AppendU16(img, kEhdr64Size);
  AppendU16(img, kPhdr64Size);
  AppendU16(img, kNumPhdrs);
  AppendU16(img, kShdr64Size);
  AppendU16(img, shnum);
  AppendU16(img, shstrndx);

  // Phdr[0] = PT_PHDR
  AppendU32(img, kPtPhdr);
  AppendU32(img, kPfR);
  AppendU64(img, kEhdr64Size);                 // p_offset
  AppendU64(img, req.base_address + kEhdr64Size); // p_vaddr
  AppendU64(img, req.base_address + kEhdr64Size); // p_paddr
  AppendU64(img, kNumPhdrs * kPhdr64Size);
  AppendU64(img, kNumPhdrs * kPhdr64Size);
  AppendU64(img, 8);

  // Phdr[1] = PT_LOAD R+X (covers ELF header + phdrs + .text + .rodata)
  AppendU32(img, kPtLoad);
  AppendU32(img, kPfR | kPfX);
  AppendU64(img, 0);                       // p_offset
  AppendU64(img, req.base_address);        // p_vaddr
  AppendU64(img, req.base_address);        // p_paddr
  AppendU64(img, rx_load_filesz);          // p_filesz
  AppendU64(img, rx_load_filesz);          // p_memsz
  AppendU64(img, kPageSize);               // p_align

  // Phdr[2] = PT_LOAD R+W (covers .data + .bss).  Always emitted so the
  // phdr count stays constant across configurations; sizes collapse to
  // zero when the request carries no R/W payload.
  AppendU32(img, kPtLoad);
  AppendU32(img, kPfR | kPfW);
  AppendU64(img, rw_present ? rw_file_off : 0);
  AppendU64(img, rw_present ? rw_vaddr    : 0);
  AppendU64(img, rw_present ? rw_vaddr    : 0);
  AppendU64(img, rw_filesz);
  AppendU64(img, rw_memsz);
  AppendU64(img, kPageSize);

  // Phdr[3] = PT_GNU_STACK (non-executable stack hint).
  AppendU32(img, kPtGnuStack);
  AppendU32(img, kPfR | kPfW);
  AppendU64(img, 0);
  AppendU64(img, 0);
  AppendU64(img, 0);
  AppendU64(img, 0);
  AppendU64(img, 0);
  AppendU64(img, 0x10);

  // .text bytes follow immediately after the phdr table.
  PadTo(img, text_file_off);
  img.insert(img.end(), text.begin(), text.end());

  // .rodata bytes (if any).
  if (rodata_size) {
    img.insert(img.end(), req.rodata.begin(), req.rodata.end());
  }

  // .data bytes (if any), on the next page.
  if (rw_present && rw_data_size) {
    PadTo(img, rw_file_off);
    img.insert(img.end(), req.data.begin(), req.data.end());
  } else if (rw_present) {
    // R/W segment is BSS-only; still need the file offset to exist
    // so loaders that mmap p_filesz==0 can resolve the segment.
    PadTo(img, rw_file_off);
  }

  // .shstrtab blob.
  PadTo(img, shstrtab_off);
  img.insert(img.end(), shstrtab.begin(), shstrtab.end());

  // Section header table (8-byte aligned).
  PadTo(img, shdr_off);

  // [0] SHN_UNDEF
  EmitShdr(img, PendingShdr{});

  // [1] .text
  {
    PendingShdr s{};
    s.name_off  = name_text;
    s.sh_type   = kShtProgBits;
    s.sh_flags  = kShfAlloc | kShfExecInst;
    s.sh_addr   = text_vaddr;
    s.sh_offset = text_file_off;
    s.sh_size   = text_size;
    s.sh_addralign = 16;
    EmitShdr(img, s);
  }

  // [2] .rodata
  if (rodata_size) {
    PendingShdr s{};
    s.name_off  = name_rodata;
    s.sh_type   = kShtProgBits;
    s.sh_flags  = kShfAlloc;
    s.sh_addr   = rodata_vaddr;
    s.sh_offset = rodata_file_off;
    s.sh_size   = rodata_size;
    s.sh_addralign = 8;
    EmitShdr(img, s);
  }

  // [3] .data
  if (rw_data_size) {
    PendingShdr s{};
    s.name_off  = name_data;
    s.sh_type   = kShtProgBits;
    s.sh_flags  = kShfAlloc | kShfWrite;
    s.sh_addr   = rw_vaddr;
    s.sh_offset = rw_file_off;
    s.sh_size   = rw_data_size;
    s.sh_addralign = 8;
    EmitShdr(img, s);
  }

  // [4] .bss
  if (req.bss_size) {
    PendingShdr s{};
    s.name_off  = name_bss;
    s.sh_type   = kShtNoBits;
    s.sh_flags  = kShfAlloc | kShfWrite;
    s.sh_addr   = rw_vaddr + rw_data_size;
    s.sh_offset = rw_file_off + rw_data_size;
    s.sh_size   = req.bss_size;
    s.sh_addralign = 8;
    EmitShdr(img, s);
  }

  // [last] .shstrtab
  {
    PendingShdr s{};
    s.name_off  = name_shstrtab;
    s.sh_type   = kShtStrTab;
    s.sh_offset = shstrtab_off;
    s.sh_size   = shstrtab_size;
    s.sh_addralign = 1;
    EmitShdr(img, s);
  }

  result.entry_vaddr      = text_vaddr;
  result.text_vaddr       = text_vaddr;
  result.text_file_offset = text_file_off;
  return result;
}

} // namespace polyglot::linker::elf
