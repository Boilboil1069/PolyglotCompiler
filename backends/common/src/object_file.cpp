/**
 * @file     object_file.cpp
 * @brief    Shared backend implementation
 *
 * @ingroup  Backend / Common
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <algorithm>
#include <cstring>
#include <unordered_map>

#include "backends/common/include/object_file.h"

namespace polyglot::backends {
namespace {

// ELF constants
constexpr std::uint8_t ELFCLASS64 = 2;
constexpr std::uint8_t ELFDATA2LSB = 1;
constexpr std::uint8_t EV_CURRENT = 1;
constexpr std::uint16_t ET_REL = 1;
constexpr std::uint16_t EM_X86_64 = 62;
constexpr std::uint16_t EM_AARCH64 = 183;

constexpr std::uint32_t SHT_NULL [[maybe_unused]] = 0;
constexpr std::uint32_t SHT_PROGBITS = 1;
constexpr std::uint32_t SHT_SYMTAB = 2;
constexpr std::uint32_t SHT_STRTAB = 3;
constexpr std::uint32_t SHT_RELA = 4;

constexpr std::uint64_t SHF_WRITE = 0x1;
constexpr std::uint64_t SHF_ALLOC = 0x2;
constexpr std::uint64_t SHF_EXECINSTR = 0x4;
constexpr std::uint64_t SHF_INFO_LINK = 0x40;

constexpr std::uint8_t STB_LOCAL = 0;
constexpr std::uint8_t STB_GLOBAL = 1;
constexpr std::uint8_t STT_NOTYPE = 0;
constexpr std::uint8_t STT_FUNC = 2;

template <typename T> void WriteValue(std::vector<std::uint8_t> &out, T value) {
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF));
  }
}

void WriteBytes(std::vector<std::uint8_t> &out, const void *data, std::size_t size) {
  const auto *bytes = static_cast<const std::uint8_t *>(data);
  out.insert(out.end(), bytes, bytes + size);
}

void AlignTo(std::vector<std::uint8_t> &out, std::size_t alignment) {
  while (out.size() % alignment != 0) {
    out.push_back(0);
  }
}

} // namespace

void ELFBuilder::AddSection(const Section &section) {
  sections_.push_back(section);
}

void ELFBuilder::AddSymbol(const Symbol &symbol) {
  symbols_.push_back(symbol);
}

std::vector<std::uint8_t> ELFBuilder::Build() {
  std::vector<std::uint8_t> result;

  // Build string table
  std::vector<std::string> string_table;
  std::vector<std::uint32_t> section_name_offsets;
  std::uint32_t strtab_size = 1; // Start with null byte

  string_table.push_back("");
  for (const auto &sec : sections_) {
    section_name_offsets.push_back(strtab_size);
    strtab_size += static_cast<std::uint32_t>(sec.name.size()) + 1;
    string_table.push_back(sec.name);
  }

  // Add .symtab and .strtab section names
  std::uint32_t symtab_name_offset = strtab_size;
  strtab_size += 8; // ".symtab"
  string_table.push_back(".symtab");

  std::uint32_t symstrtab_name_offset = strtab_size;
  strtab_size += 8; // ".strtab"
  string_table.push_back(".strtab");

  std::uint32_t shstrtab_name_offset = strtab_size;
  strtab_size += 10; // ".shstrtab"
  string_table.push_back(".shstrtab");

  // Track which sections have relocations and add their names
  std::vector<std::uint32_t> rela_name_offsets;
  std::vector<std::size_t> rela_section_indices; // original section index
  for (std::size_t i = 0; i < sections_.size(); ++i) {
    if (!sections_[i].relocations.empty()) {
      rela_section_indices.push_back(i);
      rela_name_offsets.push_back(strtab_size);
      std::string rela_name = ".rela" + sections_[i].name;
      strtab_size += static_cast<std::uint32_t>(rela_name.size()) + 1;
      string_table.push_back(rela_name);
    }
  }

  // ELF header
  std::uint8_t e_ident[16] = {0};
  e_ident[0] = 0x7F;
  e_ident[1] = 'E';
  e_ident[2] = 'L';
  e_ident[3] = 'F';
  e_ident[4] = ELFCLASS64;
  e_ident[5] = ELFDATA2LSB;
  e_ident[6] = EV_CURRENT;
  WriteBytes(result, e_ident, 16);

  WriteValue<std::uint16_t>(result, ET_REL);                           // e_type
  WriteValue<std::uint16_t>(result, is_x64_ ? EM_X86_64 : EM_AARCH64); // e_machine
  WriteValue<std::uint32_t>(result, EV_CURRENT);                       // e_version
  WriteValue<std::uint64_t>(result, 0);                                // e_entry
  WriteValue<std::uint64_t>(result, 0);                                // e_phoff

  // e_shoff will be filled later
  std::size_t e_shoff_pos = result.size();
  WriteValue<std::uint64_t>(result, 0);

  WriteValue<std::uint32_t>(result, 0);  // e_flags
  WriteValue<std::uint16_t>(result, 64); // e_ehsize
  WriteValue<std::uint16_t>(result, 0);  // e_phentsize
  WriteValue<std::uint16_t>(result, 0);  // e_phnum
  WriteValue<std::uint16_t>(result, 64); // e_shentsize

  std::uint16_t shnum = static_cast<std::uint16_t>(
      sections_.size() + 4 +
      rela_section_indices.size());         // +null, symtab, strtab, shstrtab, rela sections
  WriteValue<std::uint16_t>(result, shnum); // e_shnum
  WriteValue<std::uint16_t>(result, static_cast<std::uint16_t>(shnum - 1)); // e_shstrndx

  // Section data
  std::vector<std::uint64_t> section_offsets;
  for (const auto &sec : sections_) {
    AlignTo(result, 16);
    section_offsets.push_back(result.size());
    result.insert(result.end(), sec.data.begin(), sec.data.end());
  }

  // Write relocation (RELA) section data
  std::vector<std::uint64_t> rela_offsets;
  std::vector<std::uint64_t> rela_sizes;
  for (std::size_t ri = 0; ri < rela_section_indices.size(); ++ri) {
    const auto &sec = sections_[rela_section_indices[ri]];
    AlignTo(result, 8);
    rela_offsets.push_back(result.size());
    for (const auto &rel : sec.relocations) {
      WriteValue<std::uint64_t>(result, rel.offset); // r_offset
      // Build r_info: find symbol index
      std::uint64_t sym_idx = 0;
      for (std::size_t si = 0; si < symbols_.size(); ++si) {
        if (symbols_[si].name == rel.symbol) {
          sym_idx = si + 1; // +1 because null symbol at 0
          break;
        }
      }
      std::uint64_t r_info = (sym_idx << 32) | static_cast<std::uint32_t>(rel.type);
      WriteValue<std::uint64_t>(result, r_info);    // r_info
      WriteValue<std::int64_t>(result, rel.addend); // r_addend
    }
    rela_sizes.push_back(result.size() - rela_offsets.back());
  }

  // Symbol string table
  AlignTo(result, 8);
  std::uint64_t sym_strtab_offset = result.size();
  std::vector<std::uint32_t> symbol_name_offsets;
  std::uint32_t sym_strtab_size = 1;
  result.push_back(0); // null byte

  for (const auto &sym : symbols_) {
    symbol_name_offsets.push_back(sym_strtab_size);
    result.insert(result.end(), sym.name.begin(), sym.name.end());
    result.push_back(0);
    sym_strtab_size += static_cast<std::uint32_t>(sym.name.size()) + 1;
  }

  // Symbol table
  AlignTo(result, 8);
  std::uint64_t symtab_offset = result.size();

  // Null symbol
  WriteValue<std::uint32_t>(result, 0); // st_name
  WriteValue<std::uint8_t>(result, 0);  // st_info
  WriteValue<std::uint8_t>(result, 0);  // st_other
  WriteValue<std::uint16_t>(result, 0); // st_shndx
  WriteValue<std::uint64_t>(result, 0); // st_value
  WriteValue<std::uint64_t>(result, 0); // st_size

  // Actual symbols
  for (std::size_t i = 0; i < symbols_.size(); ++i) {
    const auto &sym = symbols_[i];
    std::uint8_t st_info = (sym.is_global ? STB_GLOBAL : STB_LOCAL) << 4;
    st_info |= (sym.is_function ? STT_FUNC : STT_NOTYPE);

    // Find section index
    std::uint16_t st_shndx = 0;
    for (std::size_t si = 0; si < sections_.size(); ++si) {
      if (sections_[si].name == sym.section) {
        st_shndx = static_cast<std::uint16_t>(si + 1); // +1 for null section
        break;
      }
    }

    WriteValue<std::uint32_t>(result, symbol_name_offsets[i]);
    WriteValue<std::uint8_t>(result, st_info);
    WriteValue<std::uint8_t>(result, 0); // st_other
    WriteValue<std::uint16_t>(result, st_shndx);
    WriteValue<std::uint64_t>(result, sym.offset);
    WriteValue<std::uint64_t>(result, sym.size);
  }

  std::uint64_t symtab_size = result.size() - symtab_offset;

  // Section header string table
  AlignTo(result, 8);
  std::uint64_t shstrtab_offset = result.size();
  result.push_back(0);
  for (const auto &str : string_table) {
    result.insert(result.end(), str.begin(), str.end());
    result.push_back(0);
  }
  std::uint64_t shstrtab_size = result.size() - shstrtab_offset;

  // Section headers
  AlignTo(result, 8);
  std::uint64_t sh_offset = result.size();

  // Update e_shoff
  for (std::size_t i = 0; i < 8; ++i) {
    result[e_shoff_pos + i] = static_cast<std::uint8_t>((sh_offset >> (i * 8)) & 0xFF);
  }

  // Null section header
  for (int i = 0; i < 64; ++i)
    result.push_back(0);

  // Regular section headers
  for (std::size_t i = 0; i < sections_.size(); ++i) {
    const auto &sec = sections_[i];
    std::uint32_t sh_type = (sec.name == ".text") ? SHT_PROGBITS : SHT_PROGBITS;
    std::uint64_t sh_flags = 0;
    if (sec.name == ".text")
      sh_flags = SHF_ALLOC | SHF_EXECINSTR;
    else if (sec.name == ".data")
      sh_flags = SHF_ALLOC | SHF_WRITE;

    WriteValue<std::uint32_t>(result, section_name_offsets[i]);
    WriteValue<std::uint32_t>(result, sh_type);
    WriteValue<std::uint64_t>(result, sh_flags);
    WriteValue<std::uint64_t>(result, 0); // sh_addr
    WriteValue<std::uint64_t>(result, section_offsets[i]);
    WriteValue<std::uint64_t>(result, sec.data.size());
    WriteValue<std::uint32_t>(result, 0);  // sh_link
    WriteValue<std::uint32_t>(result, 0);  // sh_info
    WriteValue<std::uint64_t>(result, 16); // sh_addralign
    WriteValue<std::uint64_t>(result, 0);  // sh_entsize
  }

  // .symtab section header
  WriteValue<std::uint32_t>(result, symtab_name_offset);
  WriteValue<std::uint32_t>(result, SHT_SYMTAB);
  WriteValue<std::uint64_t>(result, 0); // sh_flags
  WriteValue<std::uint64_t>(result, 0); // sh_addr
  WriteValue<std::uint64_t>(result, symtab_offset);
  WriteValue<std::uint64_t>(result, symtab_size);
  WriteValue<std::uint32_t>(result,
                            static_cast<std::uint32_t>(sections_.size() + 2)); // sh_link (strtab)
  WriteValue<std::uint32_t>(result, 1);  // sh_info (first global symbol)
  WriteValue<std::uint64_t>(result, 8);  // sh_addralign
  WriteValue<std::uint64_t>(result, 24); // sh_entsize

  // .strtab section header
  WriteValue<std::uint32_t>(result, symstrtab_name_offset);
  WriteValue<std::uint32_t>(result, SHT_STRTAB);
  WriteValue<std::uint64_t>(result, 0); // sh_flags
  WriteValue<std::uint64_t>(result, 0); // sh_addr
  WriteValue<std::uint64_t>(result, sym_strtab_offset);
  WriteValue<std::uint64_t>(result, sym_strtab_size);
  WriteValue<std::uint32_t>(result, 0); // sh_link
  WriteValue<std::uint32_t>(result, 0); // sh_info
  WriteValue<std::uint64_t>(result, 1); // sh_addralign
  WriteValue<std::uint64_t>(result, 0); // sh_entsize

  // .shstrtab section header
  WriteValue<std::uint32_t>(result, shstrtab_name_offset);
  WriteValue<std::uint32_t>(result, SHT_STRTAB);
  WriteValue<std::uint64_t>(result, 0); // sh_flags
  WriteValue<std::uint64_t>(result, 0); // sh_addr
  WriteValue<std::uint64_t>(result, shstrtab_offset);
  WriteValue<std::uint64_t>(result, shstrtab_size);
  WriteValue<std::uint32_t>(result, 0); // sh_link
  WriteValue<std::uint32_t>(result, 0); // sh_info
  WriteValue<std::uint64_t>(result, 1); // sh_addralign
  WriteValue<std::uint64_t>(result, 0); // sh_entsize

  // .rela section headers
  for (std::size_t ri = 0; ri < rela_section_indices.size(); ++ri) {
    std::uint32_t target_shndx =
        static_cast<std::uint32_t>(rela_section_indices[ri] + 1);                  // +1 for null
    std::uint32_t symtab_shndx = static_cast<std::uint32_t>(sections_.size() + 1); // .symtab index
    WriteValue<std::uint32_t>(result, rela_name_offsets[ri]);
    WriteValue<std::uint32_t>(result, SHT_RELA);
    WriteValue<std::uint64_t>(result, SHF_INFO_LINK);    // sh_flags
    WriteValue<std::uint64_t>(result, 0);                // sh_addr
    WriteValue<std::uint64_t>(result, rela_offsets[ri]); // sh_offset
    WriteValue<std::uint64_t>(result, rela_sizes[ri]);   // sh_size
    WriteValue<std::uint32_t>(result, symtab_shndx);     // sh_link -> .symtab
    WriteValue<std::uint32_t>(result, target_shndx);     // sh_info -> target section
    WriteValue<std::uint64_t>(result, 8);                // sh_addralign
    WriteValue<std::uint64_t>(result, 24);               // sh_entsize (sizeof Elf64_Rela)
  }

  return result;
}

void MachOBuilder::AddSection(const Section &section) {
  sections_.push_back(section);
}

void MachOBuilder::AddSymbol(const Symbol &symbol) {
  symbols_.push_back(symbol);
}

std::vector<std::uint8_t> MachOBuilder::Build() {
  std::vector<std::uint8_t> result;

  // Mach-O constants
  constexpr std::uint32_t MH_MAGIC_64 = 0xFEEDFACF;
  constexpr std::uint32_t MH_OBJECT = 1;
  constexpr std::uint32_t CPU_TYPE_X86_64 = 0x01000007;
  constexpr std::uint32_t CPU_TYPE_ARM64 = 0x0100000C;
  constexpr std::uint32_t CPU_SUBTYPE_X86_64_ALL = 3;
  constexpr std::uint32_t CPU_SUBTYPE_ARM64_ALL = 0;
  constexpr std::uint32_t LC_SEGMENT_64 = 0x19;
  constexpr std::uint32_t LC_SYMTAB = 0x02;
  constexpr std::uint32_t LC_BUILD_VERSION = 0x32;

  // Compute section data layout
  // Header: 32 bytes
  // LC_SEGMENT_64: 72 bytes + 80 bytes per section
  // LC_SYMTAB: 24 bytes
  // LC_BUILD_VERSION: 24 bytes (no tool entries)
  std::uint32_t segment_cmd_size = 72 + static_cast<std::uint32_t>(80 * sections_.size());
  std::uint32_t symtab_cmd_size = 24;
  std::uint32_t build_version_cmd_size = 24;
  std::uint32_t ncmds = 3;
  std::uint32_t sizeofcmds = segment_cmd_size + symtab_cmd_size + build_version_cmd_size;
  std::uint32_t header_size = 32 + sizeofcmds;

  // Align section data start to 16 bytes
  std::uint32_t data_start = (header_size + 15) & ~15u;

  // Compute section offsets
  std::vector<std::uint32_t> sec_offsets;
  std::uint32_t current_offset = data_start;
  for (const auto &sec : sections_) {
    sec_offsets.push_back(current_offset);
    current_offset += static_cast<std::uint32_t>(sec.data.size());
    current_offset = (current_offset + 15) & ~15u;
  }

  // Compute relocation table offsets (placed after all section data).
  // Each Mach-O relocation_info entry is 8 bytes.
  std::vector<std::uint32_t> sec_reloff(sections_.size(), 0);
  std::vector<std::uint32_t> sec_nreloc(sections_.size(), 0);
  std::uint32_t reloc_area_offset = current_offset;
  (void)reloc_area_offset; // Retained for layout documentation; not yet referenced.
  for (std::size_t i = 0; i < sections_.size(); ++i) {
    if (!sections_[i].relocations.empty()) {
      sec_reloff[i] = current_offset;
      sec_nreloc[i] = static_cast<std::uint32_t>(sections_[i].relocations.size());
      current_offset += sec_nreloc[i] * 8;
      current_offset = (current_offset + 3) & ~3u; // 4-byte align
    }
  }

  // Symbol and string table after sections and relocations
  std::uint32_t strtab_offset = current_offset;
  std::vector<std::uint8_t> strtab;
  strtab.push_back(0x20); // Start with space (Mach-O convention: first byte pad)
  strtab.push_back(0);    // Null terminator for empty string

  std::vector<std::uint32_t> sym_str_offsets;
  for (const auto &sym : symbols_) {
    // Mach-O symbols start with '_' prefix
    sym_str_offsets.push_back(static_cast<std::uint32_t>(strtab.size()));
    strtab.push_back('_');
    strtab.insert(strtab.end(), sym.name.begin(), sym.name.end());
    strtab.push_back(0);
  }

  std::uint32_t strtab_size = static_cast<std::uint32_t>(strtab.size());
  std::uint32_t symtab_offset = strtab_offset + ((strtab_size + 7) & ~7u);
  // Each nlist_64 entry is 16 bytes
  std::uint32_t symtab_size = static_cast<std::uint32_t>(symbols_.size()) * 16;
  (void)symtab_size; // Retained for layout documentation; not yet referenced.

  /** @name Write Mach-O header (mach_header_64) */
  /** @{ */
  WriteValue<std::uint32_t>(result, MH_MAGIC_64);
  WriteValue<std::uint32_t>(result, is_arm64_ ? CPU_TYPE_ARM64 : CPU_TYPE_X86_64);
  WriteValue<std::uint32_t>(result, is_arm64_ ? CPU_SUBTYPE_ARM64_ALL : CPU_SUBTYPE_X86_64_ALL);
  WriteValue<std::uint32_t>(result, MH_OBJECT);
  WriteValue<std::uint32_t>(result, ncmds);
  WriteValue<std::uint32_t>(result, sizeofcmds);
  WriteValue<std::uint32_t>(result, 0); // flags
  WriteValue<std::uint32_t>(result, 0); // reserved

  /** @} */

  /** @name LC_SEGMENT_64 */
  /** @{ */
  WriteValue<std::uint32_t>(result, LC_SEGMENT_64);
  WriteValue<std::uint32_t>(result, segment_cmd_size);

  // segname (16 bytes, empty for object files)
  for (int i = 0; i < 16; ++i)
    result.push_back(0);

  WriteValue<std::uint64_t>(result, 0); // vmaddr
  std::uint64_t vmsize = current_offset > data_start ? (current_offset - data_start) : 0;
  WriteValue<std::uint64_t>(result, vmsize);                                       // vmsize
  WriteValue<std::uint64_t>(result, data_start);                                   // fileoff
  WriteValue<std::uint64_t>(result, vmsize);                                       // filesize
  WriteValue<std::uint32_t>(result, 7);                                            // maxprot (rwx)
  WriteValue<std::uint32_t>(result, 7);                                            // initprot (rwx)
  WriteValue<std::uint32_t>(result, static_cast<std::uint32_t>(sections_.size())); // nsects
  WriteValue<std::uint32_t>(result, 0);                                            // flags

  // Section headers within LC_SEGMENT_64 (each 80 bytes)
  for (std::size_t i = 0; i < sections_.size(); ++i) {
    const auto &sec = sections_[i];

    // sectname (16 bytes) - map common names to Mach-O conventions
    char sectname[16] = {};
    char segname[16] = {};
    if (sec.name == ".text") {
      std::memcpy(sectname, "__text", 6);
      std::memcpy(segname, "__TEXT", 6);
    } else if (sec.name == ".data") {
      std::memcpy(sectname, "__data", 6);
      std::memcpy(segname, "__DATA", 6);
    } else if (sec.name == ".bss") {
      std::memcpy(sectname, "__bss", 5);
      std::memcpy(segname, "__DATA", 6);
    } else if (sec.name == ".rodata" || sec.name == ".const") {
      std::memcpy(sectname, "__const", 7);
      std::memcpy(segname, "__TEXT", 6);
    } else {
      // Generic mapping
      std::string mapped = sec.name.substr(0, std::min(sec.name.size(), std::size_t(15)));
      std::memcpy(sectname, mapped.c_str(), mapped.size());
      std::memcpy(segname, "__DATA", 6);
    }

    WriteBytes(result, sectname, 16);
    WriteBytes(result, segname, 16);

    WriteValue<std::uint64_t>(result, 0);               // addr
    WriteValue<std::uint64_t>(result, sec.data.size()); // size
    WriteValue<std::uint32_t>(result, sec_offsets[i]);  // offset
    WriteValue<std::uint32_t>(result, 4);               // align (2^4 = 16)
    WriteValue<std::uint32_t>(result, sec_reloff[i]);   // reloff
    WriteValue<std::uint32_t>(result, sec_nreloc[i]);   // nreloc

    // Section flags
    std::uint32_t sec_flags = 0;
    if (sec.name == ".text") {
      sec_flags = 0x80000400; // S_REGULAR | S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS
    }
    WriteValue<std::uint32_t>(result, sec_flags);

    WriteValue<std::uint32_t>(result, 0); // reserved1
    WriteValue<std::uint32_t>(result, 0); // reserved2
    WriteValue<std::uint32_t>(result, 0); // reserved3 (padding for 64-bit)
  }

  /** @} */

  /** @name LC_SYMTAB */
  /** @{ */
  WriteValue<std::uint32_t>(result, LC_SYMTAB);
  WriteValue<std::uint32_t>(result, symtab_cmd_size);
  WriteValue<std::uint32_t>(result, symtab_offset);                               // symoff
  WriteValue<std::uint32_t>(result, static_cast<std::uint32_t>(symbols_.size())); // nsyms
  WriteValue<std::uint32_t>(result, strtab_offset);                               // stroff
  WriteValue<std::uint32_t>(result, strtab_size);                                 // strsize

  /** @} */

  /** @name LC_BUILD_VERSION */
  /** @{ */
  // Declares the platform (macOS / Linux) so ld does not warn.
  WriteValue<std::uint32_t>(result, LC_BUILD_VERSION);
  WriteValue<std::uint32_t>(result, build_version_cmd_size);
  WriteValue<std::uint32_t>(result, 1);          // platform: PLATFORM_MACOS = 1
  WriteValue<std::uint32_t>(result, 0x000E0000); // minos: 14.0 encoded as 0x000E0000
  WriteValue<std::uint32_t>(result, 0);          // sdk: 0 (no specific SDK)
  WriteValue<std::uint32_t>(result, 0);          // ntools: 0

  /** @} */

  /** @name Pad to data start */
  /** @{ */
  while (result.size() < data_start) {
    result.push_back(0);
  }

  /** @} */

  /** @name Section data */
  /** @{ */
  for (std::size_t i = 0; i < sections_.size(); ++i) {
    while (result.size() < sec_offsets[i])
      result.push_back(0);
    result.insert(result.end(), sections_[i].data.begin(), sections_[i].data.end());
  }

  /** @} */

  /** @name Relocation entries (relocation_info, 8 bytes each) */
  /** @{ */
  // Build a symbol name → symbol table index map for relocation lookups.
  std::unordered_map<std::string, std::uint32_t> sym_name_index;
  for (std::uint32_t si = 0; si < symbols_.size(); ++si) {
    sym_name_index[symbols_[si].name] = si;
  }
  for (std::size_t i = 0; i < sections_.size(); ++i) {
    if (sections_[i].relocations.empty())
      continue;
    while (result.size() < sec_reloff[i])
      result.push_back(0);
    for (const auto &rel : sections_[i].relocations) {
      // r_address (offset within section)
      WriteValue<std::uint32_t>(result, static_cast<std::uint32_t>(rel.offset));
      // Packed: r_symbolnum(24) | r_pcrel(1) | r_length(2) | r_extern(1) | r_type(4)
      std::uint32_t r_symbolnum = 0;
      auto sit = sym_name_index.find(rel.symbol);
      if (sit != sym_name_index.end()) {
        r_symbolnum = sit->second;
      }
      // Determine relocation encoding from the generic type hint.
      //   type 1 = branch/PC-relative  →  4-byte instruction fixup
      //   type 0 = absolute pointer     →  8-byte data fixup
      std::uint32_t r_pcrel;
      std::uint32_t r_length;
      std::uint32_t r_type;
      if (rel.type == 1) {
        // PC-relative branch
        r_pcrel = 1;
        r_length = 2; // 2^2 = 4 bytes
        // ARM64_RELOC_BRANCH26 = 2, X86_64_RELOC_BRANCH = 2
        r_type = 2;
      } else {
        // Absolute pointer
        r_pcrel = 0;
        r_length = 3; // 2^3 = 8 bytes
        // ARM64_RELOC_UNSIGNED = 0, X86_64_RELOC_UNSIGNED = 0
        r_type = 0;
      }
      std::uint32_t r_extern = 1;
      std::uint32_t packed = (r_symbolnum & 0x00FFFFFFu) | (r_pcrel << 24) | (r_length << 25) |
                             (r_extern << 27) | (r_type << 28);
      WriteValue<std::uint32_t>(result, packed);
    }
  }

  /** @} */

  /** @name String table */
  /** @{ */
  while (result.size() < strtab_offset)
    result.push_back(0);
  result.insert(result.end(), strtab.begin(), strtab.end());

  /** @} */

  /** @name Symbol table (nlist_64) */
  /** @{ */
  AlignTo(result, 8);
  // Adjust symtab_offset if needed to match actual position
  // (the header already points to the computed position)
  while (result.size() < symtab_offset)
    result.push_back(0);

  for (std::size_t i = 0; i < symbols_.size(); ++i) {
    const auto &sym = symbols_[i];

    WriteValue<std::uint32_t>(result, sym_str_offsets[i]); // n_strx

    // n_sect: 1-based section index (0 = NO_SECT for undefined)
    std::uint8_t n_sect = 0;
    for (std::size_t si = 0; si < sections_.size(); ++si) {
      if (sections_[si].name == sym.section) {
        n_sect = static_cast<std::uint8_t>(si + 1);
        break;
      }
    }
    // n_type: N_SECT (0x0e) for defined symbols, N_UNDF (0x00) for undefined
    std::uint8_t n_type;
    if (n_sect != 0) {
      n_type = 0x0e; // N_SECT
    } else {
      n_type = 0x00; // N_UNDF
    }
    if (sym.is_global)
      n_type |= 0x01; // N_EXT
    result.push_back(n_type);
    result.push_back(n_sect);

    WriteValue<std::uint16_t>(result, 0);          // n_desc
    WriteValue<std::uint64_t>(result, sym.offset); // n_value
  }

  return result;
}

// ===========================================================================
// COFFBuilder
//
// Emits a Microsoft COFF object file (IMAGE_FILE_HEADER + IMAGE_SECTION_HEADER
// array + raw section data + per-section relocation tables + symbol table +
// string table). Layout exactly follows §3 of the Microsoft PE/COFF spec
// (revision 11) so MS link.exe and lld-link both treat the result as a
// first-class translation-unit input.
// ===========================================================================

namespace {

#pragma pack(push, 1)
struct CoffFileHeader {
  std::uint16_t Machine;
  std::uint16_t NumberOfSections;
  std::uint32_t TimeDateStamp;
  std::uint32_t PointerToSymbolTable;
  std::uint32_t NumberOfSymbols;
  std::uint16_t SizeOfOptionalHeader;
  std::uint16_t Characteristics;
};
struct CoffSectionHeader {
  char Name[8];
  std::uint32_t VirtualSize;
  std::uint32_t VirtualAddress;
  std::uint32_t SizeOfRawData;
  std::uint32_t PointerToRawData;
  std::uint32_t PointerToRelocations;
  std::uint32_t PointerToLinenumbers;
  std::uint16_t NumberOfRelocations;
  std::uint16_t NumberOfLinenumbers;
  std::uint32_t Characteristics;
};
struct CoffSymbol {
  union {
    char ShortName[8];
    struct {
      std::uint32_t Zeroes;
      std::uint32_t Offset;
    } LongName;
  } Name;
  std::uint32_t Value;
  std::int16_t SectionNumber;
  std::uint16_t Type;
  std::uint8_t StorageClass;
  std::uint8_t NumberOfAuxSymbols;
};
struct CoffRelocation {
  std::uint32_t VirtualAddress;
  std::uint32_t SymbolTableIndex;
  std::uint16_t Type;
};
#pragma pack(pop)

constexpr std::uint16_t kCoffMachineAMD64 = 0x8664;
constexpr std::uint16_t kCoffMachineARM64 = 0xAA64;

// IMAGE_SCN_*
constexpr std::uint32_t kCoffSecText = 0x60000020; // CNT_CODE | MEM_EXECUTE | MEM_READ
constexpr std::uint32_t kCoffSecData = 0xC0000040; // CNT_INITIALIZED_DATA | MEM_READ | MEM_WRITE
constexpr std::uint32_t kCoffSecRData = 0x40000040; // CNT_INITIALIZED_DATA | MEM_READ
constexpr std::uint32_t kCoffSecBss = 0xC0000080;  // CNT_UNINITIALIZED_DATA | MEM_READ | MEM_WRITE

// IMAGE_SYM_CLASS_*
constexpr std::uint8_t kCoffSymExternal = 2;
constexpr std::uint8_t kCoffSymStatic = 3; // also used for section symbols

// IMAGE_REL_AMD64_*
constexpr std::uint16_t kCoffRelocAMD64Addr64 = 0x0001;
constexpr std::uint16_t kCoffRelocAMD64Addr32NB = 0x0003;
constexpr std::uint16_t kCoffRelocAMD64Rel32 = 0x0004;
// IMAGE_REL_ARM64_*
constexpr std::uint16_t kCoffRelocARM64Branch26 = 0x0003;

constexpr std::uint16_t kCoffCharLargeAddr = 0x0020; // IMAGE_FILE_LARGE_ADDRESS_AWARE

// Map a backends::Section name to the appropriate IMAGE_SCN_* characteristics.
std::uint32_t CoffSectionFlags(const std::string &name, bool is_bss) {
  if (is_bss)
    return kCoffSecBss;
  if (name == ".text" || name == ".code" || name == "__text")
    return kCoffSecText;
  if (name == ".rdata" || name == ".rodata" || name == "__const")
    return kCoffSecRData;
  return kCoffSecData;
}

bool LooksLikeBss(const Section &sec) {
  return sec.name == ".bss" || sec.name == "__bss";
}

} // namespace

void COFFBuilder::AddSection(const Section &section) {
  sections_.push_back(section);
}

void COFFBuilder::AddSymbol(const Symbol &symbol) {
  symbols_.push_back(symbol);
}

std::vector<std::uint8_t> COFFBuilder::Build() {
  // ── 1. Section headers (data offsets are filled in below) ────────────────
  std::vector<CoffSectionHeader> sec_headers;
  sec_headers.reserve(sections_.size());

  // String table grows as we encounter long names. The first 4 bytes hold
  // its own size (including the size field).
  std::string strtab(4, '\0');

  std::unordered_map<std::string, std::uint16_t> name_to_idx;

  for (std::size_t i = 0; i < sections_.size(); ++i) {
    const auto &sec = sections_[i];
    CoffSectionHeader sh{};
    std::memset(&sh, 0, sizeof(sh));

    if (sec.name.size() <= 8) {
      std::memcpy(sh.Name, sec.name.data(), sec.name.size());
    } else {
      // COFF long-name encoding: '/' followed by the decimal string-table offset.
      const auto offset = static_cast<std::uint32_t>(strtab.size());
      strtab.append(sec.name);
      strtab.push_back('\0');
      std::string encoded = "/" + std::to_string(offset);
      std::memcpy(sh.Name, encoded.data(), std::min<std::size_t>(encoded.size(), 8));
    }

    const bool bss = LooksLikeBss(sec);
    sh.Characteristics = CoffSectionFlags(sec.name, bss);
    if (bss) {
      sh.SizeOfRawData = 0;
      sh.VirtualSize = static_cast<std::uint32_t>(sec.data.size());
    } else {
      sh.SizeOfRawData = static_cast<std::uint32_t>(sec.data.size());
    }

    name_to_idx[sec.name] = static_cast<std::uint16_t>(sec_headers.size());
    sec_headers.push_back(sh);
  }

  // ── 2. Symbol table ──────────────────────────────────────────────────────
  std::vector<CoffSymbol> coff_syms;
  std::unordered_map<std::string, std::uint32_t> sym_name_to_idx;

  // 2a. Section symbols (one per section, each followed by an aux record).
  for (std::size_t i = 0; i < sections_.size(); ++i) {
    const auto &sec = sections_[i];
    CoffSymbol cs{};
    std::memset(&cs, 0, sizeof(cs));
    if (sec.name.size() <= 8) {
      std::memcpy(cs.Name.ShortName, sec.name.data(), sec.name.size());
    } else {
      cs.Name.LongName.Zeroes = 0;
      cs.Name.LongName.Offset = static_cast<std::uint32_t>(strtab.size());
      strtab.append(sec.name);
      strtab.push_back('\0');
    }
    cs.Value = 0;
    cs.SectionNumber = static_cast<std::int16_t>(i + 1);
    cs.Type = 0;
    cs.StorageClass = kCoffSymStatic;
    cs.NumberOfAuxSymbols = 1;
    coff_syms.push_back(cs);

    // Auxiliary section record: Length, NumberOfRelocations, NumberOfLinenumbers,
    // CheckSum (4), Number (2), Selection (1), then 3 bytes of zero padding.
    CoffSymbol aux{};
    std::memset(&aux, 0, sizeof(aux));
    auto raw = reinterpret_cast<std::uint8_t *>(&aux);
    auto len = static_cast<std::uint32_t>(sec.data.size());
    std::memcpy(raw, &len, 4);
    auto nrelocs = static_cast<std::uint16_t>(sec.relocations.size());
    std::memcpy(raw + 4, &nrelocs, 2);
    coff_syms.push_back(aux);
  }

  // 2b. User symbols (functions / data labels).
  for (const auto &sym : symbols_) {
    CoffSymbol cs{};
    std::memset(&cs, 0, sizeof(cs));
    if (sym.name.size() <= 8) {
      std::memcpy(cs.Name.ShortName, sym.name.data(), sym.name.size());
    } else {
      cs.Name.LongName.Zeroes = 0;
      cs.Name.LongName.Offset = static_cast<std::uint32_t>(strtab.size());
      strtab.append(sym.name);
      strtab.push_back('\0');
    }
    cs.Value = static_cast<std::uint32_t>(sym.offset);
    auto it = name_to_idx.find(sym.section);
    cs.SectionNumber =
        (it != name_to_idx.end()) ? static_cast<std::int16_t>(it->second + 1) : 0;
    cs.Type = sym.is_function ? 0x20 : 0x00;
    cs.StorageClass = sym.is_global ? kCoffSymExternal : kCoffSymStatic;
    sym_name_to_idx[sym.name] = static_cast<std::uint32_t>(coff_syms.size());
    coff_syms.push_back(cs);
  }

  // Patch the string-table prefix (first 4 bytes = total size including itself).
  const auto strtab_size = static_cast<std::uint32_t>(strtab.size());
  std::memcpy(strtab.data(), &strtab_size, 4);

  // ── 3. Per-section relocation tables ─────────────────────────────────────
  std::vector<std::vector<CoffRelocation>> sec_relocs(sec_headers.size());
  for (std::size_t i = 0; i < sections_.size(); ++i) {
    for (const auto &r : sections_[i].relocations) {
      CoffRelocation cr{};
      cr.VirtualAddress = static_cast<std::uint32_t>(r.offset);
      auto it = sym_name_to_idx.find(r.symbol);
      cr.SymbolTableIndex = (it != sym_name_to_idx.end()) ? it->second : 0u;
      if (is_arm64_) {
        cr.Type = kCoffRelocARM64Branch26;
      } else {
        // r.type 0 -> 64-bit absolute, 1 -> rel32 (PC-relative).
        cr.Type = (r.type == 0) ? kCoffRelocAMD64Addr64 : kCoffRelocAMD64Rel32;
      }
      sec_relocs[i].push_back(cr);
    }
  }

  // ── 4. Compute file offsets ──────────────────────────────────────────────
  std::size_t cursor =
      sizeof(CoffFileHeader) + sec_headers.size() * sizeof(CoffSectionHeader);

  for (std::size_t i = 0; i < sec_headers.size(); ++i) {
    const auto &sec = sections_[i];
    if (LooksLikeBss(sec)) {
      sec_headers[i].PointerToRawData = 0;
    } else {
      sec_headers[i].PointerToRawData = static_cast<std::uint32_t>(cursor);
      cursor += sec.data.size();
    }
  }
  for (std::size_t i = 0; i < sec_headers.size(); ++i) {
    if (!sec_relocs[i].empty()) {
      sec_headers[i].PointerToRelocations = static_cast<std::uint32_t>(cursor);
      sec_headers[i].NumberOfRelocations =
          static_cast<std::uint16_t>(sec_relocs[i].size());
      cursor += sec_relocs[i].size() * sizeof(CoffRelocation);
    }
  }

  CoffFileHeader fh{};
  std::memset(&fh, 0, sizeof(fh));
  fh.Machine = is_arm64_ ? kCoffMachineARM64 : kCoffMachineAMD64;
  fh.NumberOfSections = static_cast<std::uint16_t>(sec_headers.size());
  fh.PointerToSymbolTable = static_cast<std::uint32_t>(cursor);
  fh.NumberOfSymbols = static_cast<std::uint32_t>(coff_syms.size());
  fh.Characteristics = kCoffCharLargeAddr;

  // ── 5. Serialise ─────────────────────────────────────────────────────────
  std::vector<std::uint8_t> out;
  const std::size_t total = static_cast<std::size_t>(fh.PointerToSymbolTable) +
                            coff_syms.size() * sizeof(CoffSymbol) + strtab.size();
  out.reserve(total);

  auto append = [&](const void *src, std::size_t n) {
    const auto *p = static_cast<const std::uint8_t *>(src);
    out.insert(out.end(), p, p + n);
  };

  append(&fh, sizeof(fh));
  append(sec_headers.data(), sec_headers.size() * sizeof(CoffSectionHeader));
  for (std::size_t i = 0; i < sections_.size(); ++i) {
    if (!LooksLikeBss(sections_[i]) && !sections_[i].data.empty()) {
      append(sections_[i].data.data(), sections_[i].data.size());
    }
  }
  for (std::size_t i = 0; i < sec_relocs.size(); ++i) {
    if (!sec_relocs[i].empty()) {
      append(sec_relocs[i].data(), sec_relocs[i].size() * sizeof(CoffRelocation));
    }
  }
  append(coff_syms.data(), coff_syms.size() * sizeof(CoffSymbol));
  append(strtab.data(), strtab.size());

  return out;
}

} // namespace polyglot::backends

/** @} */