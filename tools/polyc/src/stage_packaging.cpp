// ============================================================================
// stage_packaging.cpp — Stage 6 implementation
// ============================================================================

#include "tools/polyc/src/stage_packaging.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#if __has_include(<elf.h>)
#include <elf.h>
#else
using Elf64_Half  = std::uint16_t;
using Elf64_Word  = std::uint32_t;
using Elf64_Sword = std::int32_t;
using Elf64_Xword = std::uint64_t;
using Elf64_Sxword = std::int64_t;
using Elf64_Addr  = std::uint64_t;
using Elf64_Off   = std::uint64_t;

struct Elf64_Ehdr {
    unsigned char e_ident[16];
    Elf64_Half e_type, e_machine;
    Elf64_Word e_version;
    Elf64_Addr e_entry;
    Elf64_Off  e_phoff, e_shoff;
    Elf64_Word e_flags;
    Elf64_Half e_ehsize, e_phentsize, e_phnum,
               e_shentsize, e_shnum, e_shstrndx;
};
struct Elf64_Shdr {
    Elf64_Word  sh_name, sh_type;
    Elf64_Xword sh_flags;
    Elf64_Addr  sh_addr;
    Elf64_Off   sh_offset;
    Elf64_Xword sh_size;
    Elf64_Word  sh_link, sh_info;
    Elf64_Xword sh_addralign, sh_entsize;
};
struct Elf64_Sym {
    Elf64_Word st_name;
    unsigned char st_info, st_other;
    Elf64_Half st_shndx;
    Elf64_Addr st_value;
    Elf64_Xword st_size;
};
struct Elf64_Rela {
    Elf64_Addr   r_offset;
    Elf64_Xword  r_info;
    Elf64_Sxword r_addend;
};
#define ELFMAG "\177ELF"
#define SELFMAG 4
#define EI_CLASS 4
#define EI_DATA  5
#define EI_VERSION 6
#define ELFCLASS64   2
#define ELFDATA2LSB  1
#define EV_CURRENT   1
#define ET_REL       1
#define EM_X86_64   62
#define EM_AARCH64  183
#define SHN_UNDEF    0
#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_RELA     4
#define SHT_NOBITS   8
#define SHF_WRITE    0x1
#define SHF_ALLOC    0x2
#define SHF_EXECINSTR 0x4
#define STB_LOCAL    0
#define STB_GLOBAL   1
#define STT_NOTYPE   0
#define STT_FUNC     2
#define R_X86_64_64       1
#define R_X86_64_PC32     2
#define R_AARCH64_JUMP26  282
#define R_AARCH64_CALL26  283
#define ELF64_ST_INFO(b,t)  (((b)<<4)+((t)&0x0f))
#define ELF64_R_INFO(s,t)   ((((Elf64_Xword)(s))<<32)+((t)&0xffffffffULL))
#endif

#if defined(__APPLE__)
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/reloc.h>
#endif

namespace fs = std::filesystem;

namespace polyglot::tools {
namespace {

// ════════════════════════════════════════════════════════════════════════════
// POBJ emitter (kept in sync with polyld)
// ════════════════════════════════════════════════════════════════════════════

#pragma pack(push, 1)
struct FileHeader {
    char magic[4];
    std::uint16_t version{1};
    std::uint16_t section_count{0};
    std::uint16_t symbol_count{0};
    std::uint16_t reloc_count{0};
    std::uint64_t strtab_offset{0};
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
    std::uint8_t  binding{0};
    std::uint8_t  reserved[3]{};
};
struct RelocRecord {
    std::uint32_t section_index{0};
    std::uint64_t offset{0};
    std::uint32_t type{0};
    std::uint32_t symbol_index{0};
    std::int64_t  addend{0};
};
struct PAuxHeader {
    char magic[4];
    std::uint16_t version;
    std::uint16_t section_count;
    std::uint8_t  reserved[8];
};
#pragma pack(pop)

constexpr std::uint32_t kSectionFlagBss = 1u << 1;

// ── POBJ ────────────────────────────────────────────────────────────────────

std::string BuildPobj(const std::string &path,
                      const std::vector<ObjSection> &obj_sections,
                      const std::vector<ObjSymbol>  &obj_symbols) {
    std::vector<std::uint8_t> strtab{0};
    auto add_str = [&](const std::string &s) {
        std::uint32_t off = static_cast<std::uint32_t>(strtab.size());
        strtab.insert(strtab.end(), s.begin(), s.end());
        strtab.push_back(0);
        return off;
    };

    std::vector<SectionRecord> sections;
    std::size_t cursor = sizeof(FileHeader);
    for (const auto &sec : obj_sections) {
        SectionRecord rec{};
        rec.name_offset = add_str(sec.name);
        rec.flags = sec.bss ? kSectionFlagBss : 0;
        rec.size  = static_cast<std::uint64_t>(sec.data.size());
        sections.push_back(rec);
    }

    std::vector<SymbolRecord> symbols;
    for (const auto &sym : obj_symbols) {
        SymbolRecord rec{};
        rec.name_offset   = add_str(sym.name);
        rec.section_index = sym.defined ? sym.section_index : 0xFFFFFFFF;
        rec.value         = sym.value;
        rec.size          = sym.size;
        rec.binding       = sym.global ? 1 : 0;
        symbols.push_back(rec);
    }

    std::vector<RelocRecord> reloc_records;
    for (std::size_t si = 0; si < obj_sections.size(); ++si) {
        for (const auto &r : obj_sections[si].relocs) {
            RelocRecord rr{};
            rr.section_index = static_cast<std::uint32_t>(si);
            rr.offset        = r.offset;
            rr.type          = r.type;
            rr.symbol_index  = r.symbol_index;
            rr.addend        = r.addend;
            reloc_records.push_back(rr);
        }
    }

    std::size_t table_bytes = sections.size() * sizeof(SectionRecord)
                            + symbols.size() * sizeof(SymbolRecord)
                            + reloc_records.size() * sizeof(RelocRecord);
    cursor += table_bytes;

    std::size_t data_cursor = cursor;
    for (std::size_t i = 0; i < obj_sections.size(); ++i) {
        sections[i].offset = obj_sections[i].bss ? 0 : data_cursor;
        if (!obj_sections[i].bss) data_cursor += obj_sections[i].data.size();
    }

    FileHeader hdr{};
    std::memcpy(hdr.magic, "POBJ", 4);
    hdr.version        = 1;
    hdr.section_count  = static_cast<std::uint16_t>(sections.size());
    hdr.symbol_count   = static_cast<std::uint16_t>(symbols.size());
    hdr.reloc_count    = static_cast<std::uint16_t>(reloc_records.size());
    hdr.strtab_offset  = data_cursor;

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return {};
    ofs.write(reinterpret_cast<const char *>(&hdr),           sizeof(hdr));
    ofs.write(reinterpret_cast<const char *>(sections.data()),
              static_cast<std::streamsize>(sections.size()       * sizeof(SectionRecord)));
    ofs.write(reinterpret_cast<const char *>(symbols.data()),
              static_cast<std::streamsize>(symbols.size()        * sizeof(SymbolRecord)));
    ofs.write(reinterpret_cast<const char *>(reloc_records.data()),
              static_cast<std::streamsize>(reloc_records.size()  * sizeof(RelocRecord)));
    for (const auto &sec : obj_sections) {
        if (sec.bss) continue;
        ofs.write(reinterpret_cast<const char *>(sec.data.data()),
                  static_cast<std::streamsize>(sec.data.size()));
    }
    ofs.write(reinterpret_cast<const char *>(strtab.data()),
              static_cast<std::streamsize>(strtab.size()));
    return ofs.good() ? path : std::string{};
}

// ── ELF64 ───────────────────────────────────────────────────────────────────

std::string BuildElf64(const std::string &path,
                       const std::vector<ObjSection> &obj_sections,
                       const std::vector<ObjSymbol>  &obj_symbols,
                       const std::string &arch) {
    bool is_arm64 = (arch == "arm64" || arch == "aarch64" || arch == "armv8");
    auto align_to = [](std::size_t v, std::size_t a) {
        return a ? (v + a - 1) & ~(a - 1) : v;
    };

    auto text_it = std::find_if(obj_sections.begin(), obj_sections.end(),
                                [](const ObjSection &s){ return s.name == ".text"; });
    auto data_it = std::find_if(obj_sections.begin(), obj_sections.end(),
                                [](const ObjSection &s){ return s.name == ".data"; });
    auto bss_it  = std::find_if(obj_sections.begin(), obj_sections.end(),
                                [](const ObjSection &s){ return s.bss; });

    std::string shstr{'\0'};
    auto add_name = [&](const std::string &n) {
        std::uint32_t off = static_cast<std::uint32_t>(shstr.size());
        shstr.append(n); shstr.push_back('\0');
        return off;
    };

    std::string strtab(1, '\0');
    std::vector<std::uint16_t> section_map(obj_sections.size(), SHN_UNDEF);
    std::vector<Elf64_Sym> symtab;
    symtab.push_back(Elf64_Sym{});

    for (const auto &sym : obj_symbols) {
        std::uint32_t name_off = static_cast<std::uint32_t>(strtab.size());
        strtab.append(sym.name); strtab.push_back('\0');
        Elf64_Sym s{};
        s.st_name  = name_off;
        bool defined = sym.defined && sym.section_index != 0xFFFFFFFF;
        s.st_info  = ELF64_ST_INFO(sym.global ? STB_GLOBAL : STB_LOCAL,
                                    defined ? STT_FUNC : STT_NOTYPE);
        s.st_value = sym.value;
        s.st_size  = sym.size;
        symtab.push_back(s);
    }

    std::vector<Elf64_Shdr> shdrs;
    auto add_sh = [&](const Elf64_Shdr &sh) {
        shdrs.push_back(sh);
        return static_cast<std::uint16_t>(shdrs.size() - 1);
    };
    add_sh(Elf64_Shdr{});

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
        sh.sh_size = bss_it->data.size();
        sh.sh_addralign = 8;
        idx_bss = add_sh(sh);
        section_map[static_cast<std::size_t>(bss_it - obj_sections.begin())] = idx_bss;
    }

    off = align_to(off, 8);
    std::uint16_t idx_symtab = SHN_UNDEF;
    {
        Elf64_Shdr sh{};
        sh.sh_name = add_name(".symtab"); sh.sh_type = SHT_SYMTAB;
        sh.sh_offset = off;
        sh.sh_size   = symtab.size() * sizeof(Elf64_Sym);
        sh.sh_info   = 1; sh.sh_addralign = 8; sh.sh_entsize = sizeof(Elf64_Sym);
        idx_symtab = add_sh(sh);
        off += sh.sh_size;
    }
    std::uint16_t idx_strtab = SHN_UNDEF;
    {
        Elf64_Shdr sh{};
        sh.sh_name = add_name(".strtab"); sh.sh_type = SHT_STRTAB;
        sh.sh_offset = off; sh.sh_size = strtab.size(); sh.sh_addralign = 1;
        idx_strtab = add_sh(sh);
        off += sh.sh_size;
    }

    bool has_relocs = (text_it != obj_sections.end() && !text_it->relocs.empty());
    std::uint16_t idx_rela = SHN_UNDEF;
    if (has_relocs) {
        off = align_to(off, 8);
        Elf64_Shdr sh{};
        sh.sh_name = add_name(".rela.text"); sh.sh_type = SHT_RELA;
        sh.sh_offset = off;
        sh.sh_size   = text_it->relocs.size() * sizeof(Elf64_Rela);
        sh.sh_addralign = 8; sh.sh_entsize = sizeof(Elf64_Rela);
        idx_rela = add_sh(sh);
        off += sh.sh_size;
    }
    std::uint16_t idx_shstr = SHN_UNDEF;
    {
        Elf64_Shdr sh{};
        sh.sh_name = add_name(".shstrtab"); sh.sh_type = SHT_STRTAB;
        sh.sh_offset = off; sh.sh_size = shstr.size(); sh.sh_addralign = 1;
        idx_shstr = add_sh(sh);
        off += sh.sh_size;
    }

    // Fix up symbol section indices
    for (std::size_t i = 1; i < symtab.size(); ++i) {
        const auto &sym = obj_symbols[i - 1];
        if (sym.defined && sym.section_index != 0xFFFFFFFF && sym.section_index < section_map.size())
            symtab[i].st_shndx = section_map[sym.section_index];
    }
    if (idx_symtab != SHN_UNDEF && idx_strtab != SHN_UNDEF)
        shdrs[idx_symtab].sh_link = idx_strtab;
    if (idx_rela != SHN_UNDEF && idx_symtab != SHN_UNDEF && idx_text != SHN_UNDEF) {
        shdrs[idx_rela].sh_link = idx_symtab;
        shdrs[idx_rela].sh_info = idx_text;
    }

    Elf64_Ehdr ehdr{};
    std::memcpy(ehdr.e_ident, ELFMAG, SELFMAG);
    ehdr.e_ident[EI_CLASS]   = ELFCLASS64;
    ehdr.e_ident[EI_DATA]    = ELFDATA2LSB;
    ehdr.e_ident[EI_VERSION] = EV_CURRENT;
    ehdr.e_type    = ET_REL;
    ehdr.e_machine = is_arm64 ? EM_AARCH64 : EM_X86_64;
    ehdr.e_version = EV_CURRENT;
    ehdr.e_ehsize  = sizeof(Elf64_Ehdr);
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    off = align_to(off, 8);
    ehdr.e_shoff   = off;
    ehdr.e_shnum   = static_cast<std::uint16_t>(shdrs.size());
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

#define WRITE_IF(idx, iter, bytes) \
    if (idx != SHN_UNDEF && iter != obj_sections.end()) { \
        pad_to(shdrs[idx].sh_offset); \
        ofs.write(reinterpret_cast<const char *>(iter->data.data()), \
                  static_cast<std::streamsize>(bytes)); \
        cursor += bytes; \
    }
    WRITE_IF(idx_text, text_it, text_it->data.size())
    WRITE_IF(idx_data, data_it, data_it->data.size())
#undef WRITE_IF

    if (idx_symtab != SHN_UNDEF) {
        pad_to(shdrs[idx_symtab].sh_offset);
        ofs.write(reinterpret_cast<const char *>(symtab.data()),
                  static_cast<std::streamsize>(symtab.size() * sizeof(Elf64_Sym)));
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
            std::uint32_t rtype = is_arm64
                ? (r.type == 1 ? R_AARCH64_CALL26 : R_AARCH64_JUMP26)
                : (r.type == 1 ? R_X86_64_PC32 : R_X86_64_64);
            rela.r_info   = ELF64_R_INFO(static_cast<std::uint32_t>(r.symbol_index + 1), rtype);
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
    ofs.write(reinterpret_cast<const char *>(shdrs.data()),
              static_cast<std::streamsize>(shdrs.size() * sizeof(Elf64_Shdr)));
    return ofs.good() ? path : std::string{};
}

// ── Mach-O64 ────────────────────────────────────────────────────────────────

#if defined(__APPLE__)
std::string BuildMachO64(const std::string &path,
                         const std::vector<ObjSection> &obj_sections,
                         const std::vector<ObjSymbol>  &obj_symbols,
                         const std::string &arch) {
    auto text_it = std::find_if(obj_sections.begin(), obj_sections.end(),
                                [](const ObjSection &s){ return s.name == ".text"; });
    bool is_arm64 = (arch == "arm64" || arch == "aarch64" || arch == "armv8");
    std::size_t text_size = text_it != obj_sections.end() ? text_it->data.size() : 0;

    mach_header_64 mh{};
    mh.magic     = MH_MAGIC_64;
    mh.cputype   = is_arm64 ? CPU_TYPE_ARM64 : CPU_TYPE_X86_64;
    mh.cpusubtype = is_arm64 ? CPU_SUBTYPE_ARM64_ALL : CPU_SUBTYPE_X86_64_ALL;
    mh.filetype  = MH_OBJECT;
    mh.ncmds     = 2;
    mh.sizeofcmds = sizeof(segment_command_64) + sizeof(section_64) + sizeof(symtab_command);
    mh.flags     = MH_SUBSECTIONS_VIA_SYMBOLS;

    segment_command_64 seg{};
    seg.cmd = LC_SEGMENT_64;
    seg.cmdsize = sizeof(segment_command_64) + sizeof(section_64);
    std::memcpy(seg.segname, "__TEXT", 6);
    seg.vmsize = text_size; seg.filesize = text_size;
    seg.maxprot = VM_PROT_READ | VM_PROT_EXECUTE;
    seg.initprot = VM_PROT_READ | VM_PROT_EXECUTE;
    seg.nsects = 1;

    section_64 sec{};
    std::memcpy(sec.sectname, "__text", 6);
    std::memcpy(sec.segname,  "__TEXT", 6);
    sec.size  = text_size;
    sec.align = 4;
    sec.flags = S_REGULAR | S_ATTR_SOME_INSTRUCTIONS;
    sec.offset = sizeof(mach_header_64) + seg.cmdsize + sizeof(symtab_command);

    std::string strtab(1, '\0');
    std::vector<nlist_64> nlists;
    std::unordered_map<std::string, std::uint32_t> sym_index;
    for (const auto &sym : obj_symbols) {
        if (sym_index.count(sym.name)) continue;
        std::uint32_t name_off = static_cast<std::uint32_t>(strtab.size());
        strtab.append(sym.name); strtab.push_back('\0');
        nlist_64 nl{};
        nl.n_un.n_strx = name_off;
        bool defined = sym.defined && sym.section_index != 0xFFFFFFFF;
        nl.n_type = defined ? (N_EXT | N_SECT) : (N_EXT | N_UNDF);
        nl.n_sect  = defined ? 1 : 0;
        nl.n_value = defined ? sym.value : 0;
        sym_index[sym.name] = static_cast<std::uint32_t>(nlists.size());
        nlists.push_back(nl);
    }

    std::size_t reloc_bytes = (text_it != obj_sections.end())
        ? text_it->relocs.size() * sizeof(relocation_info) : 0;
    sec.reloff  = static_cast<std::uint32_t>(sec.offset + text_size);
    sec.nreloc  = static_cast<std::uint32_t>(text_it != obj_sections.end() ? text_it->relocs.size() : 0);

    symtab_command st{};
    st.cmd      = LC_SYMTAB;
    st.cmdsize  = sizeof(symtab_command);
    st.symoff   = static_cast<std::uint32_t>(sec.reloff + reloc_bytes);
    st.nsyms    = static_cast<std::uint32_t>(nlists.size());
    st.stroff   = st.symoff + static_cast<std::uint32_t>(nlists.size() * sizeof(nlist_64));
    st.strsize  = static_cast<std::uint32_t>(strtab.size());

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return {};
    ofs.write(reinterpret_cast<const char *>(&mh),  sizeof(mh));
    ofs.write(reinterpret_cast<const char *>(&seg), sizeof(seg));
    ofs.write(reinterpret_cast<const char *>(&sec), sizeof(sec));
    ofs.write(reinterpret_cast<const char *>(&st),  sizeof(st));
    if (text_it != obj_sections.end()) {
        ofs.write(reinterpret_cast<const char *>(text_it->data.data()),
                  static_cast<std::streamsize>(text_it->data.size()));
        for (const auto &r : text_it->relocs) {
            relocation_info ri{};
            ri.r_address   = static_cast<std::int32_t>(r.offset);
            ri.r_symbolnum = static_cast<std::int32_t>(r.symbol_index);
            ri.r_pcrel     = (r.type == 1);
            ri.r_length    = 2;
            ri.r_extern    = 1;
            ri.r_type      = is_arm64 ? 2 : 0;
            ofs.write(reinterpret_cast<const char *>(&ri), sizeof(ri));
        }
    }
    for (const auto &n : nlists)
        ofs.write(reinterpret_cast<const char *>(&n), sizeof(n));
    ofs.write(strtab.data(), static_cast<std::streamsize>(strtab.size()));
    return ofs.good() ? path : std::string{};
}
#else
std::string BuildMachO64(const std::string &, const std::vector<ObjSection> &,
                         const std::vector<ObjSymbol> &, const std::string &) {
    return {};
}
#endif

// ── COFF ────────────────────────────────────────────────────────────────────

#pragma pack(push, 1)
struct CoffFileHeader {
    std::uint16_t Machine, NumberOfSections;
    std::uint32_t TimeDateStamp, PointerToSymbolTable, NumberOfSymbols;
    std::uint16_t SizeOfOptionalHeader, Characteristics;
};
struct CoffSectionHeader {
    char Name[8];
    std::uint32_t VirtualSize, VirtualAddress, SizeOfRawData,
                  PointerToRawData, PointerToRelocations, PointerToLinenumbers;
    std::uint16_t NumberOfRelocations, NumberOfLinenumbers;
    std::uint32_t Characteristics;
};
struct CoffSymbol {
    union {
        char ShortName[8];
        struct { std::uint32_t Zeroes, Offset; } LongName;
    } Name;
    std::uint32_t Value;
    std::int16_t  SectionNumber;
    std::uint16_t Type;
    std::uint8_t  StorageClass, NumberOfAuxSymbols;
};
struct CoffRelocation {
    std::uint32_t VirtualAddress, SymbolTableIndex;
    std::uint16_t Type;
};
#pragma pack(pop)

constexpr std::uint16_t kCoffMachineAMD64  = 0x8664;
constexpr std::uint16_t kCoffMachineARM64  = 0xAA64;
constexpr std::uint32_t kCoffSecText = 0x60000020;
constexpr std::uint32_t kCoffSecData = 0xC0000040;
constexpr std::uint32_t kCoffSecBss  = 0xC0000080;
constexpr std::uint8_t  kCoffSymExternal = 2;
constexpr std::uint8_t  kCoffSymStatic   = 3;
constexpr std::uint16_t kCoffRelocAMD64Addr64   = 0x0001;
constexpr std::uint16_t kCoffRelocAMD64Rel32    = 0x0004;
constexpr std::uint16_t kCoffRelocARM64Branch26 = 0x0003;

std::string BuildCoff(const std::string &path,
                      const std::vector<ObjSection> &obj_sections,
                      const std::vector<ObjSymbol>  &obj_symbols,
                      const std::string &arch) {
    bool is_arm64 = (arch == "arm64" || arch == "aarch64" || arch == "armv8");

    std::vector<CoffSectionHeader> sec_headers;
    std::vector<const ObjSection *> sec_ptrs;
    std::unordered_map<std::string, std::uint16_t> sec_name_to_idx;

    for (const auto &s : obj_sections) {
        CoffSectionHeader sh{}; std::memset(&sh, 0, sizeof(sh));
        std::memcpy(sh.Name, s.name.data(), std::min<std::size_t>(s.name.size(), 8));
        sh.SizeOfRawData = static_cast<std::uint32_t>(s.data.size());
        if (s.name == ".text" || s.name == ".code") sh.Characteristics = kCoffSecText;
        else if (s.bss) { sh.Characteristics = kCoffSecBss; sh.SizeOfRawData = 0;
                          sh.VirtualSize = static_cast<std::uint32_t>(s.data.size()); }
        else sh.Characteristics = kCoffSecData;
        sec_name_to_idx[s.name] = static_cast<std::uint16_t>(sec_headers.size());
        sec_headers.push_back(sh); sec_ptrs.push_back(&s);
    }

    std::string strtab(4, '\0');
    std::vector<CoffSymbol> coff_syms;
    std::unordered_map<std::string, std::uint32_t> sym_name_to_idx;
    for (const auto &sym : obj_symbols) {
        CoffSymbol cs{}; std::memset(&cs, 0, sizeof(cs));
        if (sym.name.size() <= 8) {
            std::memcpy(cs.Name.ShortName, sym.name.data(), sym.name.size());
        } else {
            cs.Name.LongName.Zeroes = 0;
            cs.Name.LongName.Offset = static_cast<std::uint32_t>(strtab.size());
            strtab.append(sym.name); strtab.push_back('\0');
        }
        cs.Value = static_cast<std::uint32_t>(sym.value);
        cs.SectionNumber = (sym.defined && sym.section_index != 0xFFFFFFFF)
                         ? static_cast<std::int16_t>(sym.section_index + 1) : 0;
        cs.Type = 0x20;
        cs.StorageClass = sym.global ? kCoffSymExternal : kCoffSymStatic;
        sym_name_to_idx[sym.name] = static_cast<std::uint32_t>(coff_syms.size());
        coff_syms.push_back(cs);
    }
    std::uint32_t strtab_size = static_cast<std::uint32_t>(strtab.size());
    std::memcpy(strtab.data(), &strtab_size, 4);

    std::vector<std::vector<CoffRelocation>> sec_relocs(sec_headers.size());
    for (std::size_t si = 0; si < obj_sections.size(); ++si) {
        for (const auto &r : obj_sections[si].relocs) {
            CoffRelocation cr{};
            cr.VirtualAddress = static_cast<std::uint32_t>(r.offset);
            auto it = (r.symbol_index < obj_symbols.size())
                    ? sym_name_to_idx.find(obj_symbols[r.symbol_index].name)
                    : sym_name_to_idx.end();
            cr.SymbolTableIndex = it != sym_name_to_idx.end() ? it->second : r.symbol_index;
            cr.Type = is_arm64 ? kCoffRelocARM64Branch26
                               : (r.type == 0 ? kCoffRelocAMD64Addr64 : kCoffRelocAMD64Rel32);
            sec_relocs[si].push_back(cr);
        }
    }

    std::size_t offset = sizeof(CoffFileHeader) + sec_headers.size() * sizeof(CoffSectionHeader);
    for (std::size_t i = 0; i < sec_headers.size(); ++i) {
        if (sec_ptrs[i]->bss) { sec_headers[i].PointerToRawData = 0; }
        else { sec_headers[i].PointerToRawData = static_cast<std::uint32_t>(offset);
               offset += sec_ptrs[i]->data.size(); }
    }
    for (std::size_t i = 0; i < sec_headers.size(); ++i) {
        if (!sec_relocs[i].empty()) {
            sec_headers[i].PointerToRelocations = static_cast<std::uint32_t>(offset);
            sec_headers[i].NumberOfRelocations  = static_cast<std::uint16_t>(sec_relocs[i].size());
            offset += sec_relocs[i].size() * sizeof(CoffRelocation);
        }
    }

    CoffFileHeader fh{};
    fh.Machine              = is_arm64 ? kCoffMachineARM64 : kCoffMachineAMD64;
    fh.NumberOfSections     = static_cast<std::uint16_t>(sec_headers.size());
    fh.PointerToSymbolTable = static_cast<std::uint32_t>(offset);
    fh.NumberOfSymbols      = static_cast<std::uint32_t>(coff_syms.size());

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return {};
    ofs.write(reinterpret_cast<const char *>(&fh), sizeof(fh));
    ofs.write(reinterpret_cast<const char *>(sec_headers.data()),
              static_cast<std::streamsize>(sec_headers.size() * sizeof(CoffSectionHeader)));
    for (std::size_t i = 0; i < sec_ptrs.size(); ++i)
        if (!sec_ptrs[i]->bss && !sec_ptrs[i]->data.empty())
            ofs.write(reinterpret_cast<const char *>(sec_ptrs[i]->data.data()),
                      static_cast<std::streamsize>(sec_ptrs[i]->data.size()));
    for (std::size_t i = 0; i < sec_relocs.size(); ++i)
        if (!sec_relocs[i].empty())
            ofs.write(reinterpret_cast<const char *>(sec_relocs[i].data()),
                      static_cast<std::streamsize>(sec_relocs[i].size() * sizeof(CoffRelocation)));
    ofs.write(reinterpret_cast<const char *>(coff_syms.data()),
              static_cast<std::streamsize>(coff_syms.size() * sizeof(CoffSymbol)));
    ofs.write(strtab.data(), static_cast<std::streamsize>(strtab.size()));
    return ofs.good() ? path : std::string{};
}

// ── EmitObject dispatcher ───────────────────────────────────────────────────

std::string EmitObject(const DriverSettings &settings,
                       const std::vector<ObjSection> &sections,
                       const std::vector<ObjSymbol>  &symbols) {
    std::string ext;
    const std::string &fmt = settings.obj_format;
    if (fmt == "pobj") ext = ".pobj";
    else if (fmt == "coff") ext = ".obj";
    else ext = ".o";

    std::string out = settings.emit_obj_path.empty()
                    ? settings.output + ext
                    : settings.emit_obj_path;

    if (fmt == "pobj") return BuildPobj(out, sections, symbols);
    if (fmt == "coff") {
        auto w = BuildCoff(out, sections, symbols, settings.arch);
        if (w.empty()) std::cerr << "[error] COFF emit failed\n";
        return w;
    }
    if (fmt == "elf") {
        auto w = BuildElf64(out, sections, symbols, settings.arch);
        if (w.empty()) std::cerr << "[error] ELF emit failed\n";
        return w;
    }
    if (fmt == "macho") {
#if defined(__APPLE__)
        auto w = BuildMachO64(out, sections, symbols, settings.arch);
        if (w.empty()) std::cerr << "[error] Mach-O emit failed\n";
        return w;
#else
        std::cerr << "[error] Mach-O requested on non-Apple platform\n";
        return {};
#endif
    }
    std::cerr << "[warn] unknown obj format '" << fmt << "', falling back to POBJ\n";
    return BuildPobj(out, sections, symbols);
}

// ── PAUX helper ─────────────────────────────────────────────────────────────

void WriteAuxBinary(const std::string &aux_dir, const std::string &filename,
                    const std::vector<std::pair<std::string, std::string>> &secs,
                    bool verbose) {
    if (aux_dir.empty()) return;
    fs::path path = fs::path(aux_dir) / filename;
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return;
    PAuxHeader hdr{};
    std::memcpy(hdr.magic, "PAUX", 4);
    hdr.version = 1;
    hdr.section_count = static_cast<std::uint16_t>(secs.size());
    ofs.write(reinterpret_cast<const char *>(&hdr), sizeof(hdr));
    std::size_t total = sizeof(PAuxHeader);
    for (const auto &[name, data] : secs) {
        std::uint16_t nl = static_cast<std::uint16_t>(name.size());
        std::uint32_t dl = static_cast<std::uint32_t>(data.size());
        ofs.write(reinterpret_cast<const char *>(&nl), 2);
        ofs.write(name.data(), nl);
        ofs.write(reinterpret_cast<const char *>(&dl), 4);
        ofs.write(data.data(), dl);
        total += 2 + nl + 4 + dl;
    }
    if (verbose)
        std::cerr << "[polyc]   -> " << path.string()
                  << " (" << total << " bytes, binary)\n";
}

void WriteAuxBinarySingle(const std::string &aux_dir, const std::string &filename,
                          const std::string &sec_name, const std::string &content,
                          bool verbose) {
    WriteAuxBinary(aux_dir, filename, {{sec_name, content}}, verbose);
}

}  // namespace (anonymous)

// ════════════════════════════════════════════════════════════════════════════
// RunPackagingStage
// ════════════════════════════════════════════════════════════════════════════

PackagingResult RunPackagingStage(const DriverSettings &settings,
                                  const BackendResult   &backend,
                                  const BridgeResult    &bridge,
                                  const std::string     &aux_dir,
                                  const std::string     &stem) {
    PackagingResult result;
    const bool V = settings.verbose;

    if (!backend.success) {
        result.diagnostics.Report(
            core::SourceLoc{"<packaging>", 1, 1},
            "backend stage did not succeed; skipping packaging");
        result.success = false;
        return result;
    }

    const auto &sections = backend.sections;
    const auto &symbols  = backend.symbols;

    // ── Emit IR file ──────────────────────────────────────────────────────
    if (!settings.emit_ir_path.empty() && !backend.ir_text.empty()) {
        std::ofstream ofs(settings.emit_ir_path);
        if (ofs.is_open()) ofs << backend.ir_text;
    }

    // ── Emit assembly text file ───────────────────────────────────────────
    if (!settings.emit_asm_path.empty() && !backend.assembly_text.empty()) {
        std::ofstream ofs(settings.emit_asm_path);
        if (ofs.is_open()) ofs << backend.assembly_text;
    }

    // ── Write assembly to aux ─────────────────────────────────────────────
    WriteAuxBinarySingle(aux_dir, stem + ".asm.paux", "assembly",
                         backend.assembly_text, V);

    // ── Per-language bridge library emission ──────────────────────────────
    if (!aux_dir.empty() && settings.language == "ploy") {
        std::unordered_map<std::string, std::vector<std::size_t>> lang_groups;
        for (std::size_t i = 0; i < symbols.size(); ++i) {
            const auto &name = symbols[i].name;
            if (name.rfind("__ploy_bridge_ploy_", 0) == 0) {
                auto rest = name.substr(19);
                auto sep  = rest.find('_');
                if (sep != std::string::npos)
                    lang_groups[rest.substr(0, sep)].push_back(i);
            }
        }
        for (const auto &[lang, indices] : lang_groups) {
            std::vector<ObjSymbol> lib_syms;
            for (auto idx : indices)
                if (idx < symbols.size()) lib_syms.push_back(symbols[idx]);
            if (lib_syms.empty()) continue;
            fs::path lib_path = fs::path(aux_dir) / (stem + "_" + lang + ".lib.pobj");
            auto written = BuildPobj(lib_path.string(), sections, lib_syms);
            if (!written.empty() && V)
                std::cerr << "[polyc]   -> " << lib_path.string()
                          << " (" << lang << " bridge library)\n";
        }
    }

    // ── Object emission + optional link ──────────────────────────────────
    auto emit_and_link = [&](bool do_link) -> bool {
        std::string obj_path = EmitObject(settings, sections, symbols);
        if (obj_path.empty()) {
            result.diagnostics.Report(
                core::SourceLoc{"<packaging>", 1, 1},
                "failed to emit object file");
            return false;
        }
        result.obj_path = obj_path;
        if (V) std::cerr << "[polyc] Produced: " << obj_path << "\n";

        // Copy into aux/
        if (!aux_dir.empty()) {
            std::string ext = (settings.obj_format == "coff") ? ".obj"
                            : (settings.obj_format == "pobj") ? ".pobj" : ".o";
            fs::path aux_obj = fs::path(aux_dir) / (stem + ext);
            if (aux_obj.string() != obj_path) {
                std::error_code ec;
                fs::copy_file(obj_path, aux_obj,
                              fs::copy_options::overwrite_existing, ec);
                if (!ec && V)
                    std::cerr << "[polyc]   -> " << aux_obj.string()
                              << " (object copy in aux)\n";
            }
        }

        if (!do_link) return true;

        std::string out_exe = settings.output;
        std::string desc_arg = bridge.descriptor_file.empty()
            ? std::string{}
            : " --ploy-desc " + bridge.descriptor_file;
        std::string aux_arg  = aux_dir.empty()
            ? std::string{}
            : " --aux-dir " + aux_dir;

        if (settings.obj_format == "pobj") {
            std::string cmd = settings.polyld_path + " " + obj_path
                            + desc_arg + aux_arg + " -o " + out_exe;
            if (V) std::cerr << "[polyc] Invoking polyld -> " << out_exe << "\n";
            int rc = std::system(cmd.c_str());
            if (rc != 0) {
                result.diagnostics.Report(
                    core::SourceLoc{"<packaging>", 1, 1},
                    "polyld failed (rc=" + std::to_string(rc) + ")");
                return false;
            }
        } else if (settings.obj_format == "coff") {
            std::string cmd = "link /NOLOGO /OUT:" + out_exe + " " + obj_path;
            if (V) std::cerr << "[polyc] Invoking link.exe -> " << out_exe << "\n";
            int rc = std::system(cmd.c_str());
            if (rc != 0) {
                cmd = "lld-link /OUT:" + out_exe + " " + obj_path;
                rc  = std::system(cmd.c_str());
            }
            if (rc != 0)
                std::cerr << "[warn] system linker not available; object at: "
                          << obj_path << "\n";
        } else {
            std::string cmd = "clang -o " + out_exe + " " + obj_path;
            if (V) std::cerr << "[polyc] Invoking system linker -> " << out_exe << "\n";
            int rc = std::system(cmd.c_str());
            if (rc != 0)
                std::cerr << "[warn] system linker not available; object at: "
                          << obj_path << "\n";
        }
        result.output_path = out_exe;
        if (V) std::cerr << "[polyc] Link stage completed\n";
        return true;
    };

    bool ok = false;
    if (settings.mode == "link")        ok = emit_and_link(true);
    else /* compile | assemble */       ok = emit_and_link(false);

    result.success = ok;
    return result;
}

}  // namespace polyglot::tools
