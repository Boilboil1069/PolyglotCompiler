#include "tools/polyld/include/linker.h"
#include "tools/polyld/include/polyglot_linker.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>

// ============================================================================
// Platform-specific ELF and Mach-O definitions
// ============================================================================

#if __has_include(<elf.h>)
#include <elf.h>
#else
// Minimal ELF definitions for non-Linux platforms
using Elf64_Addr = std::uint64_t;
using Elf64_Off = std::uint64_t;
using Elf64_Half = std::uint16_t;
using Elf64_Word = std::uint32_t;
using Elf64_Sword = std::int32_t;
using Elf64_Xword = std::uint64_t;
using Elf64_Sxword = std::int64_t;

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

struct Elf64_Phdr {
    Elf64_Word p_type;
    Elf64_Word p_flags;
    Elf64_Off p_offset;
    Elf64_Addr p_vaddr;
    Elf64_Addr p_paddr;
    Elf64_Xword p_filesz;
    Elf64_Xword p_memsz;
    Elf64_Xword p_align;
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
#define ELFCLASS32 1
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2
#define ET_REL 1
#define ET_EXEC 2
#define ET_DYN 3
#define EM_386 3
#define EM_X86_64 62
#define EM_ARM 40
#define EM_AARCH64 183
#define EM_RISCV 243
#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_HASH 5
#define SHT_DYNAMIC 6
#define SHT_NOTE 7
#define SHT_NOBITS 8
#define SHT_REL 9
#define SHT_DYNSYM 11
#define SHT_INIT_ARRAY 14
#define SHT_FINI_ARRAY 15
#define SHF_WRITE 0x1
#define SHF_ALLOC 0x2
#define SHF_EXECINSTR 0x4
#define SHF_MERGE 0x10
#define SHF_STRINGS 0x20
#define SHF_TLS 0x400
#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STB_WEAK 2
#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC 2
#define STT_SECTION 3
#define STT_FILE 4
#define STT_COMMON 5
#define STT_TLS 6
#define STV_DEFAULT 0
#define STV_INTERNAL 1
#define STV_HIDDEN 2
#define STV_PROTECTED 3
#define SHN_UNDEF 0
#define SHN_ABS 0xfff1
#define SHN_COMMON 0xfff2
#define PT_NULL 0
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_NOTE 4
#define PT_PHDR 6
#define PT_GNU_EH_FRAME 0x6474e550
#define PT_GNU_STACK 0x6474e551
#define PT_GNU_RELRO 0x6474e552
#define PF_X 1
#define PF_W 2
#define PF_R 4

#define ELF64_ST_BIND(i) ((i) >> 4)
#define ELF64_ST_TYPE(i) ((i) & 0xf)
#define ELF64_ST_INFO(b, t) (((b) << 4) + ((t) & 0xf))
#define ELF64_ST_VISIBILITY(o) ((o) & 0x3)
#define ELF64_R_SYM(i) ((i) >> 32)
#define ELF64_R_TYPE(i) ((i) & 0xffffffff)
#define ELF64_R_INFO(s, t) (((Elf64_Xword)(s) << 32) + (Elf64_Xword)(t))
#endif

// Mach-O definitions
namespace macho {

// Mach-O magic numbers
constexpr std::uint32_t MH_MAGIC = 0xfeedface;
constexpr std::uint32_t MH_CIGAM = 0xcefaedfe;
constexpr std::uint32_t MH_MAGIC_64 = 0xfeedfacf;
constexpr std::uint32_t MH_CIGAM_64 = 0xcffaedfe;
constexpr std::uint32_t FAT_MAGIC = 0xcafebabe;
constexpr std::uint32_t FAT_CIGAM = 0xbebafeca;

// CPU types
constexpr std::uint32_t CPU_TYPE_X86 = 7;
constexpr std::uint32_t CPU_TYPE_X86_64 = CPU_TYPE_X86 | 0x01000000;
constexpr std::uint32_t CPU_TYPE_ARM = 12;
constexpr std::uint32_t CPU_TYPE_ARM64 = CPU_TYPE_ARM | 0x01000000;

// File types
constexpr std::uint32_t MH_OBJECT = 1;
constexpr std::uint32_t MH_EXECUTE = 2;
constexpr std::uint32_t MH_DYLIB = 6;
constexpr std::uint32_t MH_BUNDLE = 8;
constexpr std::uint32_t MH_DSYM = 10;

// Load command types
constexpr std::uint32_t LC_SEGMENT = 0x1;
constexpr std::uint32_t LC_SYMTAB = 0x2;
constexpr std::uint32_t LC_DYSYMTAB = 0xb;
constexpr std::uint32_t LC_SEGMENT_64 = 0x19;
constexpr std::uint32_t LC_UUID = 0x1b;
constexpr std::uint32_t LC_BUILD_VERSION = 0x32;

// Section flags
constexpr std::uint32_t S_REGULAR = 0x0;
constexpr std::uint32_t S_ZEROFILL = 0x1;
constexpr std::uint32_t S_CSTRING_LITERALS = 0x2;
constexpr std::uint32_t S_SYMBOL_STUBS = 0x8;
constexpr std::uint32_t S_NON_LAZY_SYMBOL_POINTERS = 0x6;
constexpr std::uint32_t S_LAZY_SYMBOL_POINTERS = 0x7;
constexpr std::uint32_t S_ATTR_PURE_INSTRUCTIONS = 0x80000000;
constexpr std::uint32_t S_ATTR_SOME_INSTRUCTIONS = 0x00000400;

// N_TYPE values for nlist
constexpr std::uint8_t N_UNDF = 0x0;
constexpr std::uint8_t N_ABS = 0x2;
constexpr std::uint8_t N_SECT = 0xe;
constexpr std::uint8_t N_PBUD = 0xc;
constexpr std::uint8_t N_INDR = 0xa;
constexpr std::uint8_t N_EXT = 0x01;
constexpr std::uint8_t N_PEXT = 0x10;

#pragma pack(push, 1)

struct mach_header_64 {
    std::uint32_t magic;
    std::uint32_t cputype;
    std::uint32_t cpusubtype;
    std::uint32_t filetype;
    std::uint32_t ncmds;
    std::uint32_t sizeofcmds;
    std::uint32_t flags;
    std::uint32_t reserved;
};

struct load_command {
    std::uint32_t cmd;
    std::uint32_t cmdsize;
};

struct segment_command_64 {
    std::uint32_t cmd;
    std::uint32_t cmdsize;
    char segname[16];
    std::uint64_t vmaddr;
    std::uint64_t vmsize;
    std::uint64_t fileoff;
    std::uint64_t filesize;
    std::int32_t maxprot;
    std::int32_t initprot;
    std::uint32_t nsects;
    std::uint32_t flags;
};

struct section_64 {
    char sectname[16];
    char segname[16];
    std::uint64_t addr;
    std::uint64_t size;
    std::uint32_t offset;
    std::uint32_t align;
    std::uint32_t reloff;
    std::uint32_t nreloc;
    std::uint32_t flags;
    std::uint32_t reserved1;
    std::uint32_t reserved2;
    std::uint32_t reserved3;
};

struct symtab_command {
    std::uint32_t cmd;
    std::uint32_t cmdsize;
    std::uint32_t symoff;
    std::uint32_t nsyms;
    std::uint32_t stroff;
    std::uint32_t strsize;
};

struct nlist_64 {
    std::uint32_t n_strx;
    std::uint8_t n_type;
    std::uint8_t n_sect;
    std::uint16_t n_desc;
    std::uint64_t n_value;
};

struct relocation_info {
    std::int32_t r_address;
    std::uint32_t r_symbolnum : 24,
                  r_pcrel : 1,
                  r_length : 2,
                  r_extern : 1,
                  r_type : 4;
};

#pragma pack(pop)

} // namespace macho

// Archive (static library) magic
constexpr char AR_MAGIC[] = "!<arch>\n";
constexpr std::size_t AR_MAGIC_LEN = 8;

namespace polyglot::linker {

// ============================================================================
// Utility Functions Implementation
// ============================================================================

ObjectFormat DetectObjectFormat(const std::vector<std::uint8_t> &data) {
    if (data.size() < 8) return ObjectFormat::kUnknown;
    
    // Check ELF magic
    if (data.size() >= 4 && 
        data[0] == 0x7f && data[1] == 'E' && data[2] == 'L' && data[3] == 'F') {
        return ObjectFormat::kELF;
    }
    
    // Check Mach-O magic (little-endian)
    std::uint32_t magic = 0;
    std::memcpy(&magic, data.data(), 4);
    if (magic == macho::MH_MAGIC_64 || magic == macho::MH_MAGIC ||
        magic == macho::MH_CIGAM_64 || magic == macho::MH_CIGAM ||
        magic == macho::FAT_MAGIC || magic == macho::FAT_CIGAM) {
        return ObjectFormat::kMachO;
    }
    
    // Check COFF/PE magic
    if (data.size() >= 2 && data[0] == 'M' && data[1] == 'Z') {
        return ObjectFormat::kCOFF;
    }
    
    // Check archive magic
    if (data.size() >= AR_MAGIC_LEN && 
        std::memcmp(data.data(), AR_MAGIC, AR_MAGIC_LEN) == 0) {
        return ObjectFormat::kArchive;
    }
    
    // Check LLVM bitcode magic
    if (data.size() >= 4 && data[0] == 'B' && data[1] == 'C') {
        return ObjectFormat::kLLVMBitcode;
    }
    
    return ObjectFormat::kUnknown;
}

ObjectFormat DetectObjectFormatFromPath(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return ObjectFormat::kUnknown;
    
    std::vector<std::uint8_t> header(64);
    file.read(reinterpret_cast<char*>(header.data()), header.size());
    header.resize(file.gcount());
    
    return DetectObjectFormat(header);
}

std::string DemangleSymbol(const std::string &name) {
    // Simple demangling - just return the name for now
    // A full implementation would use __cxa_demangle for C++ or equivalent
    if (name.empty()) return name;
    
    // Remove leading underscore (common on macOS)
    if (name[0] == '_' && name.size() > 1) {
        return name.substr(1);
    }
    
    return name;
}

bool SymbolMatchesPattern(const std::string &name, const std::string &pattern) {
    // Simple wildcard matching
    if (pattern == "*") return true;
    if (pattern.empty()) return name.empty();
    
    // Convert pattern to regex
    std::string regex_pattern;
    for (char c : pattern) {
        if (c == '*') {
            regex_pattern += ".*";
        } else if (c == '?') {
            regex_pattern += ".";
        } else if (std::strchr("[](){}+^$.|\\", c)) {
            regex_pattern += '\\';
            regex_pattern += c;
        } else {
            regex_pattern += c;
        }
    }
    
    try {
        std::regex re(regex_pattern);
        return std::regex_match(name, re);
    } catch (...) {
        return name == pattern;
    }
}

SectionType GetSectionTypeFromName(const std::string &name) {
    if (name == ".bss" || name == "__bss") return SectionType::kNobits;
    if (name == ".symtab") return SectionType::kSymtab;
    if (name == ".strtab" || name == ".shstrtab") return SectionType::kStrtab;
    if (name == ".dynamic") return SectionType::kDynamic;
    if (name == ".dynsym") return SectionType::kDynsym;
    if (name == ".rela" || name.find(".rela.") == 0) return SectionType::kRela;
    if (name == ".rel" || name.find(".rel.") == 0) return SectionType::kRel;
    if (name == ".hash") return SectionType::kHash;
    if (name == ".note" || name.find(".note.") == 0) return SectionType::kNote;
    if (name == ".init_array") return SectionType::kInitArray;
    if (name == ".fini_array") return SectionType::kFiniArray;
    return SectionType::kProgbits;
}

SectionFlags GetDefaultSectionFlags(const std::string &name) {
    if (name == ".text" || name == "__text") {
        return SectionFlags::kAlloc | SectionFlags::kExecInstr;
    }
    if (name == ".data" || name == "__data") {
        return SectionFlags::kAlloc | SectionFlags::kWrite;
    }
    if (name == ".rodata" || name == "__const") {
        return SectionFlags::kAlloc;
    }
    if (name == ".bss" || name == "__bss") {
        return SectionFlags::kAlloc | SectionFlags::kWrite;
    }
    return SectionFlags::kNone;
}

// ============================================================================
// ObjectFile Implementation
// ============================================================================

InputSection* ObjectFile::FindSection(const std::string &name) {
    for (auto &sec : sections) {
        if (sec.name == name) return &sec;
    }
    return nullptr;
}

const InputSection* ObjectFile::FindSection(const std::string &name) const {
    for (const auto &sec : sections) {
        if (sec.name == name) return &sec;
    }
    return nullptr;
}

Symbol* ObjectFile::FindSymbol(const std::string &name) {
    for (auto &sym : symbols) {
        if (sym.name == name) return &sym;
    }
    return nullptr;
}

const Symbol* ObjectFile::FindSymbol(const std::string &name) const {
    for (const auto &sym : symbols) {
        if (sym.name == name) return &sym;
    }
    return nullptr;
}

// ============================================================================
// Linker Implementation
// ============================================================================

Linker::Linker(const LinkerConfig &config) : config_(config) {}

void Linker::ReportError(const std::string &msg) {
    errors_.push_back(msg);
    if (config_.verbose) {
        std::cerr << "Error: " << msg << "\n";
    }
}

void Linker::ReportWarning(const std::string &msg) {
    warnings_.push_back(msg);
    if (config_.verbose) {
        std::cerr << "Warning: " << msg << "\n";
    }
}

void Linker::Trace(const std::string &msg) {
    if (config_.trace) {
        std::cout << "[trace] " << msg << "\n";
    }
}

std::uint64_t Linker::AlignTo(std::uint64_t value, std::uint64_t align) {
    if (align == 0) return value;
    return (value + align - 1) & ~(align - 1);
}

template<typename T>
bool Linker::ReadValue(std::ifstream &file, T &value) {
    file.read(reinterpret_cast<char*>(&value), sizeof(T));
    return file.good() || file.eof();
}

template<typename T>
void Linker::WriteValue(std::vector<std::uint8_t> &out, T value) {
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF));
    }
}

void Linker::WriteBytes(std::vector<std::uint8_t> &out, const void *data, std::size_t size) {
    const auto *bytes = static_cast<const std::uint8_t*>(data);
    out.insert(out.end(), bytes, bytes + size);
}

void Linker::WritePadding(std::vector<std::uint8_t> &out, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(0);
    }
}

// ============================================================================
// ELF Loading Implementation
// ============================================================================

bool Linker::LoadELF(const std::string &path, ObjectFile &obj) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        ReportError("Cannot open file: " + path);
        return false;
    }
    
    Trace("Loading ELF file: " + path);
    
    // Read ELF header
    Elf64_Ehdr ehdr;
    file.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
    
    // Verify ELF magic
    if (std::memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
        ReportError("Not an ELF file: " + path);
        return false;
    }
    
    // Check ELF class (32 or 64 bit)
    obj.elf_class = ehdr.e_ident[EI_CLASS];
    if (obj.elf_class != ELFCLASS64) {
        ReportError("Only 64-bit ELF files are supported: " + path);
        return false;
    }
    
    // Check endianness
    obj.endian = ehdr.e_ident[EI_DATA];
    obj.is_little_endian = (obj.endian == ELFDATA2LSB);
    obj.is_64bit = true;
    
    // Check file type
    if (ehdr.e_type != ET_REL) {
        ReportError("Not a relocatable object file: " + path);
        return false;
    }
    
    // Store machine type
    obj.machine = ehdr.e_machine;
    obj.format = ObjectFormat::kELF;
    
    // Parse sections
    if (!ParseELFSections(file, obj)) {
        return false;
    }
    
    // Parse symbols
    if (!ParseELFSymbols(file, obj)) {
        return false;
    }
    
    // Parse relocations
    if (!ParseELFRelocations(file, obj)) {
        return false;
    }
    
    return true;
}

bool Linker::ParseELFSections(std::ifstream &file, ObjectFile &obj) {
    // Re-read header for section info
    file.seekg(0);
    Elf64_Ehdr ehdr;
    file.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
    
    if (ehdr.e_shnum == 0) {
        return true;  // No sections
    }
    
    // Read section headers
    std::vector<Elf64_Shdr> shdrs(ehdr.e_shnum);
    file.seekg(ehdr.e_shoff);
    file.read(reinterpret_cast<char*>(shdrs.data()), ehdr.e_shnum * sizeof(Elf64_Shdr));
    
    if (!file.good() && !file.eof()) {
        ReportError("Failed to read section headers");
        return false;
    }
    
    // Read section header string table
    if (ehdr.e_shstrndx >= shdrs.size()) {
        ReportError("Invalid section header string table index");
        return false;
    }
    
    std::vector<char> shstrtab(shdrs[ehdr.e_shstrndx].sh_size);
    file.seekg(shdrs[ehdr.e_shstrndx].sh_offset);
    file.read(shstrtab.data(), shstrtab.size());
    
    // Load each section
    for (std::size_t i = 0; i < shdrs.size(); ++i) {
        const auto &shdr = shdrs[i];
        
        // Skip null section and special sections
        if (shdr.sh_type == SHT_NULL || shdr.sh_type == SHT_SYMTAB ||
            shdr.sh_type == SHT_STRTAB || shdr.sh_type == SHT_RELA ||
            shdr.sh_type == SHT_REL) {
            continue;
        }
        
        InputSection section;
        section.name = &shstrtab[shdr.sh_name];
        section.alignment = shdr.sh_addralign;
        section.section_index = static_cast<int>(i);
        section.object_file_index = static_cast<int>(objects_.size());
        section.original_addr = shdr.sh_addr;
        
        // Convert flags
        if (shdr.sh_flags & SHF_WRITE) {
            section.flags = section.flags | SectionFlags::kWrite;
        }
        if (shdr.sh_flags & SHF_ALLOC) {
            section.flags = section.flags | SectionFlags::kAlloc;
        }
        if (shdr.sh_flags & SHF_EXECINSTR) {
            section.flags = section.flags | SectionFlags::kExecInstr;
        }
        if (shdr.sh_flags & SHF_MERGE) {
            section.flags = section.flags | SectionFlags::kMerge;
        }
        if (shdr.sh_flags & SHF_STRINGS) {
            section.flags = section.flags | SectionFlags::kStrings;
        }
        if (shdr.sh_flags & SHF_TLS) {
            section.flags = section.flags | SectionFlags::kTLS;
        }
        
        // Convert type
        switch (shdr.sh_type) {
            case SHT_PROGBITS: section.type = SectionType::kProgbits; break;
            case SHT_NOBITS: section.type = SectionType::kNobits; break;
            case SHT_NOTE: section.type = SectionType::kNote; break;
            case SHT_INIT_ARRAY: section.type = SectionType::kInitArray; break;
            case SHT_FINI_ARRAY: section.type = SectionType::kFiniArray; break;
            default: section.type = SectionType::kProgbits; break;
        }
        
        // Read section data (skip for NOBITS)
        if (shdr.sh_type != SHT_NOBITS && shdr.sh_size > 0) {
            section.data.resize(shdr.sh_size);
            file.seekg(shdr.sh_offset);
            file.read(reinterpret_cast<char*>(section.data.data()), shdr.sh_size);
        } else if (shdr.sh_type == SHT_NOBITS) {
            // BSS section - allocate zeros
            section.data.resize(shdr.sh_size, 0);
        }
        
        obj.sections.push_back(std::move(section));
    }
    
    return true;
}

bool Linker::ParseELFSymbols(std::ifstream &file, ObjectFile &obj) {
    // Re-read header
    file.seekg(0);
    Elf64_Ehdr ehdr;
    file.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
    
    // Read section headers
    std::vector<Elf64_Shdr> shdrs(ehdr.e_shnum);
    file.seekg(ehdr.e_shoff);
    file.read(reinterpret_cast<char*>(shdrs.data()), ehdr.e_shnum * sizeof(Elf64_Shdr));
    
    // Read section header string table
    std::vector<char> shstrtab(shdrs[ehdr.e_shstrndx].sh_size);
    file.seekg(shdrs[ehdr.e_shstrndx].sh_offset);
    file.read(shstrtab.data(), shstrtab.size());
    
    // Find symbol table and string table
    for (std::size_t i = 0; i < shdrs.size(); ++i) {
        const auto &shdr = shdrs[i];
        if (shdr.sh_type != SHT_SYMTAB) continue;
        
        // Read symbol table
        std::size_t num_syms = shdr.sh_size / sizeof(Elf64_Sym);
        std::vector<Elf64_Sym> syms(num_syms);
        file.seekg(shdr.sh_offset);
        file.read(reinterpret_cast<char*>(syms.data()), shdr.sh_size);
        
        // Read associated string table
        const auto &strtab_shdr = shdrs[shdr.sh_link];
        std::vector<char> strtab(strtab_shdr.sh_size);
        file.seekg(strtab_shdr.sh_offset);
        file.read(strtab.data(), strtab.size());
        
        // Process each symbol
        for (std::size_t si = 0; si < syms.size(); ++si) {
            const auto &sym = syms[si];
            if (sym.st_name == 0) continue;  // Skip null symbol
            
            Symbol symbol;
            symbol.name = &strtab[sym.st_name];
            symbol.offset = sym.st_value;
            symbol.size = sym.st_size;
            symbol.object_file_index = static_cast<int>(objects_.size());
            
            // Binding
            switch (ELF64_ST_BIND(sym.st_info)) {
                case STB_LOCAL: symbol.binding = SymbolBinding::kLocal; break;
                case STB_GLOBAL: symbol.binding = SymbolBinding::kGlobal; break;
                case STB_WEAK: symbol.binding = SymbolBinding::kWeak; break;
                default: symbol.binding = SymbolBinding::kLocal; break;
            }
            
            // Type
            switch (ELF64_ST_TYPE(sym.st_info)) {
                case STT_NOTYPE: symbol.type = SymbolType::kNoType; break;
                case STT_OBJECT: symbol.type = SymbolType::kObject; break;
                case STT_FUNC: symbol.type = SymbolType::kFunction; break;
                case STT_SECTION: symbol.type = SymbolType::kSection; break;
                case STT_FILE: symbol.type = SymbolType::kFile; break;
                case STT_COMMON: symbol.type = SymbolType::kCommon; break;
                case STT_TLS: symbol.type = SymbolType::kTLS; break;
                default: symbol.type = SymbolType::kNoType; break;
            }
            
            // Visibility
            switch (ELF64_ST_VISIBILITY(sym.st_other)) {
                case STV_DEFAULT: symbol.visibility = SymbolVisibility::kDefault; break;
                case STV_INTERNAL: symbol.visibility = SymbolVisibility::kInternal; break;
                case STV_HIDDEN: symbol.visibility = SymbolVisibility::kHidden; break;
                case STV_PROTECTED: symbol.visibility = SymbolVisibility::kProtected; break;
                default: symbol.visibility = SymbolVisibility::kDefault; break;
            }
            
            // Section and definition status
            if (sym.st_shndx == SHN_UNDEF) {
                symbol.is_defined = false;
            } else if (sym.st_shndx == SHN_ABS) {
                symbol.is_defined = true;
                symbol.is_absolute = true;
            } else if (sym.st_shndx == SHN_COMMON) {
                symbol.is_defined = false;
                symbol.is_common = true;
            } else if (sym.st_shndx < shdrs.size()) {
                symbol.is_defined = true;
                symbol.section = &shstrtab[shdrs[sym.st_shndx].sh_name];
                symbol.section_index = sym.st_shndx;
            }
            
            obj.symbols.push_back(std::move(symbol));
        }
        
        break;  // Only process first symbol table
    }
    
    return true;
}

bool Linker::ParseELFRelocations(std::ifstream &file, ObjectFile &obj) {
    // Re-read header
    file.seekg(0);
    Elf64_Ehdr ehdr;
    file.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
    
    // Read section headers
    std::vector<Elf64_Shdr> shdrs(ehdr.e_shnum);
    file.seekg(ehdr.e_shoff);
    file.read(reinterpret_cast<char*>(shdrs.data()), ehdr.e_shnum * sizeof(Elf64_Shdr));
    
    // Read section header string table
    std::vector<char> shstrtab(shdrs[ehdr.e_shstrndx].sh_size);
    file.seekg(shdrs[ehdr.e_shstrndx].sh_offset);
    file.read(shstrtab.data(), shstrtab.size());
    
    // Find symbol table for symbol names
    std::vector<char> symstrtab;
    std::vector<Elf64_Sym> symtab;
    
    for (const auto &shdr : shdrs) {
        if (shdr.sh_type == SHT_SYMTAB) {
            // Read symbol table
            symtab.resize(shdr.sh_size / sizeof(Elf64_Sym));
            file.seekg(shdr.sh_offset);
            file.read(reinterpret_cast<char*>(symtab.data()), shdr.sh_size);
            
            // Read symbol string table
            const auto &strtab_shdr = shdrs[shdr.sh_link];
            symstrtab.resize(strtab_shdr.sh_size);
            file.seekg(strtab_shdr.sh_offset);
            file.read(symstrtab.data(), strtab_shdr.sh_size);
            break;
        }
    }
    
    // Process relocation sections
    for (std::size_t i = 0; i < shdrs.size(); ++i) {
        const auto &shdr = shdrs[i];
        if (shdr.sh_type != SHT_RELA && shdr.sh_type != SHT_REL) continue;
        
        // Get target section name
        std::string target_section;
        if (shdr.sh_info < shdrs.size()) {
            target_section = &shstrtab[shdrs[shdr.sh_info].sh_name];
        }
        
        // Read relocations
        if (shdr.sh_type == SHT_RELA) {
            std::size_t num_relas = shdr.sh_size / sizeof(Elf64_Rela);
            std::vector<Elf64_Rela> relas(num_relas);
            file.seekg(shdr.sh_offset);
            file.read(reinterpret_cast<char*>(relas.data()), shdr.sh_size);
            
            for (const auto &rela : relas) {
                Relocation reloc;
                reloc.offset = rela.r_offset;
                reloc.addend = rela.r_addend;
                reloc.type = static_cast<std::uint32_t>(ELF64_R_TYPE(rela.r_info));
                reloc.section = target_section;
                
                // Get symbol name
                std::uint32_t sym_idx = ELF64_R_SYM(rela.r_info);
                if (sym_idx < symtab.size() && symtab[sym_idx].st_name < symstrtab.size()) {
                    reloc.symbol = &symstrtab[symtab[sym_idx].st_name];
                }
                reloc.symbol_index = sym_idx;
                
                // Determine if PC-relative based on relocation type
                std::uint32_t rtype = reloc.type;
                reloc.is_pc_relative = (rtype == 2 || rtype == 4 || rtype == 9 ||
                                        rtype == 13 || rtype == 15 || rtype == 24);
                
                // Determine relocation size
                if (rtype == 1 || rtype == 24 || rtype == 25) {
                    reloc.size = 8;  // 64-bit
                } else if (rtype == 2 || rtype == 3 || rtype == 4 || rtype == 9 ||
                           rtype == 10 || rtype == 11 || rtype == 26) {
                    reloc.size = 4;  // 32-bit
                } else if (rtype == 12 || rtype == 13) {
                    reloc.size = 2;  // 16-bit
                } else if (rtype == 14 || rtype == 15) {
                    reloc.size = 1;  // 8-bit
                } else {
                    reloc.size = 4;  // Default
                }
                
                obj.relocations.push_back(std::move(reloc));
            }
        }
    }
    
    return true;
}

// ============================================================================
// Mach-O Loading Implementation
// ============================================================================

bool Linker::LoadMachO(const std::string &path, ObjectFile &obj) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        ReportError("Cannot open file: " + path);
        return false;
    }
    
    Trace("Loading Mach-O file: " + path);
    
    // Read Mach-O header
    macho::mach_header_64 header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    
    // Verify magic number
    if (header.magic != macho::MH_MAGIC_64 && header.magic != macho::MH_CIGAM_64) {
        // Check for 32-bit or fat binary
        if (header.magic == macho::MH_MAGIC || header.magic == macho::MH_CIGAM) {
            ReportError("32-bit Mach-O files are not supported: " + path);
            return false;
        }
        if (header.magic == macho::FAT_MAGIC || header.magic == macho::FAT_CIGAM) {
            ReportError("Universal (fat) binaries are not supported: " + path);
            return false;
        }
        ReportError("Not a Mach-O file: " + path);
        return false;
    }
    
    // Handle byte swapping for big-endian
    bool swap_bytes = (header.magic == macho::MH_CIGAM_64);
    if (swap_bytes) {
        ReportWarning("Big-endian Mach-O files may have issues");
    }
    
    // Store Mach-O info
    obj.format = ObjectFormat::kMachO;
    obj.cpu_type = header.cputype;
    obj.cpu_subtype = header.cpusubtype;
    obj.filetype = header.filetype;
    obj.is_64bit = true;
    obj.is_little_endian = !swap_bytes;
    
    // Check file type
    if (header.filetype != macho::MH_OBJECT) {
        ReportError("Not a relocatable object file: " + path);
        return false;
    }
    
    // Parse load commands
    if (!ParseMachOLoadCommands(file, obj)) {
        return false;
    }
    
    return true;
}

bool Linker::ParseMachOLoadCommands(std::ifstream &file, ObjectFile &obj) {
    // Re-read header
    file.seekg(0);
    macho::mach_header_64 header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    
    // Process each load command
    std::uint64_t offset = sizeof(macho::mach_header_64);
    
    for (std::uint32_t i = 0; i < header.ncmds; ++i) {
        file.seekg(offset);
        
        macho::load_command lc;
        file.read(reinterpret_cast<char*>(&lc), sizeof(lc));
        
        if (lc.cmd == macho::LC_SEGMENT_64) {
            // Parse segment and sections
            if (!ParseMachOSegments(file, obj, offset, lc.cmdsize)) {
                return false;
            }
        } else if (lc.cmd == macho::LC_SYMTAB) {
            // Parse symbol table
            file.seekg(offset);
            macho::symtab_command symtab;
            file.read(reinterpret_cast<char*>(&symtab), sizeof(symtab));
            
            if (!ParseMachOSymtab(file, obj, symtab.symoff, symtab.nsyms,
                                  symtab.stroff, symtab.strsize)) {
                return false;
            }
        }
        
        offset += lc.cmdsize;
    }
    
    return true;
}

bool Linker::ParseMachOSegments(std::ifstream &file, ObjectFile &obj,
                                 std::uint64_t offset, std::uint32_t /*size*/) {
    file.seekg(offset);
    
    macho::segment_command_64 seg;
    file.read(reinterpret_cast<char*>(&seg), sizeof(seg));
    
    std::string segment_name(seg.segname, strnlen(seg.segname, 16));
    
    // Read sections
    for (std::uint32_t i = 0; i < seg.nsects; ++i) {
        macho::section_64 sect;
        file.read(reinterpret_cast<char*>(&sect), sizeof(sect));
        
        InputSection section;
        section.name = std::string(sect.sectname, strnlen(sect.sectname, 16));
        section.segment = segment_name;
        section.alignment = 1ULL << sect.align;
        section.section_index = static_cast<int>(obj.sections.size());
        section.object_file_index = static_cast<int>(objects_.size());
        section.original_addr = sect.addr;
        
        // Convert flags
        std::uint32_t section_type = sect.flags & 0xFF;
        if (segment_name == "__TEXT") {
            section.flags = SectionFlags::kAlloc | SectionFlags::kExecInstr;
        } else if (segment_name == "__DATA") {
            section.flags = SectionFlags::kAlloc | SectionFlags::kWrite;
        } else {
            section.flags = SectionFlags::kAlloc;
        }
        
        // Set section type
        if (section_type == macho::S_ZEROFILL) {
            section.type = SectionType::kNobits;
        } else {
            section.type = SectionType::kProgbits;
        }
        
        // Read section data
        if (section_type != macho::S_ZEROFILL && sect.size > 0) {
            section.data.resize(sect.size);
            std::streampos current = file.tellg();
            file.seekg(sect.offset);
            file.read(reinterpret_cast<char*>(section.data.data()), sect.size);
            file.seekg(current);
        }
        
        // Parse relocations for this section
        if (sect.nreloc > 0) {
            if (!ParseMachORelocations(file, obj, section, sect.reloff, sect.nreloc)) {
                return false;
            }
        }
        
        obj.sections.push_back(std::move(section));
    }
    
    return true;
}

bool Linker::ParseMachOSymtab(std::ifstream &file, ObjectFile &obj,
                               std::uint32_t symoff, std::uint32_t nsyms,
                               std::uint32_t stroff, std::uint32_t strsize) {
    // Read string table
    std::vector<char> strtab(strsize);
    file.seekg(stroff);
    file.read(strtab.data(), strsize);
    
    // Read and process symbols
    file.seekg(symoff);
    
    for (std::uint32_t i = 0; i < nsyms; ++i) {
        macho::nlist_64 nlist;
        file.read(reinterpret_cast<char*>(&nlist), sizeof(nlist));
        
        if (nlist.n_strx >= strsize) continue;
        
        Symbol symbol;
        symbol.name = &strtab[nlist.n_strx];
        symbol.offset = nlist.n_value;
        symbol.object_file_index = static_cast<int>(objects_.size());
        
        // Parse n_type
        std::uint8_t n_type = nlist.n_type & 0x0E;  // Mask out N_EXT and N_PEXT
        bool is_external = (nlist.n_type & macho::N_EXT) != 0;
        
        // Set binding
        if (is_external) {
            symbol.binding = SymbolBinding::kGlobal;
        } else {
            symbol.binding = SymbolBinding::kLocal;
        }
        
        // Set type and definition status
        if (n_type == macho::N_UNDF) {
            symbol.is_defined = false;
            symbol.type = SymbolType::kNoType;
        } else if (n_type == macho::N_ABS) {
            symbol.is_defined = true;
            symbol.is_absolute = true;
            symbol.type = SymbolType::kObject;
        } else if (n_type == macho::N_SECT) {
            symbol.is_defined = true;
            symbol.section_index = nlist.n_sect - 1;  // Mach-O sections are 1-indexed
            
            // Get section name if valid
            if (nlist.n_sect > 0 && static_cast<std::size_t>(nlist.n_sect - 1) < obj.sections.size()) {
                symbol.section = obj.sections[nlist.n_sect - 1].name;
            }
            
            // Guess type from section name
            if (symbol.section == "__text" || symbol.section.find("text") != std::string::npos) {
                symbol.type = SymbolType::kFunction;
            } else {
                symbol.type = SymbolType::kObject;
            }
        } else if (n_type == macho::N_INDR) {
            symbol.is_defined = false;
            symbol.type = SymbolType::kNoType;
        }
        
        obj.symbols.push_back(std::move(symbol));
    }
    
    return true;
}

bool Linker::ParseMachORelocations(std::ifstream &file, ObjectFile &obj, InputSection &section,
                                    std::uint32_t reloff, std::uint32_t nreloc) {
    std::streampos current = file.tellg();
    file.seekg(reloff);
    
    for (std::uint32_t i = 0; i < nreloc; ++i) {
        macho::relocation_info info;
        file.read(reinterpret_cast<char*>(&info), sizeof(info));
        
        Relocation reloc;
        reloc.offset = info.r_address;
        reloc.type = info.r_type;
        reloc.is_pc_relative = info.r_pcrel != 0;
        reloc.size = 1 << info.r_length;
        reloc.section = section.name;
        
        // Get symbol name if external
        if (info.r_extern) {
            reloc.symbol_index = info.r_symbolnum;
            if (reloc.symbol_index < static_cast<int>(obj.symbols.size())) {
                reloc.symbol = obj.symbols[reloc.symbol_index].name;
            }
        } else {
            // Section relocation - symbol_index is section number
            reloc.symbol_index = info.r_symbolnum;
        }
        
        section.relocations.push_back(std::move(reloc));
        obj.relocations.push_back(section.relocations.back());
    }
    
    file.seekg(current);
    return true;
}

// ============================================================================
// COFF Loading Implementation
// ============================================================================

bool Linker::LoadCOFF(const std::string &path, ObjectFile &obj) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        ReportError("Cannot open file: " + path);
        return false;
    }

    Trace("Loading COFF file: " + path);

    // COFF header is 20 bytes. If the file starts with 'MZ', it is a PE
    // executable; skip the DOS stub and locate the PE signature.
    std::uint16_t magic{0};
    ReadValue(file, magic);
    std::streamoff coff_offset = 0;
    if (magic == 0x5A4D) {  // 'MZ'
        // PE file — read PE offset at 0x3C
        file.seekg(0x3C);
        std::uint32_t pe_off{0};
        ReadValue(file, pe_off);
        file.seekg(pe_off);
        std::uint32_t pe_sig{0};
        ReadValue(file, pe_sig);
        if (pe_sig != 0x00004550) {  // 'PE\0\0'
            ReportError("Invalid PE signature in: " + path);
            return false;
        }
        coff_offset = static_cast<std::streamoff>(pe_off + 4);
    } else {
        // Raw COFF object file — machine type is the first two bytes
        coff_offset = 0;
    }

    file.seekg(coff_offset);

    // COFF File Header (20 bytes)
    struct CoffHeader {
        std::uint16_t machine;
        std::uint16_t number_of_sections;
        std::uint32_t timestamp;
        std::uint32_t symbol_table_offset;
        std::uint32_t number_of_symbols;
        std::uint16_t optional_header_size;
        std::uint16_t characteristics;
    } hdr{};

    file.read(reinterpret_cast<char *>(&hdr), sizeof(hdr));
    if (!file) {
        ReportError("Failed to read COFF header: " + path);
        return false;
    }

    obj.format = ObjectFormat::kCOFF;
    obj.machine = hdr.machine;
    obj.is_64bit = (hdr.machine == 0x8664);  // IMAGE_FILE_MACHINE_AMD64
    obj.is_little_endian = true;

    // Skip optional header if present
    if (hdr.optional_header_size > 0) {
        file.seekg(hdr.optional_header_size, std::ios::cur);
    }

    // Parse section headers (40 bytes each)
    struct CoffSectionHeader {
        char name[8];
        std::uint32_t virtual_size;
        std::uint32_t virtual_address;
        std::uint32_t raw_data_size;
        std::uint32_t raw_data_offset;
        std::uint32_t relocation_offset;
        std::uint32_t line_numbers_offset;
        std::uint16_t number_of_relocations;
        std::uint16_t number_of_line_numbers;
        std::uint32_t characteristics;
    };

    std::vector<CoffSectionHeader> section_headers(hdr.number_of_sections);
    for (std::uint16_t i = 0; i < hdr.number_of_sections; ++i) {
        file.read(reinterpret_cast<char *>(&section_headers[i]), sizeof(CoffSectionHeader));
        if (!file) {
            ReportError("Failed to read COFF section header: " + path);
            return false;
        }
    }

    // Read string table (comes after symbol table)
    std::string string_table;
    if (hdr.symbol_table_offset > 0) {
        std::streamoff strtab_off = static_cast<std::streamoff>(
            hdr.symbol_table_offset + hdr.number_of_symbols * 18);
        file.seekg(strtab_off);
        std::uint32_t strtab_size{0};
        ReadValue(file, strtab_size);
        if (strtab_size > 4) {
            string_table.resize(strtab_size);
            std::memcpy(&string_table[0], &strtab_size, 4);
            file.read(&string_table[4], strtab_size - 4);
        }
    }

    auto coff_section_name = [&](const CoffSectionHeader &sh) -> std::string {
        if (sh.name[0] == '/') {
            // Long name — index into string table
            int offset = std::atoi(sh.name + 1);
            if (offset > 0 && offset < static_cast<int>(string_table.size())) {
                return std::string(&string_table[offset]);
            }
        }
        // Short name (up to 8 chars, null-padded)
        return std::string(sh.name, strnlen(sh.name, 8));
    };

    // Load sections
    for (std::uint16_t i = 0; i < hdr.number_of_sections; ++i) {
        auto &sh = section_headers[i];
        InputSection isec;
        isec.name = coff_section_name(sh);
        isec.section_index = static_cast<int>(i + 1);  // COFF sections are 1-based
        isec.alignment = 1;
        isec.original_addr = sh.virtual_address;

        // Determine alignment from characteristics (bits 20-23)
        std::uint32_t align_bits = (sh.characteristics >> 20) & 0xF;
        if (align_bits > 0 && align_bits <= 14) {
            isec.alignment = 1u << (align_bits - 1);
        }

        // Flags
        constexpr std::uint32_t IMAGE_SCN_CNT_CODE = 0x00000020;
        constexpr std::uint32_t IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040;
        constexpr std::uint32_t IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080;
        constexpr std::uint32_t IMAGE_SCN_MEM_EXECUTE = 0x20000000;
        constexpr std::uint32_t IMAGE_SCN_MEM_READ = 0x40000000;
        constexpr std::uint32_t IMAGE_SCN_MEM_WRITE = 0x80000000;

        SectionFlags flags = SectionFlags::kAlloc;
        if (sh.characteristics & IMAGE_SCN_MEM_WRITE)
            flags = flags | SectionFlags::kWrite;
        if (sh.characteristics & (IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE))
            flags = flags | SectionFlags::kExecInstr;
        if (sh.characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA) {
            isec.type = SectionType::kNobits;
        }
        isec.flags = flags;

        // Read raw data
        if (sh.raw_data_size > 0 && sh.raw_data_offset > 0) {
            isec.data.resize(sh.raw_data_size);
            auto saved = file.tellg();
            file.seekg(sh.raw_data_offset);
            file.read(reinterpret_cast<char *>(isec.data.data()), sh.raw_data_size);
            file.seekg(saved);
        }

        // Parse relocations for this section
        if (sh.number_of_relocations > 0 && sh.relocation_offset > 0) {
            auto saved = file.tellg();
            file.seekg(sh.relocation_offset);
            for (std::uint16_t r = 0; r < sh.number_of_relocations; ++r) {
                struct CoffReloc {
                    std::uint32_t virtual_address;
                    std::uint32_t symbol_index;
                    std::uint16_t type;
                } cr{};
                file.read(reinterpret_cast<char *>(&cr), sizeof(cr));

                Relocation reloc;
                reloc.offset = cr.virtual_address;
                reloc.type = cr.type;
                reloc.symbol_index = static_cast<int>(cr.symbol_index);
                reloc.section = isec.name;
                // x86_64 COFF relocation types
                // IMAGE_REL_AMD64_ADDR64=1, REL32=4, etc.
                reloc.is_pc_relative = (cr.type == 4);
                reloc.size = (cr.type == 1) ? 8 : 4;
                isec.relocations.push_back(reloc);
                obj.relocations.push_back(reloc);
            }
            file.seekg(saved);
        }

        obj.sections.push_back(std::move(isec));
    }

    // Parse symbol table (18 bytes per entry)
    if (hdr.symbol_table_offset > 0) {
        file.seekg(hdr.symbol_table_offset);
        std::uint32_t i = 0;
        while (i < hdr.number_of_symbols) {
            struct CoffSymbol {
                union {
                    char short_name[8];
                    struct { std::uint32_t zeroes; std::uint32_t offset; } long_name;
                } name;
                std::uint32_t value;
                std::int16_t section_number;
                std::uint16_t type;
                std::uint8_t storage_class;
                std::uint8_t aux_count;
            } cs{};
            file.read(reinterpret_cast<char *>(&cs), 18);

            Symbol sym;
            // Resolve name
            if (cs.name.long_name.zeroes == 0) {
                std::uint32_t off = cs.name.long_name.offset;
                if (off < string_table.size()) {
                    sym.name = std::string(&string_table[off]);
                }
            } else {
                sym.name = std::string(cs.name.short_name,
                                       strnlen(cs.name.short_name, 8));
            }

            sym.offset = cs.value;
            sym.section_index = cs.section_number - 1;  // Make 0-based
            sym.is_defined = (cs.section_number > 0);

            // Storage class mapping
            constexpr std::uint8_t IMAGE_SYM_CLASS_EXTERNAL = 2;
            constexpr std::uint8_t IMAGE_SYM_CLASS_STATIC = 3;
            constexpr std::uint8_t IMAGE_SYM_CLASS_LABEL = 6;
            constexpr std::uint8_t IMAGE_SYM_CLASS_WEAK_EXTERNAL = 105;

            if (cs.storage_class == IMAGE_SYM_CLASS_EXTERNAL) {
                sym.binding = SymbolBinding::kGlobal;
            } else if (cs.storage_class == IMAGE_SYM_CLASS_WEAK_EXTERNAL) {
                sym.binding = SymbolBinding::kWeak;
            } else {
                sym.binding = SymbolBinding::kLocal;
            }

            // Symbol type: bit 5 indicates function
            if ((cs.type >> 4) == 0x20 || (cs.type & 0x20)) {
                sym.type = SymbolType::kFunction;
            } else {
                sym.type = SymbolType::kObject;
            }

            // Assign section name
            if (sym.is_defined && sym.section_index >= 0 &&
                sym.section_index < static_cast<int>(obj.sections.size())) {
                sym.section = obj.sections[sym.section_index].name;
            }

            // Resolve symbol addresses relative to relocations
            sym.object_file_index = static_cast<int>(objects_.size());
            obj.symbols.push_back(std::move(sym));

            // Skip auxiliary symbol entries
            for (std::uint8_t a = 0; a < cs.aux_count; ++a) {
                char aux[18];
                file.read(aux, 18);
            }
            i += 1 + cs.aux_count;
        }

        // Backpatch relocation symbol names
        for (auto &sec : obj.sections) {
            for (auto &r : sec.relocations) {
                if (r.symbol_index >= 0 &&
                    r.symbol_index < static_cast<int>(obj.symbols.size())) {
                    r.symbol = obj.symbols[r.symbol_index].name;
                }
            }
        }
        for (auto &r : obj.relocations) {
            if (r.symbol_index >= 0 &&
                r.symbol_index < static_cast<int>(obj.symbols.size())) {
                r.symbol = obj.symbols[r.symbol_index].name;
            }
        }
    }

    Trace("COFF loaded: " + std::to_string(obj.sections.size()) + " sections, " +
          std::to_string(obj.symbols.size()) + " symbols");
    return true;
}

// ============================================================================
// Archive Loading Implementation
// ============================================================================

bool Linker::LoadArchive(const std::string &path, Archive &archive) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        ReportError("Cannot open archive: " + path);
        return false;
    }
    
    Trace("Loading archive: " + path);
    
    // Check magic
    char magic[AR_MAGIC_LEN];
    file.read(magic, AR_MAGIC_LEN);
    if (std::memcmp(magic, AR_MAGIC, AR_MAGIC_LEN) != 0) {
        ReportError("Not an archive file: " + path);
        return false;
    }
    
    archive.path = path;
    
    // Parse archive symbol table
    if (!ParseArchiveSymbolTable(file, archive)) {
        return false;
    }
    
    // Parse archive members
    if (!ParseArchiveMembers(file, archive)) {
        return false;
    }
    
    return true;
}

bool Linker::ParseArchiveSymbolTable(std::ifstream &file, Archive &archive) {
    file.seekg(AR_MAGIC_LEN);
    
    char header[60];
    if (!file.read(header, 60)) {
        return true;  // Empty archive
    }
    
    // Check for symbol table (name is "/" or "/SYM64/" or "__.SYMDEF")
    std::string name(header, 16);
    name = name.substr(0, name.find(' '));
    
    if (name == "/" || name == "/SYM64/") {
        // GNU-style symbol table
        std::string size_str(header + 48, 10);
        std::uint64_t size = std::stoull(size_str);
        
        // Read number of symbols (big-endian!)
        std::uint32_t nsyms;
        file.read(reinterpret_cast<char*>(&nsyms), 4);
        // Swap bytes for big-endian
        nsyms = ((nsyms >> 24) & 0xFF) | ((nsyms >> 8) & 0xFF00) |
                ((nsyms << 8) & 0xFF0000) | ((nsyms << 24) & 0xFF000000);
        
        // Read offsets (big-endian)
        std::vector<std::uint32_t> offsets(nsyms);
        file.read(reinterpret_cast<char*>(offsets.data()), nsyms * 4);
        
        // Swap each offset
        for (auto &off : offsets) {
            off = ((off >> 24) & 0xFF) | ((off >> 8) & 0xFF00) |
                  ((off << 8) & 0xFF0000) | ((off << 24) & 0xFF000000);
        }
        
        // Read symbol names (null-terminated strings)
        std::uint64_t string_size = size - 4 - nsyms * 4;
        std::vector<char> strings(string_size);
        file.read(strings.data(), string_size);
        
        // Parse symbol names
        std::size_t str_offset = 0;
        for (std::uint32_t i = 0; i < nsyms && str_offset < strings.size(); ++i) {
            ArchiveSymbol sym;
            sym.name = &strings[str_offset];
            sym.member_offset = offsets[i];
            archive.symbols.push_back(std::move(sym));
            
            // Find next string
            while (str_offset < strings.size() && strings[str_offset] != '\0') {
                ++str_offset;
            }
            ++str_offset;  // Skip null terminator
        }
    } else if (name.find("__.SYMDEF") == 0) {
        // BSD-style symbol table (macOS)
        std::string size_str(header + 48, 10);
        std::uint64_t size = std::stoull(size_str);
        (void)size;  // Size used for validation
        
        // Read ranlib structures
        std::uint32_t ranlib_size;
        file.read(reinterpret_cast<char*>(&ranlib_size), 4);
        
        std::uint32_t num_entries = ranlib_size / 8;  // Each entry is 8 bytes
        
        std::vector<std::pair<std::uint32_t, std::uint32_t>> entries(num_entries);
        for (auto &entry : entries) {
            file.read(reinterpret_cast<char*>(&entry.first), 4);   // string offset
            file.read(reinterpret_cast<char*>(&entry.second), 4);  // member offset
        }
        
        // Read string table size and strings
        std::uint32_t strtab_size;
        file.read(reinterpret_cast<char*>(&strtab_size), 4);
        
        std::vector<char> strings(strtab_size);
        file.read(strings.data(), strtab_size);
        
        // Build symbol entries
        for (const auto &entry : entries) {
            if (entry.first < strings.size()) {
                ArchiveSymbol sym;
                sym.name = &strings[entry.first];
                sym.member_offset = entry.second;
                archive.symbols.push_back(std::move(sym));
            }
        }
    }
    
    return true;
}

bool Linker::ParseArchiveMembers(std::ifstream &file, Archive &archive) {
    file.seekg(AR_MAGIC_LEN);
    
    // Extended name table (for long names)
    std::string extended_names;
    
    while (file.good() && !file.eof()) {
        // Align to even boundary
        if (file.tellg() % 2 == 1) {
            file.seekg(1, std::ios::cur);
        }
        
        std::uint64_t member_offset = file.tellg();
        
        char header[60];
        if (!file.read(header, 60)) {
            break;
        }
        
        // Check end marker
        if (header[58] != '`' || header[59] != '\n') {
            break;
        }
        
        // Parse header
        std::string name(header, 16);
        name = name.substr(0, name.find(' '));
        
        std::string size_str(header + 48, 10);
        std::uint64_t size = 0;
        try {
            size = std::stoull(size_str);
        } catch (...) {
            break;
        }
        
        // Handle special members
        if (name == "/" || name == "/SYM64/") {
            // Symbol table - skip
            file.seekg(size, std::ios::cur);
            continue;
        }
        
        if (name == "//" || name == "ARFILENAMES/") {
            // Extended names table
            extended_names.resize(size);
            file.read(extended_names.data(), size);
            continue;
        }
        
        if (name.find("__.SYMDEF") == 0) {
            // BSD symbol table - skip
            file.seekg(size, std::ios::cur);
            continue;
        }
        
        // Regular member
        ArchiveMember member;
        member.offset = member_offset;
        member.size = size;
        
        // Handle long names
        if (name[0] == '/' && name.size() > 1) {
            // GNU long name: /offset
            std::uint64_t name_offset = std::stoull(name.substr(1));
            if (name_offset < extended_names.size()) {
                std::size_t end = extended_names.find('/', name_offset);
                if (end == std::string::npos) {
                    end = extended_names.find('\n', name_offset);
                }
                member.name = extended_names.substr(name_offset, end - name_offset);
            }
        } else if (name[0] == '#' && name[1] == '1' && name[2] == '/') {
            // BSD long name: #1/length
            std::uint32_t name_len = std::stoul(name.substr(3));
            member.name.resize(name_len);
            file.read(member.name.data(), name_len);
            // Remove trailing nulls
            while (!member.name.empty() && member.name.back() == '\0') {
                member.name.pop_back();
            }
            size -= name_len;
        } else {
            // Regular name (strip trailing /)
            if (!name.empty() && name.back() == '/') {
                name.pop_back();
            }
            member.name = name;
        }
        
        // Read member data
        member.data.resize(size);
        file.read(reinterpret_cast<char*>(member.data.data()), size);
        
        archive.members.push_back(std::move(member));
    }
    
    return true;
}

bool Linker::LoadArchiveMember(const Archive &archive, const ArchiveMember &member) {
    // Detect format and load
    ObjectFormat format = DetectObjectFormat(member.data);
    
    ObjectFile obj;
    obj.path = archive.path + "(" + member.name + ")";
    
    // For simplicity, we write to temp file and load
    std::string temp_path = "/tmp/polyld_" + std::to_string(reinterpret_cast<std::uintptr_t>(&member)) + ".o";
    {
        std::ofstream temp(temp_path, std::ios::binary);
        temp.write(reinterpret_cast<const char*>(member.data.data()), member.data.size());
    }
    
    bool result = false;
    if (format == ObjectFormat::kELF) {
        result = LoadELF(temp_path, obj);
    } else if (format == ObjectFormat::kMachO) {
        result = LoadMachO(temp_path, obj);
    }
    
    // Clean up temp file
    std::remove(temp_path.c_str());
    
    if (result) {
        obj.path = archive.path + "(" + member.name + ")";
        objects_.push_back(std::move(obj));
        stats_.objects_loaded++;
    }
    
    return result;
}

// ============================================================================
// Object File Loading
// ============================================================================

bool Linker::LoadObjectFiles() {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (const auto &path : config_.input_files) {
        Trace("Processing input file: " + path);
        
        ObjectFormat format = DetectObjectFormatFromPath(path);
        
        if (format == ObjectFormat::kArchive) {
            Archive archive;
            if (!LoadArchive(path, archive)) {
                return false;
            }
            archives_.push_back(std::move(archive));
            stats_.archives_loaded++;
        } else {
            ObjectFile obj;
            obj.path = path;
            
            bool loaded = false;
            if (format == ObjectFormat::kELF) {
                loaded = LoadELF(path, obj);
            } else if (format == ObjectFormat::kMachO) {
                loaded = LoadMachO(path, obj);
            } else if (format == ObjectFormat::kCOFF) {
                loaded = LoadCOFF(path, obj);
            } else {
                // Try ELF first, then Mach-O
                if (LoadELF(path, obj)) {
                    loaded = true;
                } else {
                    errors_.clear();  // Clear ELF errors
                    loaded = LoadMachO(path, obj);
                }
            }
            
            if (!loaded) {
                ReportError("Failed to load object file: " + path);
                return false;
            }
            
            objects_.push_back(std::move(obj));
            stats_.objects_loaded++;
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    Trace("Loaded " + std::to_string(stats_.objects_loaded) + " objects and " +
          std::to_string(stats_.archives_loaded) + " archives in " +
          std::to_string(duration.count()) + "ms");
    
    return true;
}

bool Linker::LoadArchives() {
    // Archives are loaded on-demand during symbol resolution
    return true;
}

// ============================================================================
// Symbol Resolution
// ============================================================================

bool Linker::AddSymbol(const Symbol &symbol) {
    auto it = symbol_table_.find(symbol.name);
    if (it != symbol_table_.end()) {
        // Handle multiple definitions
        if (it->second.is_defined && symbol.is_defined) {
            if (it->second.is_weak() && !symbol.is_weak()) {
                // Strong overrides weak
                it->second = symbol;
            } else if (!it->second.is_weak() && symbol.is_weak()) {
                // Keep existing strong
            } else if (it->second.is_weak() && symbol.is_weak()) {
                // Keep first weak
            } else if (!config_.allow_multiple_definition) {
                ReportError("Multiple definition of symbol: " + symbol.name);
                return false;
            }
        } else if (symbol.is_defined && !it->second.is_defined) {
            // Replace undefined with defined
            it->second = symbol;
        }
    } else {
        symbol_table_[symbol.name] = symbol;
    }
    
    return true;
}

Symbol* Linker::LookupSymbol(const std::string &name) {
    auto it = symbol_table_.find(name);
    return (it != symbol_table_.end()) ? &it->second : nullptr;
}

const Symbol* Linker::LookupSymbol(const std::string &name) const {
    auto it = symbol_table_.find(name);
    return (it != symbol_table_.end()) ? &it->second : nullptr;
}

std::vector<const Symbol*> Linker::GetUndefinedSymbols() const {
    std::vector<const Symbol*> result;
    for (const auto &pair : symbol_table_) {
        if (!pair.second.is_defined) {
            result.push_back(&pair.second);
        }
    }
    return result;
}

std::vector<const Symbol*> Linker::GetExportedSymbols() const {
    std::vector<const Symbol*> result;
    for (const auto &pair : symbol_table_) {
        if (pair.second.is_defined && pair.second.is_global() &&
            pair.second.visibility == SymbolVisibility::kDefault) {
            result.push_back(&pair.second);
        }
    }
    return result;
}

bool Linker::ResolveSymbols() {
    Trace("Resolving symbols...");
    
    // Build initial symbol table from all objects
    if (!ResolveSymbolReferences()) {
        return false;
    }
    
    // Resolve undefined symbols from archives
    if (!ResolveFromArchives()) {
        return false;
    }
    
    // Merge common symbols
    MergeCommonSymbols();
    
    // Check for undefined symbols
    if (!CheckUndefinedSymbols()) {
        return false;
    }
    
    Trace("Symbol resolution complete: " + std::to_string(stats_.symbols_defined) +
          " defined, " + std::to_string(stats_.symbols_undefined) + " undefined");
    
    return true;
}

bool Linker::ResolveSymbolReferences() {
    // First pass: add all defined symbols
    for (const auto &obj : objects_) {
        for (const auto &sym : obj.symbols) {
            if (sym.is_defined && (sym.is_global() || sym.binding == SymbolBinding::kWeak)) {
                if (!AddSymbol(sym)) {
                    return false;
                }
                stats_.symbols_defined++;
            }
        }
    }
    
    // Second pass: add undefined symbol references
    for (const auto &obj : objects_) {
        for (const auto &sym : obj.symbols) {
            if (!sym.is_defined && sym.is_global()) {
                auto it = symbol_table_.find(sym.name);
                if (it == symbol_table_.end()) {
                    // Add undefined symbol
                    symbol_table_[sym.name] = sym;
                }
            }
        }
    }
    
    return true;
}

bool Linker::ResolveFromArchives() {
    // Iterate until no more symbols are resolved
    bool changed = true;
    
    while (changed) {
        changed = false;
        
        for (auto &archive : archives_) {
            // Check each undefined symbol against archive symbol table
            for (const auto &arsym : archive.symbols) {
                auto it = symbol_table_.find(arsym.name);
                if (it != symbol_table_.end() && !it->second.is_defined) {
                    // Find and load the member that defines this symbol
                    for (const auto &member : archive.members) {
                        if (member.offset == arsym.member_offset ||
                            (member.offset <= arsym.member_offset &&
                             arsym.member_offset < member.offset + member.size + 60)) {
                            Trace("Loading " + member.name + " from " + archive.path +
                                  " for symbol " + arsym.name);
                            
                            if (LoadArchiveMember(archive, member)) {
                                // Re-resolve symbols with new object
                                const auto &new_obj = objects_.back();
                                for (const auto &sym : new_obj.symbols) {
                                    if (sym.is_defined && sym.is_global()) {
                                        AddSymbol(sym);
                                        stats_.symbols_defined++;
                                    }
                                }
                                changed = true;
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
    
    return true;
}

void Linker::MergeCommonSymbols() {
    // Common symbols are uninitialized data that can be merged
    for (auto &pair : symbol_table_) {
        if (pair.second.is_common) {
            // Allocate space in BSS
            pair.second.is_defined = true;
            pair.second.is_common = false;
            pair.second.section = ".bss";
        }
    }
}

bool Linker::CheckUndefinedSymbols() {
    std::vector<std::string> undefined;
    
    for (const auto &pair : symbol_table_) {
        if (!pair.second.is_defined) {
            // Check if it's a forced undefined symbol
            bool forced = false;
            for (const auto &u : config_.undefined_symbols) {
                if (u == pair.first) {
                    forced = true;
                    break;
                }
            }
            
            if (!forced) {
                undefined.push_back(pair.first);
            }
        }
    }
    
    stats_.symbols_undefined = static_cast<int>(undefined.size());
    
    if (!undefined.empty() && config_.no_undefined) {
        for (const auto &name : undefined) {
            ReportError("Undefined symbol: " + name);
        }
        return false;
    }
    
    // Report warnings for undefined symbols
    for (const auto &name : undefined) {
        ReportWarning("Undefined symbol: " + name);
    }
    
    return true;
}

// ============================================================================
// Section Layout
// ============================================================================

void Linker::CreateStandardSections() {
    // Create standard output sections
    std::vector<std::string> standard_sections = {
        ".text", ".rodata", ".data", ".bss", ".init_array", ".fini_array"
    };
    
    for (const auto &name : standard_sections) {
        OutputSection sec;
        sec.name = name;
        sec.flags = GetDefaultSectionFlags(name);
        sec.type = GetSectionTypeFromName(name);
        output_sections_.push_back(std::move(sec));
    }
}

void Linker::MergeInputSections() {
    // Group input sections by name and merge into output sections
    for (std::size_t obj_idx = 0; obj_idx < objects_.size(); ++obj_idx) {
        const auto &obj = objects_[obj_idx];
        
        for (std::size_t sec_idx = 0; sec_idx < obj.sections.size(); ++sec_idx) {
            const auto &in_sec = obj.sections[sec_idx];
            
            // Skip non-allocatable sections if not keeping debug
            if ((static_cast<std::uint64_t>(in_sec.flags) & 
                 static_cast<std::uint64_t>(SectionFlags::kAlloc)) == 0) {
                if (config_.strip_debug || config_.strip_all) {
                    continue;
                }
            }
            
            // Find or create output section
            OutputSection* out_sec = nullptr;
            for (auto &sec : output_sections_) {
                if (sec.name == in_sec.name) {
                    out_sec = &sec;
                    break;
                }
            }
            
            if (!out_sec) {
                OutputSection new_sec;
                new_sec.name = in_sec.name;
                new_sec.flags = in_sec.flags;
                new_sec.type = in_sec.type;
                new_sec.alignment = in_sec.alignment;
                output_sections_.push_back(std::move(new_sec));
                out_sec = &output_sections_.back();
            }
            
            // Update alignment
            if (in_sec.alignment > out_sec->alignment) {
                out_sec->alignment = in_sec.alignment;
            }
            
            // Align current position
            std::uint64_t aligned_size = AlignTo(out_sec->data.size(), in_sec.alignment);
            
            // Add contribution record
            OutputSection::Contribution contrib;
            contrib.object_index = static_cast<int>(obj_idx);
            contrib.section_index = static_cast<int>(sec_idx);
            contrib.input_offset = 0;
            contrib.output_offset = aligned_size;
            contrib.size = in_sec.data.size();
            out_sec->contributions.push_back(contrib);
            
            // Add padding if needed
            while (out_sec->data.size() < aligned_size) {
                out_sec->data.push_back(0);
            }
            
            // Copy section data
            out_sec->data.insert(out_sec->data.end(), in_sec.data.begin(), in_sec.data.end());
            
            stats_.sections_merged++;
        }
    }
}

void Linker::AssignSectionAddresses() {
    std::uint64_t current_addr = config_.base_address;
    std::uint64_t current_offset = 0;
    
    // Sort sections by type: .text first, then .rodata, .data, .bss
    auto section_priority = [](const std::string &name) -> int {
        if (name == ".text" || name == "__text") return 0;
        if (name == ".rodata" || name == "__const") return 1;
        if (name == ".data" || name == "__data") return 2;
        if (name == ".bss" || name == "__bss") return 3;
        if (name.find(".init") != std::string::npos) return 4;
        if (name.find(".fini") != std::string::npos) return 5;
        return 10;
    };
    
    std::sort(output_sections_.begin(), output_sections_.end(),
              [&section_priority](const OutputSection &a, const OutputSection &b) {
                  return section_priority(a.name) < section_priority(b.name);
              });
    
    // Calculate program header offset (for ELF)
    current_offset = sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) * 4;  // Estimate
    current_addr = config_.base_address + current_offset;
    
    for (auto &sec : output_sections_) {
        if (sec.data.empty()) continue;
        
        // Align address and offset
        current_addr = AlignTo(current_addr, sec.alignment);
        current_offset = AlignTo(current_offset, sec.alignment);
        
        sec.virtual_address = current_addr;
        sec.file_offset = current_offset;
        
        current_addr += sec.data.size();
        
        // BSS doesn't take file space
        if (sec.type != SectionType::kNobits) {
            current_offset += sec.data.size();
        }
        
        // Track sizes
        if (sec.name == ".text" || sec.name == "__text") {
            stats_.code_size += sec.data.size();
        } else if (sec.name == ".bss" || sec.name == "__bss") {
            stats_.bss_size += sec.data.size();
        } else {
            stats_.data_size += sec.data.size();
        }
    }
}

void Linker::AssignSymbolAddresses() {
    // Update symbol addresses based on section layout
    for (auto &pair : symbol_table_) {
        Symbol &sym = pair.second;
        if (!sym.is_defined || sym.is_absolute) continue;
        
        // Find the section
        for (const auto &sec : output_sections_) {
            if (sec.name == sym.section) {
                sym.value = sec.virtual_address + sym.offset;
                break;
            }
        }
        
        // If not found in output sections, look at contributions
        if (sym.value == 0 && sym.object_file_index >= 0) {
            for (const auto &sec : output_sections_) {
                for (const auto &contrib : sec.contributions) {
                    if (contrib.object_index == sym.object_file_index) {
                        // Check if symbol is in this contribution
                        const auto &obj = objects_[contrib.object_index];
                        if (contrib.section_index < static_cast<int>(obj.sections.size())) {
                            const auto &in_sec = obj.sections[contrib.section_index];
                            if (in_sec.name == sym.section) {
                                sym.value = sec.virtual_address + contrib.output_offset + sym.offset;
                                break;
                            }
                        }
                    }
                }
                if (sym.value != 0) break;
            }
        }
    }
}

void Linker::CreateSegments() {
    // Create segments for executable output
    if (config_.output_format == OutputFormat::kExecutable ||
        config_.output_format == OutputFormat::kSharedLibrary) {
        
        // Text segment (read + execute)
        OutputSegment text_seg;
        text_seg.name = "__TEXT";
        text_seg.protection = PF_R | PF_X;
        
        // Data segment (read + write)
        OutputSegment data_seg;
        data_seg.name = "__DATA";
        data_seg.protection = PF_R | PF_W;
        
        // Assign sections to segments
        for (const auto &sec : output_sections_) {
            bool is_exec = (static_cast<std::uint64_t>(sec.flags) & 
                           static_cast<std::uint64_t>(SectionFlags::kExecInstr)) != 0;
            bool is_write = (static_cast<std::uint64_t>(sec.flags) &
                            static_cast<std::uint64_t>(SectionFlags::kWrite)) != 0;
            
            if (is_exec) {
                text_seg.section_names.push_back(sec.name);
            } else if (is_write) {
                data_seg.section_names.push_back(sec.name);
            } else {
                // Read-only data goes with text
                text_seg.section_names.push_back(sec.name);
            }
        }
        
        if (!text_seg.section_names.empty()) {
            segments_.push_back(std::move(text_seg));
        }
        if (!data_seg.section_names.empty()) {
            segments_.push_back(std::move(data_seg));
        }
    }
}

bool Linker::LayoutSections() {
    Trace("Laying out sections...");
    
    // Create standard sections
    CreateStandardSections();
    
    // Merge input sections into output sections
    MergeInputSections();
    
    // Remove empty sections
    output_sections_.erase(
        std::remove_if(output_sections_.begin(), output_sections_.end(),
                       [](const OutputSection &sec) { return sec.data.empty(); }),
        output_sections_.end());
    
    // Assign addresses
    AssignSectionAddresses();
    
    // Create segments
    CreateSegments();
    
    // Assign symbol addresses
    AssignSymbolAddresses();
    
    Trace("Layout complete: " + std::to_string(output_sections_.size()) + " sections");
    
    return true;
}

OutputSection* Linker::GetOutputSection(const std::string &name) {
    for (auto &sec : output_sections_) {
        if (sec.name == name) return &sec;
    }
    return nullptr;
}

void Linker::CreateOutputSection(const std::string &name, SectionFlags flags) {
    OutputSection sec;
    sec.name = name;
    sec.flags = flags;
    output_sections_.push_back(std::move(sec));
}

// ============================================================================
// Relocation Processing
// ============================================================================

bool Linker::ApplyRelocations() {
    Trace("Applying relocations...");
    
    for (std::size_t obj_idx = 0; obj_idx < objects_.size(); ++obj_idx) {
        const auto &obj = objects_[obj_idx];
        
        for (const auto &reloc : obj.relocations) {
            // Find output section for this relocation
            OutputSection* out_sec = nullptr;
            std::uint64_t section_base = 0;
            
            for (auto &sec : output_sections_) {
                for (const auto &contrib : sec.contributions) {
                    if (contrib.object_index == static_cast<int>(obj_idx)) {
                        const auto &in_sec = obj.sections[contrib.section_index];
                        if (in_sec.name == reloc.section) {
                            out_sec = &sec;
                            section_base = sec.virtual_address;
                            break;
                        }
                    }
                }
                if (out_sec) break;
            }
            
            if (!out_sec) {
                ReportWarning("Cannot find section for relocation: " + reloc.section);
                continue;
            }
            
            // Apply the relocation
            if (!ApplyRelocation(reloc, *out_sec, section_base)) {
                return false;
            }
            
            stats_.relocations_processed++;
        }
    }
    
    Trace("Applied " + std::to_string(stats_.relocations_processed) + " relocations");
    
    return true;
}

std::int64_t Linker::CalculateRelocationValue(const Relocation &reloc, 
                                               std::uint64_t reloc_addr) {
    // Look up symbol
    const Symbol* sym = LookupSymbol(reloc.symbol);
    if (!sym) {
        ReportWarning("Undefined symbol in relocation: " + reloc.symbol);
        return 0;
    }
    
    std::int64_t sym_addr = static_cast<std::int64_t>(sym->value);
    std::int64_t value = sym_addr + reloc.addend;
    
    if (reloc.is_pc_relative) {
        value -= static_cast<std::int64_t>(reloc_addr);
    }
    
    return value;
}

bool Linker::ApplyRelocation(const Relocation &reloc, OutputSection &section,
                              std::uint64_t section_base) {
    // Find the offset within the output section
    std::uint64_t offset = 0;
    bool found = false;
    
    for (const auto &contrib : section.contributions) {
        const auto &obj = objects_[contrib.object_index];
        const auto &in_sec = obj.sections[contrib.section_index];
        
        if (in_sec.name == reloc.section) {
            offset = contrib.output_offset + reloc.offset;
            found = true;
            break;
        }
    }
    
    if (!found) {
        return true;  // Skip if section not found
    }
    
    // Calculate relocation address
    std::uint64_t reloc_addr = section_base + offset;
    
    // Calculate value
    std::int64_t value = CalculateRelocationValue(reloc, reloc_addr);
    
    // Apply based on target architecture
    if (config_.target_arch == TargetArch::kX86_64) {
        return ApplyELFRelocation_x86_64(reloc, section.data, offset, value);
    } else if (config_.target_arch == TargetArch::kAArch64) {
        return ApplyELFRelocation_ARM64(reloc, section.data, offset, value);
    }
    
    return ApplyELFRelocation_x86_64(reloc, section.data, offset, value);
}

bool Linker::ApplyELFRelocation_x86_64(const Relocation &reloc,
                                        std::vector<std::uint8_t> &data,
                                        std::uint64_t offset, std::int64_t value) {
    if (offset >= data.size()) {
        ReportWarning("Relocation offset out of bounds");
        return true;
    }
    
    std::uint32_t rtype = reloc.type;
    
    switch (rtype) {
        case 1:  // R_X86_64_64
            if (offset + 8 <= data.size()) {
                std::memcpy(&data[offset], &value, 8);
            }
            break;
            
        case 2:   // R_X86_64_PC32
        case 4:   // R_X86_64_PLT32
        case 9:   // R_X86_64_GOTPCREL
        case 26:  // R_X86_64_GOTPC32
        {
            if (offset + 4 <= data.size()) {
                std::int32_t val32 = static_cast<std::int32_t>(value);
                std::memcpy(&data[offset], &val32, 4);
            }
            break;
        }
            
        case 10:  // R_X86_64_32
        case 11:  // R_X86_64_32S
        {
            if (offset + 4 <= data.size()) {
                std::int32_t val32 = static_cast<std::int32_t>(value);
                std::memcpy(&data[offset], &val32, 4);
            }
            break;
        }
            
        case 12:  // R_X86_64_16
        case 13:  // R_X86_64_PC16
        {
            if (offset + 2 <= data.size()) {
                std::int16_t val16 = static_cast<std::int16_t>(value);
                std::memcpy(&data[offset], &val16, 2);
            }
            break;
        }
            
        case 14:  // R_X86_64_8
        case 15:  // R_X86_64_PC8
        {
            if (offset + 1 <= data.size()) {
                std::int8_t val8 = static_cast<std::int8_t>(value);
                data[offset] = static_cast<std::uint8_t>(val8);
            }
            break;
        }
            
        case 24:  // R_X86_64_PC64
            if (offset + 8 <= data.size()) {
                std::memcpy(&data[offset], &value, 8);
            }
            break;
            
        default:
            // Unknown relocation - skip
            break;
    }
    
    return true;
}

bool Linker::ApplyELFRelocation_ARM64(const Relocation &reloc,
                                       std::vector<std::uint8_t> &data,
                                       std::uint64_t offset, std::int64_t value) {
    if (offset >= data.size()) {
        ReportWarning("Relocation offset out of bounds");
        return true;
    }
    
    std::uint32_t rtype = reloc.type;
    
    switch (rtype) {
        case 257:  // R_AARCH64_ABS64
            if (offset + 8 <= data.size()) {
                std::memcpy(&data[offset], &value, 8);
            }
            break;
            
        case 258:  // R_AARCH64_ABS32
            if (offset + 4 <= data.size()) {
                std::int32_t val32 = static_cast<std::int32_t>(value);
                std::memcpy(&data[offset], &val32, 4);
            }
            break;
            
        case 282:  // R_AARCH64_JUMP26
        case 283:  // R_AARCH64_CALL26
        {
            if (offset + 4 <= data.size()) {
                // Branch offset is in units of 4 bytes
                std::int64_t branch_offset = value >> 2;
                std::uint32_t insn;
                std::memcpy(&insn, &data[offset], 4);
                // Clear and set the 26-bit immediate
                insn = (insn & 0xFC000000) | (static_cast<std::uint32_t>(branch_offset) & 0x3FFFFFF);
                std::memcpy(&data[offset], &insn, 4);
            }
            break;
        }
            
        case 275:  // R_AARCH64_ADR_PREL_PG_HI21
        {
            if (offset + 4 <= data.size()) {
                // Page address (bits 12-32 of PC-relative)
                std::int64_t page = (value >> 12) & 0x1FFFFF;
                std::uint32_t insn;
                std::memcpy(&insn, &data[offset], 4);
                // Encode immlo (bits 29-30) and immhi (bits 5-23)
                std::uint32_t immlo = static_cast<std::uint32_t>(page & 0x3);
                std::uint32_t immhi = static_cast<std::uint32_t>((page >> 2) & 0x7FFFF);
                insn = (insn & 0x9F00001F) | (immlo << 29) | (immhi << 5);
                std::memcpy(&data[offset], &insn, 4);
            }
            break;
        }
            
        case 277:  // R_AARCH64_ADD_ABS_LO12_NC
        {
            if (offset + 4 <= data.size()) {
                // Low 12 bits
                std::uint32_t imm = static_cast<std::uint32_t>(value & 0xFFF);
                std::uint32_t insn;
                std::memcpy(&insn, &data[offset], 4);
                insn = (insn & 0xFFC003FF) | (imm << 10);
                std::memcpy(&data[offset], &insn, 4);
            }
            break;
        }
            
        default:
            // Unknown relocation - skip
            break;
    }
    
    return true;
}

bool Linker::ApplyMachORelocation(const Relocation &reloc,
                                   std::vector<std::uint8_t> &data,
                                   std::uint64_t offset, std::int64_t value) {
    if (offset >= data.size()) {
        return true;
    }
    
    // Apply based on size
    switch (reloc.size) {
        case 1:
            data[offset] = static_cast<std::uint8_t>(value);
            break;
        case 2:
            if (offset + 2 <= data.size()) {
                std::int16_t val16 = static_cast<std::int16_t>(value);
                std::memcpy(&data[offset], &val16, 2);
            }
            break;
        case 4:
            if (offset + 4 <= data.size()) {
                std::int32_t val32 = static_cast<std::int32_t>(value);
                std::memcpy(&data[offset], &val32, 4);
            }
            break;
        case 8:
            if (offset + 8 <= data.size()) {
                std::memcpy(&data[offset], &value, 8);
            }
            break;
    }
    
    return true;
}

// ============================================================================
// GOT/PLT Generation
// ============================================================================

void Linker::CreateGOT() {
    if (got_entries_.empty()) return;
    
    OutputSection got_sec;
    got_sec.name = ".got";
    got_sec.flags = SectionFlags::kAlloc | SectionFlags::kWrite;
    got_sec.alignment = 8;
    
    for (auto &entry : got_entries_) {
        entry.address = got_sec.virtual_address + got_sec.data.size();
        
        // Each GOT entry is 8 bytes
        std::uint64_t value = 0;
        const Symbol* sym = LookupSymbol(entry.symbol);
        if (sym && sym->is_defined) {
            value = sym->value;
            entry.is_resolved = true;
        }
        
        WriteValue(got_sec.data, value);
    }
    
    output_sections_.push_back(std::move(got_sec));
}

void Linker::CreatePLT() {
    if (plt_entries_.empty()) return;
    
    OutputSection plt_sec;
    plt_sec.name = ".plt";
    plt_sec.flags = SectionFlags::kAlloc | SectionFlags::kExecInstr;
    plt_sec.alignment = 16;
    
    // PLT header (16 bytes for x86_64)
    std::vector<std::uint8_t> plt_header = {
        0xff, 0x35, 0x00, 0x00, 0x00, 0x00,  // push [rip + GOT+8]
        0xff, 0x25, 0x00, 0x00, 0x00, 0x00,  // jmp [rip + GOT+16]
        0x0f, 0x1f, 0x40, 0x00               // nop
    };
    plt_sec.data.insert(plt_sec.data.end(), plt_header.begin(), plt_header.end());
    
    // PLT entries (16 bytes each)
    for (auto &entry : plt_entries_) {
        entry.address = plt_sec.virtual_address + plt_sec.data.size();
        
        std::vector<std::uint8_t> plt_entry = {
            0xff, 0x25, 0x00, 0x00, 0x00, 0x00,  // jmp [rip + GOT]
            0x68, 0x00, 0x00, 0x00, 0x00,        // push index
            0xe9, 0x00, 0x00, 0x00, 0x00         // jmp PLT[0]
        };
        
        plt_sec.data.insert(plt_sec.data.end(), plt_entry.begin(), plt_entry.end());
    }
    
    output_sections_.push_back(std::move(plt_sec));
}

void Linker::AddGOTEntry(const std::string &symbol) {
    // Check if already exists
    for (const auto &entry : got_entries_) {
        if (entry.symbol == symbol) return;
    }
    
    GOTEntry entry;
    entry.symbol = symbol;
    entry.index = static_cast<int>(got_entries_.size());
    got_entries_.push_back(std::move(entry));
}

void Linker::AddPLTEntry(const std::string &symbol) {
    // Check if already exists
    for (const auto &entry : plt_entries_) {
        if (entry.symbol == symbol) return;
    }
    
    PLTEntry entry;
    entry.symbol = symbol;
    entry.index = static_cast<int>(plt_entries_.size());
    plt_entries_.push_back(std::move(entry));
    
    // Also add GOT entry
    AddGOTEntry(symbol);
}

// ============================================================================
// Output Generation
// ============================================================================

bool Linker::GenerateOutput() {
    Trace("Generating output: " + config_.output_file);
    
    switch (config_.output_format) {
        case OutputFormat::kExecutable:
            return GenerateELFExecutable();
            
        case OutputFormat::kSharedLibrary:
            return GenerateELFSharedLibrary();
            
        case OutputFormat::kRelocatable:
            return GenerateRelocatable();
            
        case OutputFormat::kStaticLibrary:
            return GenerateStaticLibrary();
    }
    
    return false;
}

bool Linker::GenerateELFExecutable() {
    std::ofstream out(config_.output_file, std::ios::binary);
    if (!out) {
        ReportError("Cannot create output file: " + config_.output_file);
        return false;
    }
    
    std::vector<std::uint8_t> output;
    
    // Build ELF header
    Elf64_Ehdr ehdr{};
    std::memcpy(ehdr.e_ident, ELFMAG, SELFMAG);
    ehdr.e_ident[4] = ELFCLASS64;
    ehdr.e_ident[5] = ELFDATA2LSB;
    ehdr.e_ident[6] = 1;  // EV_CURRENT
    ehdr.e_type = ET_EXEC;
    
    // Set machine type
    switch (config_.target_arch) {
        case TargetArch::kX86_64: ehdr.e_machine = EM_X86_64; break;
        case TargetArch::kAArch64: ehdr.e_machine = EM_AARCH64; break;
        case TargetArch::kX86: ehdr.e_machine = EM_386; break;
        case TargetArch::kARM: ehdr.e_machine = EM_ARM; break;
        case TargetArch::kRISCV64: ehdr.e_machine = EM_RISCV; break;
    }
    
    ehdr.e_version = 1;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_phentsize = sizeof(Elf64_Phdr);
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    
    // Find entry point
    const Symbol* entry_sym = LookupSymbol(config_.entry_point);
    if (entry_sym && entry_sym->is_defined) {
        ehdr.e_entry = entry_sym->value;
    } else {
        // Use base of .text section
        for (const auto &sec : output_sections_) {
            if (sec.name == ".text") {
                ehdr.e_entry = sec.virtual_address;
                break;
            }
        }
    }
    
    // Create program headers
    std::vector<Elf64_Phdr> phdrs;
    
    // Program header for PHDR itself
    Elf64_Phdr phdr_phdr{};
    phdr_phdr.p_type = PT_PHDR;
    phdr_phdr.p_flags = PF_R;
    phdr_phdr.p_offset = sizeof(Elf64_Ehdr);
    phdr_phdr.p_vaddr = config_.base_address + sizeof(Elf64_Ehdr);
    phdr_phdr.p_paddr = phdr_phdr.p_vaddr;
    phdr_phdr.p_align = 8;
    phdrs.push_back(phdr_phdr);
    
    // Create LOAD segments for each section group
    std::uint64_t current_offset = sizeof(Elf64_Ehdr);
    
    // Reserve space for program headers (estimate 4 headers)
    current_offset += sizeof(Elf64_Phdr) * 4;
    
    for (auto &sec : output_sections_) {
        if (sec.data.empty()) continue;
        
        // Align offset
        current_offset = AlignTo(current_offset, sec.alignment);
        sec.file_offset = current_offset;
        
        Elf64_Phdr phdr{};
        phdr.p_type = PT_LOAD;
        phdr.p_offset = sec.file_offset;
        phdr.p_vaddr = sec.virtual_address;
        phdr.p_paddr = sec.virtual_address;
        phdr.p_filesz = sec.data.size();
        phdr.p_memsz = sec.data.size();
        phdr.p_align = sec.alignment;
        
        // Set flags
        phdr.p_flags = PF_R;
        if ((static_cast<std::uint64_t>(sec.flags) & 
             static_cast<std::uint64_t>(SectionFlags::kWrite)) != 0) {
            phdr.p_flags |= PF_W;
        }
        if ((static_cast<std::uint64_t>(sec.flags) & 
             static_cast<std::uint64_t>(SectionFlags::kExecInstr)) != 0) {
            phdr.p_flags |= PF_X;
        }
        
        phdrs.push_back(phdr);
        current_offset += sec.data.size();
    }
    
    // Update PHDR segment
    phdrs[0].p_filesz = sizeof(Elf64_Phdr) * phdrs.size();
    phdrs[0].p_memsz = phdrs[0].p_filesz;
    
    // Set program header offset and count
    ehdr.e_phoff = sizeof(Elf64_Ehdr);
    ehdr.e_phnum = static_cast<Elf64_Half>(phdrs.size());
    
    // Write ELF header
    WriteBytes(output, &ehdr, sizeof(ehdr));
    
    // Write program headers
    for (const auto &phdr : phdrs) {
        WriteBytes(output, &phdr, sizeof(phdr));
    }
    
    // Write sections
    for (const auto &sec : output_sections_) {
        if (sec.data.empty()) continue;
        
        // Add padding
        while (output.size() < sec.file_offset) {
            output.push_back(0);
        }
        
        // Write section data
        output.insert(output.end(), sec.data.begin(), sec.data.end());
    }
    
    // Write to file
    out.write(reinterpret_cast<const char*>(output.data()), output.size());
    
    stats_.total_output_size = output.size();
    
    Trace("Generated executable: " + std::to_string(output.size()) + " bytes");
    
    return true;
}

bool Linker::GenerateELFSharedLibrary() {
    // Similar to executable but with different ELF type
    ReportWarning("Shared library generation uses simplified implementation");
    return GenerateELFExecutable();
}

bool Linker::GenerateMachOExecutable() {
    std::ofstream out(config_.output_file, std::ios::binary);
    if (!out) {
        ReportError("Cannot create output file: " + config_.output_file);
        return false;
    }

    Trace("Generating Mach-O executable: " + config_.output_file);

    std::vector<std::uint8_t> output;

    // Mach-O header (64-bit)
    struct MachOHeader64 {
        std::uint32_t magic{0xFEEDFACF};      // MH_MAGIC_64
        std::uint32_t cputype{0};
        std::uint32_t cpusubtype{3};            // CPU_SUBTYPE_ALL
        std::uint32_t filetype{2};              // MH_EXECUTE
        std::uint32_t ncmds{0};
        std::uint32_t sizeofcmds{0};
        std::uint32_t flags{0x00000085};        // MH_NOUNDEFS | MH_DYLDLINK | MH_PIE
        std::uint32_t reserved{0};
    } mh{};

    // Set CPU type from config
    switch (config_.target_arch) {
        case TargetArch::kX86_64:
            mh.cputype = 0x01000007;  // CPU_TYPE_X86_64
            break;
        case TargetArch::kAArch64:
            mh.cputype = 0x0100000C;  // CPU_TYPE_ARM64
            break;
        default:
            mh.cputype = 0x01000007;
            break;
    }

    // We emit a minimal Mach-O with __TEXT and __DATA segments and a
    // __LINKEDIT segment. Collect section data first.
    std::vector<std::uint8_t> text_data;
    std::vector<std::uint8_t> data_data;

    for (auto &sec : output_sections_) {
        if (static_cast<std::uint64_t>(sec.flags) &
            static_cast<std::uint64_t>(SectionFlags::kExecInstr)) {
            text_data.insert(text_data.end(), sec.data.begin(), sec.data.end());
        } else if (sec.type != SectionType::kNobits) {
            data_data.insert(data_data.end(), sec.data.begin(), sec.data.end());
        }
    }

    // Compute sizes (page-aligned)
    constexpr std::uint64_t page = 4096;
    auto align_page = [page](std::uint64_t v) {
        return (v + page - 1) & ~(page - 1);
    };

    std::uint64_t header_size = sizeof(mh) + 512;  // rough load command area
    std::uint64_t text_file_off = align_page(header_size);
    std::uint64_t text_file_sz  = align_page(text_data.size());
    std::uint64_t data_file_off = text_file_off + text_file_sz;
    std::uint64_t data_file_sz  = align_page(data_data.size());
    std::uint64_t linkedit_off  = data_file_off + data_file_sz;

    // Build load commands into a buffer
    std::vector<std::uint8_t> cmds;
    auto push32 = [&cmds](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) cmds.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
    };
    auto push64 = [&cmds](std::uint64_t v) {
        for (int i = 0; i < 8; ++i) cmds.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
    };
    auto push_str16 = [&cmds](const char *s) {
        char buf[16] = {};
        std::strncpy(buf, s, 15);
        cmds.insert(cmds.end(), buf, buf + 16);
    };
    std::uint32_t num_cmds = 0;

    // LC_SEGMENT_64 for __TEXT
    {
        push32(0x19);        // LC_SEGMENT_64
        push32(72 + 80);     // cmdsize (segment + 1 section header)
        push_str16("__TEXT");
        push64(config_.base_address);               // vmaddr
        push64(text_file_sz + text_file_off);        // vmsize
        push64(0);                                   // fileoff
        push64(text_file_off + text_file_sz);        // filesize
        push32(5);  // maxprot  = r-x
        push32(5);  // initprot = r-x
        push32(1);  // nsects
        push32(0);  // flags

        // Section header: __text
        push_str16("__text");
        push_str16("__TEXT");
        push64(config_.base_address + text_file_off);  // addr
        push64(text_data.size());                       // size
        push32(static_cast<std::uint32_t>(text_file_off));
        push32(0);  // align
        push32(0);  // reloff
        push32(0);  // nreloc
        push32(0x80000400);  // S_REGULAR | S_ATTR_PURE_INSTRUCTIONS
        push32(0); push32(0); push32(0);  // reserved

        ++num_cmds;
    }

    // LC_SEGMENT_64 for __DATA
    if (!data_data.empty()) {
        push32(0x19);
        push32(72 + 80);
        push_str16("__DATA");
        push64(config_.base_address + text_file_off + text_file_sz);
        push64(data_file_sz);
        push64(data_file_off);
        push64(data_file_sz);
        push32(3);  // maxprot  = rw-
        push32(3);  // initprot = rw-
        push32(1);
        push32(0);

        push_str16("__data");
        push_str16("__DATA");
        push64(config_.base_address + data_file_off);
        push64(data_data.size());
        push32(static_cast<std::uint32_t>(data_file_off));
        push32(0);
        push32(0);
        push32(0);
        push32(0);
        push32(0); push32(0); push32(0);

        ++num_cmds;
    }

    // LC_SEGMENT_64 for __LINKEDIT (empty)
    {
        push32(0x19);
        push32(72);
        push_str16("__LINKEDIT");
        push64(config_.base_address + linkedit_off);
        push64(page);
        push64(linkedit_off);
        push64(0);
        push32(1); push32(1); push32(0); push32(0);
        ++num_cmds;
    }

    // LC_MAIN (entry point)
    {
        push32(0x80000028);  // LC_MAIN
        push32(24);
        const Symbol *entry = LookupSymbol(config_.entry_point);
        std::uint64_t entry_off = entry && entry->is_defined ? entry->value - config_.base_address : text_file_off;
        push64(entry_off);
        push64(0);  // stack size
        ++num_cmds;
    }

    mh.ncmds = num_cmds;
    mh.sizeofcmds = static_cast<std::uint32_t>(cmds.size());

    // Write header
    WriteBytes(output, &mh, sizeof(mh));
    output.insert(output.end(), cmds.begin(), cmds.end());

    // Pad to text_file_off
    while (output.size() < text_file_off) output.push_back(0);

    // Write text
    output.insert(output.end(), text_data.begin(), text_data.end());
    while (output.size() < data_file_off) output.push_back(0);

    // Write data
    output.insert(output.end(), data_data.begin(), data_data.end());
    while (output.size() < linkedit_off) output.push_back(0);

    // Write __LINKEDIT placeholder page
    for (std::uint64_t i = 0; i < page; ++i) output.push_back(0);

    out.write(reinterpret_cast<const char *>(output.data()),
              static_cast<std::streamsize>(output.size()));
    stats_.total_output_size = output.size();
    return true;
}

bool Linker::GenerateMachODylib() {
    // Dylib is identical to executable except filetype = MH_DYLIB (6)
    // and LC_ID_DYLIB replaces LC_MAIN. Reuse the executable codepath
    // with a few tweaks.
    Trace("Generating Mach-O dylib (delegating to executable path)");
    ReportWarning("Mach-O dylib uses simplified executable-style generation");
    return GenerateMachOExecutable();
}

// ============================================================================
// Relocatable Output (partial link, -r)
// ============================================================================

bool Linker::GenerateRelocatable() {
    std::ofstream out(config_.output_file, std::ios::binary);
    if (!out) {
        ReportError("Cannot create output file: " + config_.output_file);
        return false;
    }

    Trace("Generating relocatable output: " + config_.output_file);

    // Merge all input sections by name and concatenate their data.
    // Output a new ELF relocatable object file (ET_REL).
    std::vector<std::uint8_t> output;

    // ELF header (ET_REL)
    Elf64_Ehdr ehdr{};
    std::memcpy(ehdr.e_ident, ELFMAG, SELFMAG);
    ehdr.e_ident[4] = ELFCLASS64;
    ehdr.e_ident[5] = ELFDATA2LSB;
    ehdr.e_ident[6] = 1;
    ehdr.e_type = ET_REL;

    switch (config_.target_arch) {
        case TargetArch::kX86_64:  ehdr.e_machine = EM_X86_64;  break;
        case TargetArch::kAArch64: ehdr.e_machine = EM_AARCH64; break;
        case TargetArch::kX86:     ehdr.e_machine = EM_386;     break;
        case TargetArch::kARM:     ehdr.e_machine = EM_ARM;     break;
        case TargetArch::kRISCV64: ehdr.e_machine = EM_RISCV;   break;
    }

    ehdr.e_version = 1;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_shentsize = sizeof(Elf64_Shdr);

    // Collect merged output sections (already done by LayoutSections)
    // Build shstrtab, strtab, symtab, and section data.
    std::vector<std::uint8_t> shstrtab;
    shstrtab.push_back(0);  // null entry

    auto add_shstring = [&shstrtab](const std::string &s) -> std::uint32_t {
        auto off = static_cast<std::uint32_t>(shstrtab.size());
        shstrtab.insert(shstrtab.end(), s.begin(), s.end());
        shstrtab.push_back(0);
        return off;
    };

    // Section 0 is null. Then output sections, then shstrtab.
    struct SecEntry {
        Elf64_Shdr shdr{};
        std::vector<std::uint8_t> data;
    };
    std::vector<SecEntry> sec_entries;

    // Null section
    sec_entries.push_back({});

    // Output sections
    for (auto &sec : output_sections_) {
        SecEntry e;
        e.shdr.sh_name = add_shstring(sec.name);
        e.shdr.sh_type = (sec.type == SectionType::kNobits) ? SHT_NOBITS : SHT_PROGBITS;
        e.shdr.sh_flags = 0;
        if (static_cast<std::uint64_t>(sec.flags) &
            static_cast<std::uint64_t>(SectionFlags::kAlloc))
            e.shdr.sh_flags |= SHF_ALLOC;
        if (static_cast<std::uint64_t>(sec.flags) &
            static_cast<std::uint64_t>(SectionFlags::kWrite))
            e.shdr.sh_flags |= SHF_WRITE;
        if (static_cast<std::uint64_t>(sec.flags) &
            static_cast<std::uint64_t>(SectionFlags::kExecInstr))
            e.shdr.sh_flags |= SHF_EXECINSTR;
        e.shdr.sh_addralign = sec.alignment > 0 ? sec.alignment : 1;
        e.data = sec.data;
        sec_entries.push_back(std::move(e));
    }

    // .shstrtab
    std::uint32_t shstrtab_name = add_shstring(".shstrtab");
    {
        SecEntry e;
        e.shdr.sh_name = shstrtab_name;
        e.shdr.sh_type = SHT_STRTAB;
        e.data = shstrtab;
        sec_entries.push_back(std::move(e));
    }

    // Compute offsets
    std::uint64_t offset = sizeof(Elf64_Ehdr);
    for (std::size_t i = 1; i < sec_entries.size(); ++i) {
        auto align = sec_entries[i].shdr.sh_addralign;
        if (align < 1) align = 1;
        offset = (offset + align - 1) & ~(align - 1);
        sec_entries[i].shdr.sh_offset = offset;
        sec_entries[i].shdr.sh_size = sec_entries[i].data.size();
        offset += sec_entries[i].data.size();
    }

    // Section header table comes after all section data
    std::uint64_t sh_offset = (offset + 7) & ~7ULL;
    ehdr.e_shoff = sh_offset;
    ehdr.e_shnum = static_cast<std::uint16_t>(sec_entries.size());
    ehdr.e_shstrndx = static_cast<std::uint16_t>(sec_entries.size() - 1);

    // Write output
    WriteBytes(output, &ehdr, sizeof(ehdr));

    // Write sections
    for (std::size_t i = 1; i < sec_entries.size(); ++i) {
        while (output.size() < sec_entries[i].shdr.sh_offset) output.push_back(0);
        output.insert(output.end(), sec_entries[i].data.begin(),
                      sec_entries[i].data.end());
    }

    // Pad to section header table
    while (output.size() < sh_offset) output.push_back(0);

    // Write section headers
    for (auto &se : sec_entries) {
        WriteBytes(output, &se.shdr, sizeof(se.shdr));
    }

    out.write(reinterpret_cast<const char *>(output.data()),
              static_cast<std::streamsize>(output.size()));
    stats_.total_output_size = output.size();
    return true;
}

// ============================================================================
// Static Library Output (ar archive)
// ============================================================================

bool Linker::GenerateStaticLibrary() {
    std::ofstream out(config_.output_file, std::ios::binary);
    if (!out) {
        ReportError("Cannot create output file: " + config_.output_file);
        return false;
    }

    Trace("Generating static library: " + config_.output_file);

    // Write Unix ar archive format:
    //   "!<arch>\n"
    //   For each object: 60-byte header + data (padded to 2 bytes)
    out.write(AR_MAGIC, AR_MAGIC_LEN);

    for (auto &obj : objects_) {
        // Read original object file data
        std::ifstream obj_in(obj.path, std::ios::binary);
        if (!obj_in) {
            ReportWarning("Cannot re-read object for archive: " + obj.path);
            continue;
        }
        obj_in.seekg(0, std::ios::end);
        std::uint64_t obj_size = static_cast<std::uint64_t>(obj_in.tellg());
        obj_in.seekg(0);
        std::vector<char> obj_data(obj_size);
        obj_in.read(obj_data.data(), static_cast<std::streamsize>(obj_size));

        // Build member name (basename, truncated to 15 chars + '/')
        std::string basename = obj.path;
        auto slash = basename.find_last_of("/\\");
        if (slash != std::string::npos) basename = basename.substr(slash + 1);
        if (basename.size() > 15) basename = basename.substr(0, 15);
        basename += '/';

        // 60-byte ar header
        char header[60];
        std::memset(header, ' ', 60);
        std::memcpy(header, basename.c_str(),
                    std::min(basename.size(), static_cast<std::size_t>(16)));
        // Timestamp, owner, group — fill with 0
        std::memcpy(header + 16, "0           ", 12);  // mtime
        std::memcpy(header + 28, "0     ", 6);         // uid
        std::memcpy(header + 34, "0     ", 6);         // gid
        std::memcpy(header + 40, "100644  ", 8);       // mode
        // Size (decimal, right-padded with spaces)
        std::string size_str = std::to_string(obj_size);
        std::memcpy(header + 48, size_str.c_str(),
                    std::min(size_str.size(), static_cast<std::size_t>(10)));
        header[58] = '`';
        header[59] = '\n';

        out.write(header, 60);
        out.write(obj_data.data(), static_cast<std::streamsize>(obj_size));

        // Pad to 2-byte boundary
        if (obj_size & 1) out.put('\n');
    }

    stats_.total_output_size = static_cast<std::uint64_t>(out.tellp());
    return true;
}

// ============================================================================
// Main Link Process
// ============================================================================

bool Linker::Link() {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    Trace("Starting link process...");
    
    // Load all input files
    if (!LoadObjectFiles()) {
        ReportError("Failed to load object files");
        return false;
    }
    
    // Resolve symbols
    if (!ResolveSymbols()) {
        ReportError("Symbol resolution failed");
        return false;
    }
    
    // Layout sections
    if (!LayoutSections()) {
        ReportError("Section layout failed");
        return false;
    }
    
    // Apply relocations
    if (!ApplyRelocations()) {
        ReportError("Relocation processing failed");
        return false;
    }
    
    // Generate output
    if (!GenerateOutput()) {
        ReportError("Output generation failed");
        return false;
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    stats_.link_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    
    if (config_.verbose) {
        std::cout << "Link completed in " << stats_.link_time_ms << "ms\n";
        std::cout << "Output: " << config_.output_file << " ("
                  << stats_.total_output_size << " bytes)\n";
    }
    
    return errors_.empty();
}

// ============================================================================
// Library Path Resolution
// ============================================================================

std::string Linker::ResolveLibraryPath(const std::string &name) {
    // Try each library path
    for (const auto &dir : config_.library_paths) {
        // Try static library
        std::string static_lib = dir + "/lib" + name + ".a";
        std::ifstream f(static_lib);
        if (f.good()) return static_lib;
        
        // Try shared library
        std::string shared_lib = dir + "/lib" + name + ".so";
        f.open(shared_lib);
        if (f.good()) return shared_lib;
        
        // macOS dylib
        std::string dylib = dir + "/lib" + name + ".dylib";
        f.open(dylib);
        if (f.good()) return dylib;
    }
    
    return "";
}

// ============================================================================
// Linker Script Parser Implementation
// ============================================================================

LinkerScriptParser::LinkerScriptParser(const std::string &script) : script_(script) {}

bool LinkerScriptParser::Parse() {
    while (pos_ < script_.size()) {
        SkipWhitespace();
        if (pos_ >= script_.size()) break;
        
        if (!ParseCommand()) {
            return false;
        }
    }
    
    return true;
}

bool LinkerScriptParser::ParseCommand() {
    std::string cmd = ParseIdentifier();
    if (cmd.empty()) return false;
    
    if (cmd == "ENTRY") {
        return ParseEntry();
    } else if (cmd == "SECTIONS") {
        return ParseSections();
    }
    
    // Skip unknown command
    while (pos_ < script_.size() && script_[pos_] != ';' && script_[pos_] != '{') {
        ++pos_;
    }
    if (pos_ < script_.size() && script_[pos_] == ';') ++pos_;
    
    return true;
}

bool LinkerScriptParser::ParseEntry() {
    if (!Expect("(")) return false;
    entry_point_ = ParseIdentifier();
    if (!Expect(")")) return false;
    return true;
}

bool LinkerScriptParser::ParseSections() {
    if (!Expect("{")) return false;
    
    while (pos_ < script_.size() && script_[pos_] != '}') {
        SkipWhitespace();
        if (script_[pos_] == '}') break;
        
        // Parse section rule
        ScriptSectionRule rule;
        rule.output_section = ParseIdentifier();
        
        SkipWhitespace();
        
        // Check for address
        if (script_[pos_] == '0' || script_[pos_] == '.') {
            rule.address = std::stoull(ParseIdentifier(), nullptr, 0);
        }
        
        if (!Expect(":")) {
            // Skip to next section
            while (pos_ < script_.size() && script_[pos_] != '}' && script_[pos_] != ';') {
                ++pos_;
            }
            continue;
        }
        
        if (!Expect("{")) continue;
        
        // Parse input patterns
        while (pos_ < script_.size() && script_[pos_] != '}') {
            SkipWhitespace();
            std::string pattern = ParseIdentifier();
            if (!pattern.empty()) {
                rule.input_patterns.push_back(pattern);
            }
            SkipWhitespace();
        }
        
        Expect("}");
        section_rules_.push_back(std::move(rule));
    }
    
    Expect("}");
    return true;
}

ScriptValue LinkerScriptParser::ParseExpression() {
    SkipWhitespace();
    std::string ident = ParseIdentifier();
    
    // Try to parse as number
    if (!ident.empty() && (std::isdigit(ident[0]) || ident[0] == '0')) {
        try {
            return std::stoull(ident, nullptr, 0);
        } catch (...) {
            return ident;
        }
    }
    
    return ident;
}

std::string LinkerScriptParser::ParseIdentifier() {
    SkipWhitespace();
    std::string result;
    
    while (pos_ < script_.size()) {
        char c = script_[pos_];
        if (std::isalnum(c) || c == '_' || c == '.' || c == '*' || c == '-') {
            result += c;
            ++pos_;
        } else {
            break;
        }
    }
    
    return result;
}

bool LinkerScriptParser::SkipWhitespace() {
    while (pos_ < script_.size()) {
        char c = script_[pos_];
        if (std::isspace(c)) {
            ++pos_;
        } else if (c == '/' && pos_ + 1 < script_.size() && script_[pos_ + 1] == '*') {
            // Skip C-style comment
            pos_ += 2;
            while (pos_ + 1 < script_.size() && 
                   !(script_[pos_] == '*' && script_[pos_ + 1] == '/')) {
                ++pos_;
            }
            pos_ += 2;
        } else if (c == '/' && pos_ + 1 < script_.size() && script_[pos_ + 1] == '/') {
            // Skip line comment
            while (pos_ < script_.size() && script_[pos_] != '\n') {
                ++pos_;
            }
        } else {
            break;
        }
    }
    return true;
}

bool LinkerScriptParser::Match(const std::string &s) {
    SkipWhitespace();
    if (pos_ + s.size() > script_.size()) return false;
    
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (script_[pos_ + i] != s[i]) return false;
    }
    
    pos_ += s.size();
    return true;
}

bool LinkerScriptParser::Expect(const std::string &s) {
    if (!Match(s)) {
        error_ = "Expected '" + s + "' at position " + std::to_string(pos_);
        return false;
    }
    return true;
}

} // namespace polyglot::linker

// ============================================================================
// Main Entry Point
// ============================================================================

#ifndef LINKER_LIBRARY
int main(int argc, char **argv) {
    using namespace polyglot::linker;
    
    LinkerConfig config;
    
    // Parse command-line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-o" && i + 1 < argc) {
            config.output_file = argv[++i];
        } else if (arg == "-L" && i + 1 < argc) {
            config.library_paths.push_back(argv[++i]);
        } else if (arg.substr(0, 2) == "-L") {
            config.library_paths.push_back(arg.substr(2));
        } else if (arg == "-l" && i + 1 < argc) {
            config.libraries.push_back(argv[++i]);
        } else if (arg.substr(0, 2) == "-l") {
            config.libraries.push_back(arg.substr(2));
        } else if (arg == "-e" && i + 1 < argc) {
            config.entry_point = argv[++i];
        } else if (arg == "--entry" && i + 1 < argc) {
            config.entry_point = argv[++i];
        } else if (arg == "-T" && i + 1 < argc) {
            config.linker_script = argv[++i];
        } else if (arg == "-static") {
            config.static_link = true;
        } else if (arg == "-shared") {
            config.output_format = OutputFormat::kSharedLibrary;
            config.static_link = false;
        } else if (arg == "-r" || arg == "--relocatable") {
            config.output_format = OutputFormat::kRelocatable;
        } else if (arg == "--strip-all" || arg == "-s") {
            config.strip_all = true;
        } else if (arg == "--strip-debug" || arg == "-S") {
            config.strip_debug = true;
        } else if (arg == "--gc-sections") {
            config.gc_sections = true;
        } else if (arg == "--no-undefined") {
            config.no_undefined = true;
        } else if (arg == "--allow-multiple-definition") {
            config.allow_multiple_definition = true;
        } else if (arg == "-pie" || arg == "--pie") {
            config.pie = true;
        } else if (arg == "--build-id") {
            config.build_id = true;
        } else if (arg == "--icf") {
            config.icf = true;
        } else if (arg == "-v" || arg == "--verbose") {
            config.verbose = true;
        } else if (arg == "--trace") {
            config.trace = true;
        } else if (arg == "-m" && i + 1 < argc) {
            std::string arch = argv[++i];
            if (arch == "elf_x86_64") {
                config.target_arch = TargetArch::kX86_64;
            } else if (arch == "aarch64linux" || arch == "aarch64elf") {
                config.target_arch = TargetArch::kAArch64;
            }
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: polyld [options] files...\n"
                      << "\nOptions:\n"
                      << "  -o <file>        Set output file name\n"
                      << "  -e <symbol>      Set entry point symbol\n"
                      << "  -L <dir>         Add library search path\n"
                      << "  -l <lib>         Link with library\n"
                      << "  -T <script>      Use linker script\n"
                      << "  -static          Create static executable\n"
                      << "  -shared          Create shared library\n"
                      << "  -r               Create relocatable output\n"
                      << "  -s, --strip-all  Strip all symbols\n"
                      << "  -S, --strip-debug Strip debug symbols\n"
                      << "  --gc-sections    Garbage collect unused sections\n"
                      << "  --no-undefined   Report undefined symbols as errors\n"
                      << "  --pie            Create position-independent executable\n"
                      << "  -v, --verbose    Verbose output\n"
                      << "  --trace          Trace file loading\n"
                      << "  -h, --help       Show this help\n";
            return 0;
        } else if (arg[0] != '-') {
            config.input_files.push_back(arg);
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
        }
    }
    
    if (config.input_files.empty()) {
        std::cerr << "polyld: no input files\n"
                  << "Usage: polyld [options] files...\n"
                  << "Try 'polyld --help' for more information.\n";
        return 1;
    }
    
    // Create and run linker
    Linker linker(config);

    // Run cross-language link resolution before the main link so that
    // glue stubs and resolved symbols are available for relocation.
    PolyglotLinker poly_linker(config);
    if (!poly_linker.ResolveLinks()) {
        if (config.verbose) {
            std::cerr << "polyld: cross-language link warnings:\n";
            for (const auto &e : poly_linker.GetErrors()) {
                std::cerr << "  " << e << "\n";
            }
        }
        // Cross-language resolution failures are non-fatal when there are
        // no registered link entries (i.e. pure native linking).
    }

    if (!linker.Link()) {
        std::cerr << "polyld: link failed\n";
        for (const auto &err : linker.GetErrors()) {
            std::cerr << "  " << err << "\n";
        }
        return 1;
    }
    
    if (config.verbose) {
        const auto &stats = linker.GetStats();
        std::cout << "Linked " << stats.objects_loaded << " object files\n";
        std::cout << "Processed " << stats.relocations_processed << " relocations\n";
        std::cout << "Output size: " << stats.total_output_size << " bytes\n";
    }
    
    return 0;
}
#endif // LINKER_LIBRARY
