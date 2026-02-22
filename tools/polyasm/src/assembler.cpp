#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#if defined(__APPLE__)
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/reloc.h>
#endif
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "backends/arm64/include/arm64_target.h"
#include "backends/x86_64/include/x86_target.h"
#include "backends/wasm/include/wasm_target.h"
#include "middle/include/ir/ir_parser.h"

namespace polyglot::tools {
namespace {

struct Options {
  std::string input;
  std::string output;
  std::string arch{"x86_64"};
  std::string format{"elf"};
};

Options ParseArgs(int argc, char **argv) {
  Options opts;
  if (argc < 2) {
    throw std::invalid_argument("Usage: polyasm <input.ir> [output.o] [--arch=x86_64|arm64|wasm] [--format=elf|pobj|macho]");
  }

  opts.input = argv[1];
  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg.rfind("--arch=", 0) == 0) {
      opts.arch = arg.substr(7);
    } else if (arg.rfind("--format=", 0) == 0) {
      opts.format = arg.substr(9);
    } else if (arg == "-o" && i + 1 < argc) {
      opts.output = argv[++i];
    } else if (opts.output.empty()) {
      opts.output = arg;
    }
  }

  if (opts.output.empty()) {
    std::filesystem::path in_path(opts.input);
    auto stem = in_path.stem().string();
    if (stem.empty()) stem = "a";
    opts.output = (in_path.parent_path() / (stem + ".o")).string();
  }

  return opts;
}

#if __has_include(<elf.h>)
#include <elf.h>
#else
using Elf64_Half = std::uint16_t;
using Elf64_Word = std::uint32_t;
using Elf64_Sword = std::int32_t;
using Elf64_Xword = std::uint64_t;
using Elf64_Sxword = std::int64_t;
using Elf64_Addr = std::uint64_t;
using Elf64_Off = std::uint64_t;

struct Elf64_Ehdr {
  unsigned char e_ident[16];
  Elf64_Half e_type;
  Elf64_Half e_machine;
  Elf64_Word e_version;
  Elf64_Addr e_entry;
  Elf64_Off e_phoff;
  Elf64_Off e_shoff;
  Elf64_Word e_flags;
  Elf64_Half e_ehsize;
  Elf64_Half e_phentsize;
  Elf64_Half e_phnum;
  Elf64_Half e_shentsize;
  Elf64_Half e_shnum;
  Elf64_Half e_shstrndx;
};

struct Elf64_Shdr {
  Elf64_Word sh_name;
  Elf64_Word sh_type;
  Elf64_Xword sh_flags;
  Elf64_Addr sh_addr;
  Elf64_Off sh_offset;
  Elf64_Xword sh_size;
  Elf64_Word sh_link;
  Elf64_Word sh_info;
  Elf64_Xword sh_addralign;
  Elf64_Xword sh_entsize;
};

struct Elf64_Sym {
  Elf64_Word st_name;
  unsigned char st_info;
  unsigned char st_other;
  Elf64_Half st_shndx;
  Elf64_Addr st_value;
  Elf64_Xword st_size;
};

struct Elf64_Rela {
  Elf64_Addr r_offset;
  Elf64_Xword r_info;
  Elf64_Sxword r_addend;
};

#define ELFMAG "\177ELF"
#define SELFMAG 4
#define EI_CLASS 4
#define EI_DATA 5
#define EI_VERSION 6
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EV_CURRENT 1
#define ET_REL 1
#define EM_X86_64 62
#define EM_AARCH64 183
#define SHN_UNDEF 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_NOBITS 8
#define SHF_WRITE 0x1
#define SHF_ALLOC 0x2
#define SHF_EXECINSTR 0x4
#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STT_NOTYPE 0
#define STT_FUNC 2
#define R_X86_64_64 1
#define R_X86_64_PC32 2
#define R_AARCH64_JUMP26 282
#define R_AARCH64_CALL26 283
#define ELF64_ST_INFO(b, t) (((b) << 4) + ((t) & 0x0f))
#define ELF64_R_INFO(sym, type) ((((Elf64_Xword)(sym)) << 32) + ((type) & 0xffffffffULL))
#endif

constexpr std::uint32_t kSectionFlagBss = 1u << 1;

struct ObjReloc {
  std::uint32_t section_index{0};
  std::uint64_t offset{0};
  std::uint32_t type{1};
  std::uint32_t symbol_index{0};
  std::int64_t addend{-4};
};

struct ObjSection {
  std::string name;
  std::vector<std::uint8_t> data;
  bool bss{false};
  std::vector<ObjReloc> relocs;
};

struct ObjSymbol {
  std::string name;
  std::uint32_t section_index{0xFFFFFFFF};
  std::uint64_t value{0};
  std::uint64_t size{0};
  bool global{true};
  bool defined{false};
};

std::string BuildPobj(const std::string &path, const std::vector<ObjSection> &obj_sections,
                      const std::vector<ObjSymbol> &obj_symbols) {
  struct FileHeader {
    char magic[4];
    std::uint16_t version{1};
    std::uint16_t section_count{0};
    std::uint16_t symbol_count{0};
    std::uint16_t reloc_count{0};
    std::uint32_t strtab_offset{0};
  };

  struct SectionRecord {
    std::uint32_t name_offset{0};
    std::uint32_t flags{0};
    std::uint64_t offset{0};
    std::uint64_t size{0};
  };

  struct SymbolRecord {
    std::uint32_t name_offset{0};
    std::uint32_t section_index{0xFFFFFFFF};
    std::uint64_t value{0};
    std::uint64_t size{0};
    std::uint8_t binding{0};
    std::uint8_t reserved[3]{};
  };

  struct RelocRecord {
    std::uint32_t section_index{0};
    std::uint64_t offset{0};
    std::uint32_t type{0};
    std::uint32_t symbol_index{0};
    std::int64_t addend{0};
  };

  std::vector<std::uint8_t> strtab{0};
  auto add_str = [&](const std::string &s) {
    std::uint32_t off = static_cast<std::uint32_t>(strtab.size());
    strtab.insert(strtab.end(), s.begin(), s.end());
    strtab.push_back(0);
    return off;
  };

  std::vector<SectionRecord> sections;
  sections.reserve(obj_sections.size());
  for (const auto &sec : obj_sections) {
    SectionRecord rec{};
    rec.name_offset = add_str(sec.name);
    rec.flags = sec.bss ? kSectionFlagBss : 0;
    rec.size = static_cast<std::uint64_t>(sec.data.size());
    sections.push_back(rec);
  }

  std::vector<SymbolRecord> symbols;
  symbols.reserve(obj_symbols.size());
  for (const auto &sym : obj_symbols) {
    SymbolRecord rec{};
    rec.name_offset = add_str(sym.name);
    rec.section_index = sym.defined ? sym.section_index : 0xFFFFFFFF;
    rec.value = sym.value;
    rec.size = sym.size;
    rec.binding = sym.global ? 1 : 0;
    symbols.push_back(rec);
  }

  std::vector<RelocRecord> reloc_records;
  for (std::size_t si = 0; si < obj_sections.size(); ++si) {
    for (const auto &r : obj_sections[si].relocs) {
      RelocRecord rr{};
      rr.section_index = static_cast<std::uint32_t>(si);
      rr.offset = r.offset;
      rr.type = r.type;
      rr.symbol_index = r.symbol_index;
      rr.addend = r.addend;
      reloc_records.push_back(rr);
    }
  }

  std::size_t cursor = sizeof(FileHeader) + sections.size() * sizeof(SectionRecord) +
                       symbols.size() * sizeof(SymbolRecord) + reloc_records.size() * sizeof(RelocRecord);
  for (std::size_t i = 0; i < obj_sections.size(); ++i) {
    sections[i].offset = obj_sections[i].bss ? 0 : cursor;
    if (!obj_sections[i].bss) cursor += obj_sections[i].data.size();
  }

  FileHeader hdr{};
  std::memcpy(hdr.magic, "POBJ", 4);
  hdr.section_count = static_cast<std::uint16_t>(sections.size());
  hdr.symbol_count = static_cast<std::uint16_t>(symbols.size());
  hdr.reloc_count = static_cast<std::uint16_t>(reloc_records.size());
  hdr.strtab_offset = static_cast<std::uint32_t>(cursor);

  std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
  if (!ofs.is_open()) return {};
  ofs.write(reinterpret_cast<const char *>(&hdr), static_cast<std::streamsize>(sizeof(hdr)));
  ofs.write(reinterpret_cast<const char *>(sections.data()), static_cast<std::streamsize>(sections.size() * sizeof(SectionRecord)));
  ofs.write(reinterpret_cast<const char *>(symbols.data()), static_cast<std::streamsize>(symbols.size() * sizeof(SymbolRecord)));
  ofs.write(reinterpret_cast<const char *>(reloc_records.data()), static_cast<std::streamsize>(reloc_records.size() * sizeof(RelocRecord)));
  for (const auto &sec : obj_sections) {
    if (sec.bss) continue;
    ofs.write(reinterpret_cast<const char *>(sec.data.data()), static_cast<std::streamsize>(sec.data.size()));
  }
  ofs.write(reinterpret_cast<const char *>(strtab.data()), static_cast<std::streamsize>(strtab.size()));
  if (!ofs.good()) return {};
  return path;
}

std::string BuildElf64(const std::string &path, const std::vector<ObjSection> &obj_sections,
                       const std::vector<ObjSymbol> &obj_symbols, const std::string &arch) {
  bool is_arm64 = (arch == "arm64" || arch == "aarch64" || arch == "armv8");
  auto align_to = [](std::size_t value, std::size_t align) {
    if (align == 0) return value;
    auto mask = align - 1;
    return (value + mask) & ~mask;
  };

  auto text_it = std::find_if(obj_sections.begin(), obj_sections.end(), [](const ObjSection &s) { return s.name == ".text"; });
  auto data_it = std::find_if(obj_sections.begin(), obj_sections.end(), [](const ObjSection &s) { return s.name == ".data"; });
  auto bss_it = std::find_if(obj_sections.begin(), obj_sections.end(), [](const ObjSection &s) { return s.bss; });

  std::string shstr;
  shstr.push_back('\0');
  auto add_name = [&](const std::string &n) {
    std::uint32_t off = static_cast<std::uint32_t>(shstr.size());
    shstr.append(n);
    shstr.push_back('\0');
    return off;
  };

  std::string strtab(1, '\0');
  std::vector<std::uint16_t> section_map(obj_sections.size(), SHN_UNDEF);
  std::vector<Elf64_Sym> symtab;
  symtab.push_back(Elf64_Sym{});

  for (const auto &sym : obj_symbols) {
    std::uint32_t name_off = static_cast<std::uint32_t>(strtab.size());
    strtab.append(sym.name);
    strtab.push_back('\0');
    Elf64_Sym s{};
    s.st_name = name_off;
    bool defined = sym.defined && sym.section_index != 0xFFFFFFFF;
    s.st_info = ELF64_ST_INFO(sym.global ? STB_GLOBAL : STB_LOCAL, defined ? STT_FUNC : STT_NOTYPE);
    s.st_other = 0;
    s.st_shndx = SHN_UNDEF;
    s.st_value = sym.value;
    s.st_size = sym.size;
    symtab.push_back(s);
  }

  std::vector<Elf64_Shdr> shdrs;
  auto add_sh = [&](const Elf64_Shdr &sh) {
    shdrs.push_back(sh);
    return static_cast<std::uint16_t>(shdrs.size() - 1);
  };

  Elf64_Shdr sh_null{};
  sh_null.sh_name = 0;
  add_sh(sh_null);

  std::size_t off = sizeof(Elf64_Ehdr);
  std::uint16_t idx_text = SHN_UNDEF;
  if (text_it != obj_sections.end()) {
    Elf64_Shdr sh{};
    sh.sh_name = add_name(".text");
    sh.sh_type = SHT_PROGBITS;
    sh.sh_flags = SHF_ALLOC | SHF_EXECINSTR;
    sh.sh_offset = off;
    sh.sh_size = text_it->data.size();
    sh.sh_addralign = 16;
    idx_text = add_sh(sh);
    section_map[static_cast<std::size_t>(text_it - obj_sections.begin())] = idx_text;
    off += text_it->data.size();
  }

  std::uint16_t idx_data = SHN_UNDEF;
  if (data_it != obj_sections.end() && !data_it->data.empty()) {
    off = align_to(off, 8);
    Elf64_Shdr sh{};
    sh.sh_name = add_name(".data");
    sh.sh_type = SHT_PROGBITS;
    sh.sh_flags = SHF_ALLOC | SHF_WRITE;
    sh.sh_offset = off;
    sh.sh_size = data_it->data.size();
    sh.sh_addralign = 8;
    idx_data = add_sh(sh);
    section_map[static_cast<std::size_t>(data_it - obj_sections.begin())] = idx_data;
    off += data_it->data.size();
  }

  std::uint16_t idx_bss = SHN_UNDEF;
  if (bss_it != obj_sections.end()) {
    Elf64_Shdr sh{};
    sh.sh_name = add_name(".bss");
    sh.sh_type = SHT_NOBITS;
    sh.sh_flags = SHF_ALLOC | SHF_WRITE;
    sh.sh_offset = 0;
    sh.sh_size = bss_it->data.size();
    sh.sh_addralign = 8;
    idx_bss = add_sh(sh);
    section_map[static_cast<std::size_t>(bss_it - obj_sections.begin())] = idx_bss;
  }

  off = align_to(off, 8);
  std::uint16_t idx_symtab = SHN_UNDEF;
  {
    Elf64_Shdr sh{};
    sh.sh_name = add_name(".symtab");
    sh.sh_type = SHT_SYMTAB;
    sh.sh_offset = off;
    sh.sh_size = symtab.size() * sizeof(Elf64_Sym);
    sh.sh_info = 1;
    sh.sh_addralign = 8;
    sh.sh_entsize = sizeof(Elf64_Sym);
    idx_symtab = add_sh(sh);
    off += sh.sh_size;
  }

  std::uint16_t idx_strtab = SHN_UNDEF;
  {
    Elf64_Shdr sh{};
    sh.sh_name = add_name(".strtab");
    sh.sh_type = SHT_STRTAB;
    sh.sh_offset = off;
    sh.sh_size = strtab.size();
    sh.sh_addralign = 1;
    idx_strtab = add_sh(sh);
    off += sh.sh_size;
  }

  bool has_relocs = (text_it != obj_sections.end() && !text_it->relocs.empty());
  std::uint16_t idx_rela = SHN_UNDEF;
  if (has_relocs) {
    off = align_to(off, 8);
    Elf64_Shdr sh{};
    sh.sh_name = add_name(".rela.text");
    sh.sh_type = SHT_RELA;
    sh.sh_offset = off;
    sh.sh_size = text_it->relocs.size() * sizeof(Elf64_Rela);
    sh.sh_addralign = 8;
    sh.sh_entsize = sizeof(Elf64_Rela);
    idx_rela = add_sh(sh);
    off += sh.sh_size;
  }

  std::uint16_t idx_shstr = SHN_UNDEF;
  {
    Elf64_Shdr sh{};
    sh.sh_name = add_name(".shstrtab");
    sh.sh_type = SHT_STRTAB;
    sh.sh_offset = off;
    sh.sh_size = shstr.size();
    sh.sh_addralign = 1;
    idx_shstr = add_sh(sh);
    off += sh.sh_size;
  }

  for (std::size_t i = 1; i < symtab.size(); ++i) {
    const auto &sym = obj_symbols[i - 1];
    if (sym.defined && sym.section_index != 0xFFFFFFFF && sym.section_index < section_map.size()) {
      symtab[i].st_shndx = section_map[sym.section_index];
    } else {
      symtab[i].st_shndx = SHN_UNDEF;
    }
  }

  if (idx_symtab != SHN_UNDEF && idx_strtab != SHN_UNDEF) {
    shdrs[idx_symtab].sh_link = idx_strtab;
  }
  if (idx_rela != SHN_UNDEF && idx_symtab != SHN_UNDEF && idx_text != SHN_UNDEF) {
    shdrs[idx_rela].sh_link = idx_symtab;
    shdrs[idx_rela].sh_info = idx_text;
  }

  Elf64_Ehdr ehdr{};
  std::memcpy(ehdr.e_ident, ELFMAG, SELFMAG);
  ehdr.e_ident[EI_CLASS] = ELFCLASS64;
  ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
  ehdr.e_ident[EI_VERSION] = EV_CURRENT;
  ehdr.e_type = ET_REL;
  ehdr.e_machine = is_arm64 ? EM_AARCH64 : EM_X86_64;
  ehdr.e_version = EV_CURRENT;
  ehdr.e_ehsize = sizeof(Elf64_Ehdr);
  ehdr.e_phentsize = 0;
  ehdr.e_shentsize = sizeof(Elf64_Shdr);
  off = align_to(off, 8);
  ehdr.e_shoff = off;
  ehdr.e_shnum = static_cast<std::uint16_t>(shdrs.size());
  ehdr.e_shstrndx = idx_shstr;

  std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
  if (!ofs.is_open()) return {};

  ofs.write(reinterpret_cast<const char *>(&ehdr), sizeof(ehdr));
  std::size_t cursor = sizeof(Elf64_Ehdr);
  auto pad_to = [&](std::size_t target) {
    if (target <= cursor) return;
    std::vector<char> zero(target - cursor, 0);
    ofs.write(zero.data(), static_cast<std::streamsize>(zero.size()));
    cursor = target;
  };

  if (idx_text != SHN_UNDEF && text_it != obj_sections.end()) {
    pad_to(shdrs[idx_text].sh_offset);
    ofs.write(reinterpret_cast<const char *>(text_it->data.data()), static_cast<std::streamsize>(text_it->data.size()));
    cursor += text_it->data.size();
  }

  if (idx_data != SHN_UNDEF && data_it != obj_sections.end()) {
    pad_to(shdrs[idx_data].sh_offset);
    ofs.write(reinterpret_cast<const char *>(data_it->data.data()), static_cast<std::streamsize>(data_it->data.size()));
    cursor += data_it->data.size();
  }

  if (idx_symtab != SHN_UNDEF) {
    pad_to(shdrs[idx_symtab].sh_offset);
    ofs.write(reinterpret_cast<const char *>(symtab.data()), static_cast<std::streamsize>(symtab.size() * sizeof(Elf64_Sym)));
    cursor += shdrs[idx_symtab].sh_size;
  }

  if (idx_strtab != SHN_UNDEF) {
    pad_to(shdrs[idx_strtab].sh_offset);
    ofs.write(strtab.data(), static_cast<std::streamsize>(strtab.size()));
    cursor += shdrs[idx_strtab].sh_size;
  }

  if (idx_rela != SHN_UNDEF && text_it != obj_sections.end()) {
    pad_to(shdrs[idx_rela].sh_offset);
    for (const auto &r : text_it->relocs) {
      Elf64_Rela rela{};
      rela.r_offset = r.offset;
      std::uint32_t reloc_type = 0;
      if (is_arm64) {
        reloc_type = (r.type == 1) ? R_AARCH64_CALL26 : R_AARCH64_JUMP26;
      } else {
        reloc_type = (r.type == 1) ? R_X86_64_PC32 : R_X86_64_64;
      }
      rela.r_info = ELF64_R_INFO(static_cast<std::uint32_t>(r.symbol_index + 1), reloc_type);
      rela.r_addend = r.addend;
      ofs.write(reinterpret_cast<const char *>(&rela), sizeof(rela));
      cursor += sizeof(rela);
    }
  }

  if (idx_shstr != SHN_UNDEF) {
    pad_to(shdrs[idx_shstr].sh_offset);
    ofs.write(shstr.data(), static_cast<std::streamsize>(shstr.size()));
    cursor += shdrs[idx_shstr].sh_size;
  }

  pad_to(ehdr.e_shoff);
  ofs.write(reinterpret_cast<const char *>(shdrs.data()), static_cast<std::streamsize>(shdrs.size() * sizeof(Elf64_Shdr)));
  return ofs.good() ? path : std::string{};
}

#if defined(__APPLE__)
std::string BuildMachO64(const std::string &path, const std::vector<ObjSection> &obj_sections,
                         const std::vector<ObjSymbol> &obj_symbols, const std::string &arch) {
  auto text_it = std::find_if(obj_sections.begin(), obj_sections.end(),
                              [](const ObjSection &s) { return s.name == ".text"; });
  bool is_arm64 = (arch == "arm64" || arch == "aarch64" || arch == "armv8");
  std::size_t text_size = text_it != obj_sections.end() ? text_it->data.size() : 0;

  mach_header_64 mh{};
  mh.magic = MH_MAGIC_64;
  mh.cputype = is_arm64 ? CPU_TYPE_ARM64 : CPU_TYPE_X86_64;
  mh.cpusubtype = is_arm64 ? CPU_SUBTYPE_ARM64_ALL : CPU_SUBTYPE_X86_64_ALL;
  mh.filetype = MH_OBJECT;
  mh.ncmds = 2;
  mh.sizeofcmds = sizeof(segment_command_64) + sizeof(section_64) + sizeof(symtab_command);
  mh.flags = MH_SUBSECTIONS_VIA_SYMBOLS;

  segment_command_64 seg{};
  seg.cmd = LC_SEGMENT_64;
  seg.cmdsize = sizeof(segment_command_64) + sizeof(section_64);
  std::memcpy(seg.segname, "__TEXT", 6);
  seg.vmsize = text_size;
  seg.filesize = text_size;
  seg.maxprot = VM_PROT_READ | VM_PROT_EXECUTE;
  seg.initprot = VM_PROT_READ | VM_PROT_EXECUTE;
  seg.nsects = 1;

  section_64 sec{};
  std::memcpy(sec.sectname, "__text", 6);
  std::memcpy(sec.segname, "__TEXT", 6);
  sec.size = text_size;
  sec.align = 4;
  sec.flags = S_REGULAR | S_ATTR_SOME_INSTRUCTIONS;
  sec.offset = sizeof(mach_header_64) + seg.cmdsize + sizeof(symtab_command);

  std::string strtab(1, '\0');
  std::vector<nlist_64> nlists;
  std::unordered_map<std::string, std::uint32_t> sym_index;

  auto add_sym = [&](const ObjSymbol &sym) {
    if (sym_index.count(sym.name)) return sym_index[sym.name];
    std::uint32_t name_off = static_cast<std::uint32_t>(strtab.size());
    strtab.append(sym.name);
    strtab.push_back('\0');
    nlist_64 nl{};
    nl.n_un.n_strx = name_off;
    bool defined = sym.defined && sym.section_index != 0xFFFFFFFF;
    nl.n_type = (defined ? (N_EXT | N_SECT) : (N_EXT | N_UNDF));
    nl.n_sect = defined ? 1 : 0;
    nl.n_desc = 0;
    nl.n_value = defined ? sym.value : 0;
    sym_index[sym.name] = static_cast<std::uint32_t>(nlists.size());
    nlists.push_back(nl);
    return sym_index[sym.name];
  };

  for (const auto &sym : obj_symbols) {
    add_sym(sym);
  }

  std::size_t reloc_bytes = text_it != obj_sections.end() ? text_it->relocs.size() * sizeof(relocation_info) : 0;
  sec.reloff = static_cast<std::uint32_t>(sec.offset + text_size);
  sec.nreloc = static_cast<std::uint32_t>(text_it != obj_sections.end() ? text_it->relocs.size() : 0);

  symtab_command st{};
  st.cmd = LC_SYMTAB;
  st.cmdsize = sizeof(symtab_command);
  st.symoff = static_cast<std::uint32_t>(sec.reloff + reloc_bytes);
  st.nsyms = static_cast<std::uint32_t>(nlists.size());
  st.stroff = st.symoff + static_cast<std::uint32_t>(nlists.size() * sizeof(nlist_64));
  st.strsize = static_cast<std::uint32_t>(strtab.size());

  std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
  if (!ofs.is_open()) return {};

  ofs.write(reinterpret_cast<const char *>(&mh), sizeof(mh));
  ofs.write(reinterpret_cast<const char *>(&seg), sizeof(seg));
  ofs.write(reinterpret_cast<const char *>(&sec), sizeof(sec));
  ofs.write(reinterpret_cast<const char *>(&st), sizeof(st));
  if (text_it != obj_sections.end()) {
    ofs.write(reinterpret_cast<const char *>(text_it->data.data()),
              static_cast<std::streamsize>(text_it->data.size()));
    for (const auto &r : text_it->relocs) {
      relocation_info ri{};
      ri.r_address = static_cast<std::int32_t>(r.offset);
      ri.r_symbolnum = static_cast<std::int32_t>(r.symbol_index);
      ri.r_pcrel = (r.type == 1);
      ri.r_length = 2;
      ri.r_extern = 1;
      ri.r_type = is_arm64 ? 2 : 0;
      ofs.write(reinterpret_cast<const char *>(&ri), sizeof(ri));
    }
  }

  for (const auto &n : nlists) {
    ofs.write(reinterpret_cast<const char *>(&n), sizeof(n));
  }
  ofs.write(strtab.data(), static_cast<std::streamsize>(strtab.size()));
  return ofs.good() ? path : std::string{};
}
#else
std::string BuildMachO64(const std::string &, const std::vector<ObjSection> &,
                         const std::vector<ObjSymbol> &, const std::string &) {
  return {};
}
#endif

std::string EmitObject(const Options &opts, const std::vector<ObjSection> &sections,
                       const std::vector<ObjSymbol> &symbols) {
  const std::string &fmt = opts.format;
  if (fmt == "pobj") {
    return BuildPobj(opts.output, sections, symbols);
  }
  if (fmt == "macho") {
    auto written = BuildMachO64(opts.output, sections, symbols, opts.arch);
    if (written.empty()) std::cerr << "[error] Mach-O emit failed or unsupported on this platform\n";
    return written;
  }
  return BuildElf64(opts.output, sections, symbols, opts.arch);
}

template <typename BackendT>
void ConvertBackendMC(const BackendT &mc, const std::string &arch, std::vector<ObjSection> &sections,
                      std::vector<ObjSymbol> &symbols) {
  sections.clear();
  symbols.clear();
  std::unordered_map<std::string, std::uint32_t> sec_index;

  for (const auto &sec : mc.sections) {
    ObjSection s;
    s.name = sec.name.empty() ? ".text" : sec.name;
    s.data = sec.data;
    s.bss = sec.bss;
    sec_index[s.name] = static_cast<std::uint32_t>(sections.size());
    sections.push_back(std::move(s));
  }

  if (sections.empty()) {
    ObjSection text;
    text.name = ".text";
    if (arch == "arm64" || arch == "aarch64" || arch == "armv8") {
      text.data = {0x00, 0x00, 0x80, 0xD2, 0xC0, 0x03, 0x5F, 0xD6};
    } else {
      text.data = {0x55, 0x48, 0x89, 0xE5, 0x31, 0xC0, 0x5D, 0xC3};
    }
    sec_index[text.name] = 0;
    sections.push_back(std::move(text));
  }

  std::uint32_t text_idx = sec_index.count(".text") ? sec_index[".text"] : 0;
  symbols.push_back({"_start", text_idx, 0, static_cast<std::uint64_t>(sections[text_idx].data.size()), true, true});

  for (const auto &sym : mc.symbols) {
    ObjSymbol osym;
    osym.name = sym.name;
    osym.value = sym.value;
    osym.size = sym.size;
    osym.global = sym.global;
    if (sym.defined && sec_index.count(sym.section)) {
      osym.section_index = sec_index[sym.section];
      osym.defined = true;
    }
    symbols.push_back(osym);
  }

  std::unordered_map<std::string, std::uint32_t> sym_index;
  for (std::uint32_t i = 0; i < symbols.size(); ++i) sym_index[symbols[i].name] = i;

  for (const auto &r : mc.relocs) {
    std::string sec_name = r.section.empty() ? ".text" : r.section;
    auto s_it = sec_index.find(sec_name);
    if (s_it == sec_index.end()) continue;

    auto sym_it = sym_index.find(r.symbol);
    if (sym_it == sym_index.end()) {
      ObjSymbol ext;
      ext.name = r.symbol;
      ext.section_index = 0xFFFFFFFF;
      ext.value = 0;
      ext.size = 0;
      ext.global = true;
      ext.defined = false;
      sym_index[ext.name] = static_cast<std::uint32_t>(symbols.size());
      symbols.push_back(ext);
      sym_it = sym_index.find(r.symbol);
    }

    ObjReloc orr{};
    orr.section_index = s_it->second;
    orr.offset = r.offset;
    orr.type = r.type;
    orr.symbol_index = sym_it->second;
    orr.addend = r.addend;
    sections[orr.section_index].relocs.push_back(orr);
  }
}

std::string Assemble(const std::string &source, const Options &opts) {
  polyglot::ir::IRContext ir_module;
  std::string msg;
  if (!polyglot::ir::ParseModule(source, ir_module, &msg)) {
    throw std::runtime_error("IR parse failed: " + msg);
  }

  std::vector<ObjSection> sections;
  std::vector<ObjSymbol> symbols;

  if (opts.arch == "wasm" || opts.arch == "wasm32" || opts.arch == "wasm64") {
    // WASM backend emits a binary module directly — no ELF/Mach-O wrapper.
    polyglot::backends::wasm::WasmTarget target(&ir_module);
    auto wasm_binary = target.EmitWasmBinary();
    if (wasm_binary.empty()) {
      throw std::runtime_error("WASM backend produced empty binary");
    }
    // Write the raw .wasm binary to the output file
    std::ofstream out_file(opts.output, std::ios::binary);
    if (!out_file) {
      throw std::runtime_error("failed to open output file: " + opts.output);
    }
    out_file.write(reinterpret_cast<const char *>(wasm_binary.data()),
                   static_cast<std::streamsize>(wasm_binary.size()));
    return opts.output;
  } else if (opts.arch == "arm64" || opts.arch == "aarch64" || opts.arch == "armv8") {
    polyglot::backends::arm64::Arm64Target target(&ir_module);
    auto mc = target.EmitObjectCode();
    ConvertBackendMC(mc, opts.arch, sections, symbols);
  } else {
    polyglot::backends::x86_64::X86Target target(&ir_module);
    auto mc = target.EmitObjectCode();
    ConvertBackendMC(mc, opts.arch, sections, symbols);
  }

  auto written = EmitObject(opts, sections, symbols);
  if (written.empty()) {
    throw std::runtime_error("failed to write object file");
  }
  return written;
}

}  // namespace
}  // namespace polyglot::tools

int main(int argc, char **argv) {
  polyglot::tools::Options opts;
  try {
    opts = polyglot::tools::ParseArgs(argc, argv);
  } catch (const std::exception &ex) {
    std::cerr << ex.what() << "\n";
    return 1;
  }

  std::ifstream input(opts.input);
  if (!input) {
    std::cerr << "Failed to open " << opts.input << "\n";
    return 1;
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();

  try {
    const auto object_path = polyglot::tools::Assemble(buffer.str(), opts);
    std::cout << object_path << "\n";
  } catch (const std::exception &ex) {
    std::cerr << "Assemble failed: " << ex.what() << "\n";
    return 1;
  }
  return 0;
}
