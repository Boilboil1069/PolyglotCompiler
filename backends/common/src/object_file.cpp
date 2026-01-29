#include "backends/common/include/object_file.h"

#include <algorithm>
#include <cstring>

namespace polyglot::backends {
namespace {

// ELF constants
constexpr std::uint8_t ELFCLASS64 = 2;
constexpr std::uint8_t ELFDATA2LSB = 1;
constexpr std::uint8_t EV_CURRENT = 1;
constexpr std::uint16_t ET_REL = 1;
constexpr std::uint16_t EM_X86_64 = 62;
constexpr std::uint16_t EM_AARCH64 = 183;

constexpr std::uint32_t SHT_NULL = 0;
constexpr std::uint32_t SHT_PROGBITS = 1;
constexpr std::uint32_t SHT_SYMTAB = 2;
constexpr std::uint32_t SHT_STRTAB = 3;
constexpr std::uint32_t SHT_RELA = 4;

constexpr std::uint64_t SHF_WRITE = 0x1;
constexpr std::uint64_t SHF_ALLOC = 0x2;
constexpr std::uint64_t SHF_EXECINSTR = 0x4;

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
    WriteValue<std::uint16_t>(result, EM_X86_64);  // e_machine
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
    
    std::uint16_t shnum = static_cast<std::uint16_t>(sections_.size() + 4); // +null, symtab, strtab, shstrtab
    WriteValue<std::uint16_t>(result, shnum);  // e_shnum
    WriteValue<std::uint16_t>(result, static_cast<std::uint16_t>(shnum - 1));  // e_shstrndx
    
    // Section data
    std::vector<std::uint64_t> section_offsets;
    for (const auto &sec : sections_) {
        AlignTo(result, 16);
        section_offsets.push_back(result.size());
        result.insert(result.end(), sec.data.begin(), sec.data.end());
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
    
    return result;
}

void MachOBuilder::AddSection(const Section &section) {
    sections_.push_back(section);
}

void MachOBuilder::AddSymbol(const Symbol &symbol) {
    symbols_.push_back(symbol);
}

std::vector<std::uint8_t> MachOBuilder::Build() {
    // Simplified Mach-O generation (64-bit)
    std::vector<std::uint8_t> result;
    
    // Mach-O header (mach_header_64)
    WriteValue<std::uint32_t>(result, 0xFEEDFACF);  // MH_MAGIC_64
    WriteValue<std::uint32_t>(result, is_arm64_ ? 0x0100000C : 0x01000007);  // CPU_TYPE
    WriteValue<std::uint32_t>(result, is_arm64_ ? 0 : 3);  // CPU_SUBTYPE
    WriteValue<std::uint32_t>(result, 1);  // MH_OBJECT
    WriteValue<std::uint32_t>(result, 2);  // ncmds
    WriteValue<std::uint32_t>(result, 0);  // sizeofcmds (filled later)
    WriteValue<std::uint32_t>(result, 0);  // flags
    WriteValue<std::uint32_t>(result, 0);  // reserved
    
    // For now, return minimal Mach-O - full implementation would add:
    // - LC_SEGMENT_64 load commands
    // - Section headers within segments
    // - Symbol table (LC_SYMTAB)
    // - String table
    
    return result;
}

} // namespace polyglot::backends
