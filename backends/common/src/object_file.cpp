#include "backends/common/include/object_file.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

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

template<typename T>
void WriteValue(std::vector<std::uint8_t> &out, T value) {
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF));
    }
}

void WriteBytes(std::vector<std::uint8_t> &out, const void *data, std::size_t size) {
    const auto *bytes = static_cast<const std::uint8_t*>(data);
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
    std::vector<std::size_t> rela_section_indices;  // original section index
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
    
    WriteValue<std::uint16_t>(result, ET_REL);  // e_type
    WriteValue<std::uint16_t>(result, is_x64_ ? EM_X86_64 : EM_AARCH64);  // e_machine
    WriteValue<std::uint32_t>(result, EV_CURRENT);  // e_version
    WriteValue<std::uint64_t>(result, 0);  // e_entry
    WriteValue<std::uint64_t>(result, 0);  // e_phoff
    
    // e_shoff will be filled later
    std::size_t e_shoff_pos = result.size();
    WriteValue<std::uint64_t>(result, 0);
    
    WriteValue<std::uint32_t>(result, 0);  // e_flags
    WriteValue<std::uint16_t>(result, 64);  // e_ehsize
    WriteValue<std::uint16_t>(result, 0);  // e_phentsize
    WriteValue<std::uint16_t>(result, 0);  // e_phnum
    WriteValue<std::uint16_t>(result, 64);  // e_shentsize
    
    std::uint16_t shnum = static_cast<std::uint16_t>(sections_.size() + 4 + rela_section_indices.size()); // +null, symtab, strtab, shstrtab, rela sections
    WriteValue<std::uint16_t>(result, shnum);  // e_shnum
    WriteValue<std::uint16_t>(result, static_cast<std::uint16_t>(shnum - 1));  // e_shstrndx
    
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
            WriteValue<std::uint64_t>(result, rel.offset);  // r_offset
            // Build r_info: find symbol index
            std::uint64_t sym_idx = 0;
            for (std::size_t si = 0; si < symbols_.size(); ++si) {
                if (symbols_[si].name == rel.symbol) {
                    sym_idx = si + 1;  // +1 because null symbol at 0
                    break;
                }
            }
            std::uint64_t r_info = (sym_idx << 32) | static_cast<std::uint32_t>(rel.type);
            WriteValue<std::uint64_t>(result, r_info);  // r_info
            WriteValue<std::int64_t>(result, rel.addend);  // r_addend
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
    WriteValue<std::uint32_t>(result, 0);  // st_name
    WriteValue<std::uint8_t>(result, 0);   // st_info
    WriteValue<std::uint8_t>(result, 0);   // st_other
    WriteValue<std::uint16_t>(result, 0);  // st_shndx
    WriteValue<std::uint64_t>(result, 0);  // st_value
    WriteValue<std::uint64_t>(result, 0);  // st_size
    
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
        WriteValue<std::uint8_t>(result, 0);  // st_other
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
    for (int i = 0; i < 64; ++i) result.push_back(0);
    
    // Regular section headers
    for (std::size_t i = 0; i < sections_.size(); ++i) {
        const auto &sec = sections_[i];
        std::uint32_t sh_type = (sec.name == ".text") ? SHT_PROGBITS : SHT_PROGBITS;
        std::uint64_t sh_flags = 0;
        if (sec.name == ".text") sh_flags = SHF_ALLOC | SHF_EXECINSTR;
        else if (sec.name == ".data") sh_flags = SHF_ALLOC | SHF_WRITE;
        
        WriteValue<std::uint32_t>(result, section_name_offsets[i]);
        WriteValue<std::uint32_t>(result, sh_type);
        WriteValue<std::uint64_t>(result, sh_flags);
        WriteValue<std::uint64_t>(result, 0);  // sh_addr
        WriteValue<std::uint64_t>(result, section_offsets[i]);
        WriteValue<std::uint64_t>(result, sec.data.size());
        WriteValue<std::uint32_t>(result, 0);  // sh_link
        WriteValue<std::uint32_t>(result, 0);  // sh_info
        WriteValue<std::uint64_t>(result, 16);  // sh_addralign
        WriteValue<std::uint64_t>(result, 0);  // sh_entsize
    }
    
    // .symtab section header
    WriteValue<std::uint32_t>(result, symtab_name_offset);
    WriteValue<std::uint32_t>(result, SHT_SYMTAB);
    WriteValue<std::uint64_t>(result, 0);  // sh_flags
    WriteValue<std::uint64_t>(result, 0);  // sh_addr
    WriteValue<std::uint64_t>(result, symtab_offset);
    WriteValue<std::uint64_t>(result, symtab_size);
    WriteValue<std::uint32_t>(result, static_cast<std::uint32_t>(sections_.size() + 2));  // sh_link (strtab)
    WriteValue<std::uint32_t>(result, 1);  // sh_info (first global symbol)
    WriteValue<std::uint64_t>(result, 8);  // sh_addralign
    WriteValue<std::uint64_t>(result, 24);  // sh_entsize
    
    // .strtab section header
    WriteValue<std::uint32_t>(result, symstrtab_name_offset);
    WriteValue<std::uint32_t>(result, SHT_STRTAB);
    WriteValue<std::uint64_t>(result, 0);  // sh_flags
    WriteValue<std::uint64_t>(result, 0);  // sh_addr
    WriteValue<std::uint64_t>(result, sym_strtab_offset);
    WriteValue<std::uint64_t>(result, sym_strtab_size);
    WriteValue<std::uint32_t>(result, 0);  // sh_link
    WriteValue<std::uint32_t>(result, 0);  // sh_info
    WriteValue<std::uint64_t>(result, 1);  // sh_addralign
    WriteValue<std::uint64_t>(result, 0);  // sh_entsize
    
    // .shstrtab section header
    WriteValue<std::uint32_t>(result, shstrtab_name_offset);
    WriteValue<std::uint32_t>(result, SHT_STRTAB);
    WriteValue<std::uint64_t>(result, 0);  // sh_flags
    WriteValue<std::uint64_t>(result, 0);  // sh_addr
    WriteValue<std::uint64_t>(result, shstrtab_offset);
    WriteValue<std::uint64_t>(result, shstrtab_size);
    WriteValue<std::uint32_t>(result, 0);  // sh_link
    WriteValue<std::uint32_t>(result, 0);  // sh_info
    WriteValue<std::uint64_t>(result, 1);  // sh_addralign
    WriteValue<std::uint64_t>(result, 0);  // sh_entsize
    
    // .rela section headers
    for (std::size_t ri = 0; ri < rela_section_indices.size(); ++ri) {
        std::uint32_t target_shndx = static_cast<std::uint32_t>(rela_section_indices[ri] + 1);  // +1 for null
        std::uint32_t symtab_shndx = static_cast<std::uint32_t>(sections_.size() + 1);  // .symtab index
        WriteValue<std::uint32_t>(result, rela_name_offsets[ri]);
        WriteValue<std::uint32_t>(result, SHT_RELA);
        WriteValue<std::uint64_t>(result, SHF_INFO_LINK);  // sh_flags
        WriteValue<std::uint64_t>(result, 0);  // sh_addr
        WriteValue<std::uint64_t>(result, rela_offsets[ri]);  // sh_offset
        WriteValue<std::uint64_t>(result, rela_sizes[ri]);  // sh_size
        WriteValue<std::uint32_t>(result, symtab_shndx);  // sh_link -> .symtab
        WriteValue<std::uint32_t>(result, target_shndx);  // sh_info -> target section
        WriteValue<std::uint64_t>(result, 8);  // sh_addralign
        WriteValue<std::uint64_t>(result, 24);  // sh_entsize (sizeof Elf64_Rela)
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
    
    // Compute section data layout
    // Header: 32 bytes
    // LC_SEGMENT_64: 72 bytes + 80 bytes per section
    // LC_SYMTAB: 24 bytes
    std::uint32_t segment_cmd_size = 72 + static_cast<std::uint32_t>(80 * sections_.size());
    std::uint32_t symtab_cmd_size = 24;
    std::uint32_t ncmds = 2;
    std::uint32_t sizeofcmds = segment_cmd_size + symtab_cmd_size;
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
    for (std::size_t i = 0; i < sections_.size(); ++i) {
        if (!sections_[i].relocations.empty()) {
            sec_reloff[i] = current_offset;
            sec_nreloc[i] = static_cast<std::uint32_t>(sections_[i].relocations.size());
            current_offset += sec_nreloc[i] * 8;
            current_offset = (current_offset + 3) & ~3u;  // 4-byte align
        }
    }
    
    // Symbol and string table after sections and relocations
    std::uint32_t strtab_offset = current_offset;
    std::vector<std::uint8_t> strtab;
    strtab.push_back(0x20);  // Start with space (Mach-O convention: first byte pad)
    strtab.push_back(0);     // Null terminator for empty string
    
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
    
    // --- Write Mach-O header (mach_header_64) ---
    WriteValue<std::uint32_t>(result, MH_MAGIC_64);
    WriteValue<std::uint32_t>(result, is_arm64_ ? CPU_TYPE_ARM64 : CPU_TYPE_X86_64);
    WriteValue<std::uint32_t>(result, is_arm64_ ? CPU_SUBTYPE_ARM64_ALL : CPU_SUBTYPE_X86_64_ALL);
    WriteValue<std::uint32_t>(result, MH_OBJECT);
    WriteValue<std::uint32_t>(result, ncmds);
    WriteValue<std::uint32_t>(result, sizeofcmds);
    WriteValue<std::uint32_t>(result, 0);  // flags
    WriteValue<std::uint32_t>(result, 0);  // reserved
    
    // --- LC_SEGMENT_64 ---
    WriteValue<std::uint32_t>(result, LC_SEGMENT_64);
    WriteValue<std::uint32_t>(result, segment_cmd_size);
    
    // segname (16 bytes, empty for object files)
    for (int i = 0; i < 16; ++i) result.push_back(0);
    
    WriteValue<std::uint64_t>(result, 0);  // vmaddr
    std::uint64_t vmsize = current_offset > data_start ? (current_offset - data_start) : 0;
    WriteValue<std::uint64_t>(result, vmsize);  // vmsize
    WriteValue<std::uint64_t>(result, data_start);  // fileoff
    WriteValue<std::uint64_t>(result, vmsize);  // filesize
    WriteValue<std::uint32_t>(result, 7);  // maxprot (rwx)
    WriteValue<std::uint32_t>(result, 7);  // initprot (rwx)
    WriteValue<std::uint32_t>(result, static_cast<std::uint32_t>(sections_.size()));  // nsects
    WriteValue<std::uint32_t>(result, 0);  // flags
    
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
        
        WriteValue<std::uint64_t>(result, 0);  // addr
        WriteValue<std::uint64_t>(result, sec.data.size());  // size
        WriteValue<std::uint32_t>(result, sec_offsets[i]);  // offset
        WriteValue<std::uint32_t>(result, 4);  // align (2^4 = 16)
        WriteValue<std::uint32_t>(result, sec_reloff[i]);  // reloff
        WriteValue<std::uint32_t>(result, sec_nreloc[i]);  // nreloc
        
        // Section flags
        std::uint32_t sec_flags = 0;
        if (sec.name == ".text") {
            sec_flags = 0x80000400;  // S_REGULAR | S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS
        }
        WriteValue<std::uint32_t>(result, sec_flags);
        
        WriteValue<std::uint32_t>(result, 0);  // reserved1
        WriteValue<std::uint32_t>(result, 0);  // reserved2
        WriteValue<std::uint32_t>(result, 0);  // reserved3 (padding for 64-bit)
    }
    
    // --- LC_SYMTAB ---
    WriteValue<std::uint32_t>(result, LC_SYMTAB);
    WriteValue<std::uint32_t>(result, symtab_cmd_size);
    WriteValue<std::uint32_t>(result, symtab_offset);  // symoff
    WriteValue<std::uint32_t>(result, static_cast<std::uint32_t>(symbols_.size()));  // nsyms
    WriteValue<std::uint32_t>(result, strtab_offset);  // stroff
    WriteValue<std::uint32_t>(result, strtab_size);  // strsize
    
    // --- Pad to data start ---
    while (result.size() < data_start) {
        result.push_back(0);
    }
    
    // --- Section data ---
    for (std::size_t i = 0; i < sections_.size(); ++i) {
        while (result.size() < sec_offsets[i]) result.push_back(0);
        result.insert(result.end(), sections_[i].data.begin(), sections_[i].data.end());
    }

    // --- Relocation entries (relocation_info, 8 bytes each) ---
    // Build a symbol name → symbol table index map for relocation lookups.
    std::unordered_map<std::string, std::uint32_t> sym_name_index;
    for (std::uint32_t si = 0; si < symbols_.size(); ++si) {
        sym_name_index[symbols_[si].name] = si;
    }
    for (std::size_t i = 0; i < sections_.size(); ++i) {
        if (sections_[i].relocations.empty()) continue;
        while (result.size() < sec_reloff[i]) result.push_back(0);
        for (const auto &rel : sections_[i].relocations) {
            // r_address (offset within section)
            WriteValue<std::uint32_t>(result, static_cast<std::uint32_t>(rel.offset));
            // Packed: r_symbolnum(24) | r_pcrel(1) | r_length(2) | r_extern(1) | r_type(4)
            std::uint32_t r_symbolnum = 0;
            auto sit = sym_name_index.find(rel.symbol);
            if (sit != sym_name_index.end()) {
                r_symbolnum = sit->second;
            }
            std::uint32_t r_pcrel = (rel.type == 1) ? 1u : 0u;  // type 1 = PC-relative
            std::uint32_t r_length = 3;  // 8 bytes (2^3)
            std::uint32_t r_extern = 1;
            std::uint32_t r_type = 0;    // GENERIC_RELOC_VANILLA
            std::uint32_t packed = (r_symbolnum & 0x00FFFFFFu)
                                 | (r_pcrel << 24)
                                 | (r_length << 25)
                                 | (r_extern << 27)
                                 | (r_type << 28);
            WriteValue<std::uint32_t>(result, packed);
        }
    }
    
    // --- String table ---
    while (result.size() < strtab_offset) result.push_back(0);
    result.insert(result.end(), strtab.begin(), strtab.end());
    
    // --- Symbol table (nlist_64) ---
    AlignTo(result, 8);
    // Adjust symtab_offset if needed to match actual position
    // (the header already points to the computed position)
    while (result.size() < symtab_offset) result.push_back(0);
    
    for (std::size_t i = 0; i < symbols_.size(); ++i) {
        const auto &sym = symbols_[i];
        
        WriteValue<std::uint32_t>(result, sym_str_offsets[i]);  // n_strx
        
        // n_type: N_EXT (1) for global, N_SECT (0x0e) for defined
        std::uint8_t n_type = 0x0e;  // N_SECT
        if (sym.is_global) n_type |= 0x01;  // N_EXT
        result.push_back(n_type);
        
        // n_sect: 1-based section index
        std::uint8_t n_sect = 0;
        for (std::size_t si = 0; si < sections_.size(); ++si) {
            if (sections_[si].name == sym.section) {
                n_sect = static_cast<std::uint8_t>(si + 1);
                break;
            }
        }
        result.push_back(n_sect);
        
        WriteValue<std::uint16_t>(result, 0);  // n_desc
        WriteValue<std::uint64_t>(result, sym.offset);  // n_value
    }
    
    return result;
}

} // namespace polyglot::backends
