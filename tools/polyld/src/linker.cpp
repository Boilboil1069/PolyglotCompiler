#include "tools/polyld/include/linker.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>

#if __has_include(<elf.h>)
#include <elf.h>
#else
// Minimal ELF definitions
using Elf64_Addr = std::uint64_t;
using Elf64_Off = std::uint64_t;
using Elf64_Half = std::uint16_t;
using Elf64_Word = std::uint32_t;
using Elf64_Xword = std::uint64_t;

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

#define ELFMAG "\177ELF"
#define SELFMAG 4
#define ET_REL 1
#define ET_EXEC 2
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHF_WRITE 0x1
#define SHF_ALLOC 0x2
#define SHF_EXECINSTR 0x4
#define STB_GLOBAL 1
#define STT_FUNC 2
#define PT_LOAD 1
#define PF_X 1
#define PF_W 2
#define PF_R 4
#endif

namespace polyglot::linker {

Linker::Linker(const LinkerConfig &config) : config_(config) {}

void Linker::ReportError(const std::string &msg) {
    errors_.push_back(msg);
    std::cerr << "Error: " << msg << "\n";
}

void Linker::ReportWarning(const std::string &msg) {
    warnings_.push_back(msg);
    std::cerr << "Warning: " << msg << "\n";
}

std::uint64_t Linker::AlignTo(std::uint64_t value, std::uint64_t align) {
    if (align == 0) return value;
    return (value + align - 1) & ~(align - 1);
}

bool Linker::LoadELF(const std::string &path, ObjectFile &obj) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        ReportError("Cannot open file: " + path);
        return false;
    }
    
    Elf64_Ehdr ehdr;
    file.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
    
    if (std::memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
        ReportError("Not an ELF file: " + path);
        return false;
    }
    
    if (ehdr.e_type != ET_REL) {
        ReportError("Not a relocatable object file: " + path);
        return false;
    }
    
    // Read section headers
    std::vector<Elf64_Shdr> shdrs(ehdr.e_shnum);
    file.seekg(ehdr.e_shoff);
    file.read(reinterpret_cast<char*>(shdrs.data()), ehdr.e_shnum * sizeof(Elf64_Shdr));
    
    // Read section header string table
    std::vector<char> shstrtab(shdrs[ehdr.e_shstrndx].sh_size);
    file.seekg(shdrs[ehdr.e_shstrndx].sh_offset);
    file.read(shstrtab.data(), shstrtab.size());
    
    // Load sections
    for (size_t i = 0; i < shdrs.size(); ++i) {
        const auto &shdr = shdrs[i];
        if (shdr.sh_type != SHT_PROGBITS && shdr.sh_type != SHT_SYMTAB &&
            shdr.sh_type != SHT_STRTAB) {
            continue;
        }
        
        std::string name = &shstrtab[shdr.sh_name];
        
        if (shdr.sh_type == SHT_PROGBITS) {
            InputSection sec;
            sec.name = name;
            sec.alignment = shdr.sh_addralign;
            sec.flags = shdr.sh_flags;
            sec.object_file_index = static_cast<int>(objects_.size());
            
            if (shdr.sh_size > 0) {
                sec.data.resize(shdr.sh_size);
                file.seekg(shdr.sh_offset);
                file.read(reinterpret_cast<char*>(sec.data.data()), shdr.sh_size);
            }
            
            obj.sections.push_back(sec);
        } else if (shdr.sh_type == SHT_SYMTAB) {
            // Load symbol table
            std::vector<Elf64_Sym> syms(shdr.sh_size / sizeof(Elf64_Sym));
            file.seekg(shdr.sh_offset);
            file.read(reinterpret_cast<char*>(syms.data()), shdr.sh_size);
            
            // Load associated string table
            const auto &strtab_shdr = shdrs[shdr.sh_link];
            std::vector<char> strtab(strtab_shdr.sh_size);
            file.seekg(strtab_shdr.sh_offset);
            file.read(strtab.data(), strtab.size());
            
            for (const auto &sym : syms) {
                if (sym.st_name == 0) continue;
                
                Symbol symbol;
                symbol.name = &strtab[sym.st_name];
                symbol.offset = sym.st_value;
                symbol.size = sym.st_size;
                symbol.is_global = (sym.st_info >> 4) == STB_GLOBAL;
                symbol.is_defined = sym.st_shndx != 0;
                symbol.object_file_index = static_cast<int>(objects_.size());
                
                if (sym.st_shndx > 0 && sym.st_shndx < shdrs.size()) {
                    symbol.section = &shstrtab[shdrs[sym.st_shndx].sh_name];
                }
                
                obj.symbols.push_back(symbol);
            }
        }
    }
    
    return true;
}

bool Linker::LoadMachO(const std::string &path, ObjectFile &obj) {
    ReportWarning("Mach-O loading not yet fully implemented: " + path);
    return false;
}

bool Linker::LoadObjectFiles() {
    for (const auto &path : config_.input_files) {
        ObjectFile obj;
        obj.path = path;
        
        // Try ELF first
        if (LoadELF(path, obj)) {
            objects_.push_back(obj);
            continue;
        }
        
        // Try Mach-O
        if (LoadMachO(path, obj)) {
            objects_.push_back(obj);
            continue;
        }
        
        ReportError("Failed to load object file: " + path);
        return false;
    }
    
    return true;
}

bool Linker::ResolveSymbols() {
    // Build symbol table
    for (const auto &obj : objects_) {
        for (const auto &sym : obj.symbols) {
            if (!sym.is_defined) continue;
            
            auto it = symbol_table_.find(sym.name);
            if (it != symbol_table_.end()) {
                if (it->second.is_defined && sym.is_global && it->second.is_global) {
                    ReportError("Multiple definitions of symbol: " + sym.name);
                    return false;
                }
                // Prefer defined symbol
                if (sym.is_defined) {
                    it->second = sym;
                }
            } else {
                symbol_table_[sym.name] = sym;
            }
        }
    }
    
    // Check for undefined symbols
    for (const auto &obj : objects_) {
        for (const auto &sym : obj.symbols) {
            if (!sym.is_defined && sym.is_global) {
                auto it = symbol_table_.find(sym.name);
                if (it == symbol_table_.end() || !it->second.is_defined) {
                    ReportError("Undefined symbol: " + sym.name);
                    return false;
                }
            }
        }
    }
    
    return true;
}

bool Linker::LayoutSections() {
    std::uint64_t current_addr = config_.base_address;
    
    // Group sections by name
    std::unordered_map<std::string, std::vector<const InputSection*>> section_groups;
    for (const auto &obj : objects_) {
        for (const auto &sec : obj.sections) {
            section_groups[sec.name].push_back(&sec);
        }
    }
    
    // Create output sections
    for (const auto &pair : section_groups) {
        OutputSection out_sec;
        out_sec.name = pair.first;
        out_sec.alignment = 16;  // Default alignment
        
        // Determine flags
        if (pair.first == ".text") {
            out_sec.flags = SHF_ALLOC | SHF_EXECINSTR;
        } else if (pair.first == ".data") {
            out_sec.flags = SHF_ALLOC | SHF_WRITE;
        } else if (pair.first == ".rodata") {
            out_sec.flags = SHF_ALLOC;
        }
        
        // Merge input sections
        for (const auto *in_sec : pair.second) {
            std::uint64_t align_offset = AlignTo(out_sec.data.size(), in_sec->alignment);
            
            // Add padding
            while (out_sec.data.size() < align_offset) {
                out_sec.data.push_back(0);
            }
            
            // Append section data
            out_sec.data.insert(out_sec.data.end(), in_sec->data.begin(), in_sec->data.end());
            out_sec.alignment = std::max(out_sec.alignment, in_sec->alignment);
        }
        
        // Assign virtual address
        current_addr = AlignTo(current_addr, out_sec.alignment);
        out_sec.virtual_address = current_addr;
        current_addr += out_sec.data.size();
        
        output_sections_.push_back(out_sec);
    }
    
    return true;
}

bool Linker::ApplyRelocations() {
    // Simplified relocation - would need full implementation
    ReportWarning("Relocation processing not yet fully implemented");
    return true;
}

bool Linker::GenerateExecutable() {
    std::ofstream out(config_.output_file, std::ios::binary);
    if (!out) {
        ReportError("Cannot create output file: " + config_.output_file);
        return false;
    }
    
    // Generate ELF executable
    Elf64_Ehdr ehdr{};
    std::memcpy(ehdr.e_ident, ELFMAG, SELFMAG);
    ehdr.e_ident[4] = 2;  // ELFCLASS64
    ehdr.e_ident[5] = 1;  // ELFDATA2LSB
    ehdr.e_ident[6] = 1;  // EV_CURRENT
    ehdr.e_type = ET_EXEC;
    ehdr.e_machine = 62;  // EM_X86_64
    ehdr.e_version = 1;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_phentsize = sizeof(Elf64_Phdr);
    
    // Find entry point
    auto entry_it = symbol_table_.find(config_.entry_point);
    if (entry_it != symbol_table_.end()) {
        ehdr.e_entry = entry_it->second.offset + config_.base_address;
    }
    
    // Create program headers
    std::vector<Elf64_Phdr> phdrs;
    std::uint64_t file_offset = sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) * output_sections_.size();
    
    for (auto &sec : output_sections_) {
        file_offset = AlignTo(file_offset, sec.alignment);
        sec.file_offset = file_offset;
        
        Elf64_Phdr phdr{};
        phdr.p_type = PT_LOAD;
        phdr.p_offset = sec.file_offset;
        phdr.p_vaddr = sec.virtual_address;
        phdr.p_paddr = sec.virtual_address;
        phdr.p_filesz = sec.data.size();
        phdr.p_memsz = sec.data.size();
        phdr.p_align = sec.alignment;
        
        if (sec.flags & SHF_EXECINSTR) phdr.p_flags |= PF_X;
        if (sec.flags & SHF_WRITE) phdr.p_flags |= PF_W;
        phdr.p_flags |= PF_R;
        
        phdrs.push_back(phdr);
        file_offset += sec.data.size();
    }
    
    ehdr.e_phoff = sizeof(Elf64_Ehdr);
    ehdr.e_phnum = static_cast<Elf64_Half>(phdrs.size());
    
    // Write ELF header
    out.write(reinterpret_cast<const char*>(&ehdr), sizeof(ehdr));
    
    // Write program headers
    for (const auto &phdr : phdrs) {
        out.write(reinterpret_cast<const char*>(&phdr), sizeof(phdr));
    }
    
    // Write sections
    for (const auto &sec : output_sections_) {
        out.seekp(sec.file_offset);
        out.write(reinterpret_cast<const char*>(sec.data.data()), sec.data.size());
    }
    
    return true;
}

bool Linker::Link() {
    if (!LoadObjectFiles()) {
        ReportError("Failed to load object files");
        return false;
    }
    
    if (!ResolveSymbols()) {
        ReportError("Symbol resolution failed");
        return false;
    }
    
    if (!LayoutSections()) {
        ReportError("Section layout failed");
        return false;
    }
    
    if (!ApplyRelocations()) {
        ReportError("Relocation failed");
        return false;
    }
    
    if (!GenerateExecutable()) {
        ReportError("Executable generation failed");
        return false;
    }
    
    std::cout << "Successfully linked: " << config_.output_file << "\n";
    return true;
}

} // namespace polyglot::linker

// Main entry point for polyld
int main(int argc, char **argv) {
    using namespace polyglot::linker;
    
    LinkerConfig config;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            config.output_file = argv[++i];
        } else if (arg == "-L" && i + 1 < argc) {
            config.library_paths.push_back(argv[++i]);
        } else if (arg == "-e" && i + 1 < argc) {
            config.entry_point = argv[++i];
        } else if (arg == "--strip-debug") {
            config.strip_debug = true;
        } else if (arg[0] != '-') {
            config.input_files.push_back(arg);
        }
    }
    
    if (config.input_files.empty()) {
        std::cerr << "Usage: polyld [-o output] [-e entry] [object files...]\n";
        return 1;
    }
    
    Linker linker(config);
    return linker.Link() ? 0 : 1;
}
