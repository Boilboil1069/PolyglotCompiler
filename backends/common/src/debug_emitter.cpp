#include "backends/common/include/debug_emitter.h"
#include "backends/common/include/dwarf_builder.h"
#include "common/include/debug/dwarf5.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <random>
#include <unordered_map>

namespace polyglot::backends {

namespace {

// ============================================================================
// Internal ELF Builder (self-contained implementation)
// ============================================================================

// ELF constants
constexpr uint8_t ELFCLASS64 = 2;
constexpr uint8_t ELFDATA2LSB = 1;
constexpr uint8_t EV_CURRENT_ELF = 1;
constexpr uint16_t ET_REL = 1;
constexpr uint16_t EM_X86_64 = 62;

constexpr uint32_t SHT_NULL = 0;
constexpr uint32_t SHT_PROGBITS = 1;
constexpr uint32_t SHT_SYMTAB = 2;
constexpr uint32_t SHT_STRTAB = 3;

// Section data structure for internal use
struct DebugSection {
    std::string name;
    std::vector<uint8_t> data;
    uint32_t flags{0};
};

// Build an ELF object file from debug sections
std::vector<uint8_t> BuildELFObject(const std::vector<DebugSection>& sections) {
    std::vector<uint8_t> result;
    
    auto write_u8 = [&](uint8_t v) { result.push_back(v); };
    auto write_u16 = [&](uint16_t v) {
        result.push_back(v & 0xFF);
        result.push_back((v >> 8) & 0xFF);
    };
    auto write_u32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) result.push_back((v >> (i * 8)) & 0xFF);
    };
    auto write_u64 = [&](uint64_t v) {
        for (int i = 0; i < 8; ++i) result.push_back((v >> (i * 8)) & 0xFF);
    };
    auto align_to = [&](size_t alignment) {
        while (result.size() % alignment != 0) result.push_back(0);
    };
    
    // Build section header string table
    std::vector<uint8_t> shstrtab;
    shstrtab.push_back(0);  // Null string
    
    std::vector<uint32_t> section_name_offsets;
    for (const auto& sec : sections) {
        section_name_offsets.push_back(static_cast<uint32_t>(shstrtab.size()));
        shstrtab.insert(shstrtab.end(), sec.name.begin(), sec.name.end());
        shstrtab.push_back(0);
    }
    
    // Add .shstrtab name
    uint32_t shstrtab_name_offset = static_cast<uint32_t>(shstrtab.size());
    const char* shstrtab_name = ".shstrtab";
    shstrtab.insert(shstrtab.end(), shstrtab_name, shstrtab_name + 10);
    
    // ELF header (64-bit)
    uint8_t e_ident[16] = {0};
    e_ident[0] = 0x7F;
    e_ident[1] = 'E';
    e_ident[2] = 'L';
    e_ident[3] = 'F';
    e_ident[4] = ELFCLASS64;
    e_ident[5] = ELFDATA2LSB;
    e_ident[6] = EV_CURRENT_ELF;
    result.insert(result.end(), e_ident, e_ident + 16);
    
    write_u16(ET_REL);      // e_type: relocatable
    write_u16(EM_X86_64);   // e_machine: x86-64
    write_u32(EV_CURRENT_ELF);  // e_version
    write_u64(0);           // e_entry
    write_u64(0);           // e_phoff (no program headers)
    
    size_t e_shoff_pos = result.size();
    write_u64(0);           // e_shoff (placeholder)
    
    write_u32(0);           // e_flags
    write_u16(64);          // e_ehsize
    write_u16(0);           // e_phentsize
    write_u16(0);           // e_phnum
    write_u16(64);          // e_shentsize
    
    uint16_t shnum = static_cast<uint16_t>(sections.size() + 2);  // +null, +shstrtab
    write_u16(shnum);       // e_shnum
    write_u16(static_cast<uint16_t>(shnum - 1));  // e_shstrndx
    
    // Write section data
    std::vector<uint64_t> section_offsets;
    for (const auto& sec : sections) {
        align_to(16);
        section_offsets.push_back(result.size());
        result.insert(result.end(), sec.data.begin(), sec.data.end());
    }
    
    // Write shstrtab data
    align_to(8);
    uint64_t shstrtab_offset = result.size();
    result.insert(result.end(), shstrtab.begin(), shstrtab.end());
    
    // Section headers
    align_to(8);
    uint64_t sh_offset = result.size();
    
    // Patch e_shoff
    for (int i = 0; i < 8; ++i) {
        result[e_shoff_pos + i] = static_cast<uint8_t>((sh_offset >> (i * 8)) & 0xFF);
    }
    
    // Null section header
    for (int i = 0; i < 64; ++i) write_u8(0);
    
    // Section headers for debug sections
    for (size_t i = 0; i < sections.size(); ++i) {
        write_u32(section_name_offsets[i]);  // sh_name
        write_u32(SHT_PROGBITS);             // sh_type
        write_u64(0);                        // sh_flags
        write_u64(0);                        // sh_addr
        write_u64(section_offsets[i]);       // sh_offset
        write_u64(sections[i].data.size());  // sh_size
        write_u32(0);                        // sh_link
        write_u32(0);                        // sh_info
        write_u64(1);                        // sh_addralign
        write_u64(0);                        // sh_entsize
    }
    
    // .shstrtab section header
    write_u32(shstrtab_name_offset);         // sh_name
    write_u32(SHT_STRTAB);                   // sh_type
    write_u64(0);                            // sh_flags
    write_u64(0);                            // sh_addr
    write_u64(shstrtab_offset);              // sh_offset
    write_u64(shstrtab.size());              // sh_size
    write_u32(0);                            // sh_link
    write_u32(0);                            // sh_info
    write_u64(1);                            // sh_addralign
    write_u64(0);                            // sh_entsize
    
    return result;
}

// ============================================================================
// DWARF Constants
// ============================================================================

// DWARF 5 standard opcodes
constexpr uint8_t kDwLnsCopy = 0x01;
constexpr uint8_t kDwLnsAdvancePc = 0x02;
constexpr uint8_t kDwLnsAdvanceLine = 0x03;
constexpr uint8_t kDwLnsSetFile = 0x04;
constexpr uint8_t kDwLnsSetColumn = 0x05;
constexpr uint8_t kDwLnsNegateStmt = 0x06;
constexpr uint8_t kDwLnsSetBasicBlock = 0x07;
constexpr uint8_t kDwLnsConstAddPc = 0x08;
constexpr uint8_t kDwLnsFixedAdvancePc = 0x09;
constexpr uint8_t kDwLnsSetPrologueEnd = 0x0a;
constexpr uint8_t kDwLnsSetEpilogueBegin = 0x0b;
constexpr uint8_t kDwLnsSetIsa = 0x0c;

// DWARF 5 extended opcodes
constexpr uint8_t kDwLneEndSequence = 0x01;
constexpr uint8_t kDwLneSetAddress = 0x02;
constexpr uint8_t kDwLneDefineFile = 0x03;
constexpr uint8_t kDwLneSetDiscriminator = 0x04;

// DWARF tags
constexpr uint16_t kDwTagCompileUnit = 0x11;
constexpr uint16_t kDwTagSubprogram = 0x2e;
constexpr uint16_t kDwTagVariable = 0x34;
constexpr uint16_t kDwTagFormalParameter = 0x05;
constexpr uint16_t kDwTagBaseType = 0x24;
constexpr uint16_t kDwTagPointerType = 0x0f;
constexpr uint16_t kDwTagStructureType = 0x13;
constexpr uint16_t kDwTagTypedef = 0x16;

// DWARF attributes
constexpr uint16_t kDwAtName = 0x03;
constexpr uint16_t kDwAtLowPc = 0x11;
constexpr uint16_t kDwAtHighPc = 0x12;
constexpr uint16_t kDwAtLanguage = 0x13;
constexpr uint16_t kDwAtProducer = 0x25;
constexpr uint16_t kDwAtCompDir = 0x1b;
constexpr uint16_t kDwAtStmtList = 0x10;
constexpr uint16_t kDwAtByteSize = 0x0b;
constexpr uint16_t kDwAtEncoding = 0x3e;
constexpr uint16_t kDwAtType = 0x49;
constexpr uint16_t kDwAtExternal = 0x3f;
constexpr uint16_t kDwAtDeclFile = 0x3a;
constexpr uint16_t kDwAtDeclLine = 0x3b;
constexpr uint16_t kDwAtLocation = 0x02;
constexpr uint16_t kDwAtFrameBase = 0x40;

// DWARF forms
constexpr uint8_t kDwFormAddr = 0x01;
constexpr uint8_t kDwFormData1 = 0x0b;
constexpr uint8_t kDwFormData2 = 0x05;
constexpr uint8_t kDwFormData4 = 0x06;
constexpr uint8_t kDwFormData8 = 0x07;
constexpr uint8_t kDwFormString = 0x08;
constexpr uint8_t kDwFormStrp = 0x0e;
constexpr uint8_t kDwFormSecOffset = 0x17;
constexpr uint8_t kDwFormExprloc = 0x18;
constexpr uint8_t kDwFormFlag = 0x0c;
constexpr uint8_t kDwFormFlagPresent = 0x19;
constexpr uint8_t kDwFormRef4 = 0x13;

// DWARF base type encodings
constexpr uint8_t kDwAteSigned = 0x05;
constexpr uint8_t kDwAteUnsigned = 0x07;
constexpr uint8_t kDwAteFloat = 0x04;
constexpr uint8_t kDwAteBool = 0x02;
constexpr uint8_t kDwAteSignedChar = 0x06;
constexpr uint8_t kDwAteUnsignedChar = 0x08;

// DWARF languages
constexpr uint16_t kDwLangC = 0x0c;
constexpr uint16_t kDwLangCpp = 0x04;
constexpr uint16_t kDwLangCpp14 = 0x21;
constexpr uint16_t kDwLangPython = 0x14;
constexpr uint16_t kDwLangRust = 0x1c;

// ============================================================================
// Helper Functions
// ============================================================================

// Write unsigned LEB128 encoding
void WriteULEB128(std::vector<uint8_t> &out, uint64_t value) {
    do {
        uint8_t byte = value & 0x7F;
        value >>= 7;
        if (value != 0) {
            byte |= 0x80;
        }
        out.push_back(byte);
    } while (value != 0);
}

// Write signed LEB128 encoding
void WriteSLEB128(std::vector<uint8_t> &out, int64_t value) {
    bool more = true;
    while (more) {
        uint8_t byte = value & 0x7F;
        value >>= 7;
        
        if ((value == 0 && !(byte & 0x40)) ||
            (value == -1 && (byte & 0x40))) {
            more = false;
        } else {
            byte |= 0x80;
        }
        out.push_back(byte);
    }
}

// Write a value in little-endian format
template<typename T>
void WriteLE(std::vector<uint8_t> &out, T value) {
    for (size_t i = 0; i < sizeof(T); ++i) {
        out.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
    }
}

// Patch a 32-bit value at a specific offset
void Patch32(std::vector<uint8_t> &data, size_t offset, uint32_t value) {
    for (size_t i = 0; i < 4; ++i) {
        data[offset + i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
    }
}

// Align output to specified boundary
void AlignTo(std::vector<uint8_t> &out, size_t alignment) {
    while (out.size() % alignment != 0) {
        out.push_back(0);
    }
}

// Extract directory and filename from path
std::pair<std::string, std::string> SplitPath(const std::string &path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return {".", path};
    }
    return {path.substr(0, pos), path.substr(pos + 1)};
}

// ============================================================================
// DWARF Section Builder
// ============================================================================

class DwarfSectionBuilder {
public:
    DwarfSectionBuilder() = default;
    
    // Build .debug_str section (string table)
    std::vector<uint8_t> BuildDebugStr();
    
    // Build .debug_abbrev section (abbreviation table)
    std::vector<uint8_t> BuildDebugAbbrev();
    
    // Build .debug_info section (debugging information entries)
    std::vector<uint8_t> BuildDebugInfo(const DebugInfoBuilder &info);
    
    // Build .debug_line section (line number program)
    std::vector<uint8_t> BuildDebugLine(const DebugInfoBuilder &info);
    
    // Build .debug_frame section (call frame information)
    std::vector<uint8_t> BuildDebugFrame(const DebugInfoBuilder &info);
    
    // Build .debug_aranges section (address ranges)
    std::vector<uint8_t> BuildDebugAranges(const DebugInfoBuilder &info);
    
    // Add string to string table and return its offset
    uint32_t AddString(const std::string &str);
    
private:
    // String table management
    std::unordered_map<std::string, uint32_t> string_offsets_;
    std::vector<uint8_t> string_data_;
    
    // File table for line number program
    struct FileEntry {
        std::string directory;
        std::string filename;
        uint32_t dir_index;
    };
    std::vector<std::string> directories_;
    std::vector<FileEntry> files_;
    std::unordered_map<std::string, uint32_t> file_indices_;
    
    // Type DIE offsets for references
    std::unordered_map<std::string, uint32_t> type_offsets_;
    
    // Get or add file entry, returns file index
    uint32_t GetOrAddFile(const std::string &path);
    
    // Build abbreviation entry
    void WriteAbbrevEntry(std::vector<uint8_t> &out, uint32_t code, uint16_t tag,
                         bool has_children, 
                         const std::vector<std::pair<uint16_t, uint8_t>> &attrs);
    
    // Encode line number program header (DWARF 5)
    void EncodeLineHeader(std::vector<uint8_t> &out);
    
    // Encode line number program statements
    void EncodeLineStatements(std::vector<uint8_t> &out, 
                             const std::vector<DebugLineInfo> &lines);
};

uint32_t DwarfSectionBuilder::AddString(const std::string &str) {
    auto it = string_offsets_.find(str);
    if (it != string_offsets_.end()) {
        return it->second;
    }
    
    uint32_t offset = static_cast<uint32_t>(string_data_.size());
    string_offsets_[str] = offset;
    
    string_data_.insert(string_data_.end(), str.begin(), str.end());
    string_data_.push_back(0);  // Null terminator
    
    return offset;
}

uint32_t DwarfSectionBuilder::GetOrAddFile(const std::string &path) {
    auto it = file_indices_.find(path);
    if (it != file_indices_.end()) {
        return it->second;
    }
    
    auto [dir, filename] = SplitPath(path);
    
    // Find or add directory
    uint32_t dir_index = 0;
    for (size_t i = 0; i < directories_.size(); ++i) {
        if (directories_[i] == dir) {
            dir_index = static_cast<uint32_t>(i + 1);
            break;
        }
    }
    if (dir_index == 0) {
        directories_.push_back(dir);
        dir_index = static_cast<uint32_t>(directories_.size());
    }
    
    // Add file entry
    uint32_t file_index = static_cast<uint32_t>(files_.size() + 1);
    files_.push_back({dir, filename, dir_index});
    file_indices_[path] = file_index;
    
    return file_index;
}

std::vector<uint8_t> DwarfSectionBuilder::BuildDebugStr() {
    // Ensure we have at least a null string at the beginning
    if (string_data_.empty()) {
        string_data_.push_back(0);
    }
    return string_data_;
}

void DwarfSectionBuilder::WriteAbbrevEntry(
    std::vector<uint8_t> &out, uint32_t code, uint16_t tag,
    bool has_children, const std::vector<std::pair<uint16_t, uint8_t>> &attrs) {
    
    WriteULEB128(out, code);
    WriteULEB128(out, tag);
    out.push_back(has_children ? 1 : 0);
    
    for (const auto &[attr, form] : attrs) {
        WriteULEB128(out, attr);
        WriteULEB128(out, form);
    }
    
    // End of attribute list
    WriteULEB128(out, 0);
    WriteULEB128(out, 0);
}

std::vector<uint8_t> DwarfSectionBuilder::BuildDebugAbbrev() {
    std::vector<uint8_t> result;
    
    // Abbreviation 1: DW_TAG_compile_unit (with children)
    WriteAbbrevEntry(result, 1, kDwTagCompileUnit, true, {
        {kDwAtProducer, kDwFormStrp},
        {kDwAtLanguage, kDwFormData2},
        {kDwAtName, kDwFormStrp},
        {kDwAtCompDir, kDwFormStrp},
        {kDwAtLowPc, kDwFormAddr},
        {kDwAtHighPc, kDwFormData4},
        {kDwAtStmtList, kDwFormSecOffset}
    });
    
    // Abbreviation 2: DW_TAG_subprogram (with children for parameters/locals)
    WriteAbbrevEntry(result, 2, kDwTagSubprogram, true, {
        {kDwAtName, kDwFormStrp},
        {kDwAtDeclFile, kDwFormData4},
        {kDwAtDeclLine, kDwFormData4},
        {kDwAtLowPc, kDwFormAddr},
        {kDwAtHighPc, kDwFormData4},
        {kDwAtFrameBase, kDwFormExprloc},
        {kDwAtExternal, kDwFormFlagPresent}
    });
    
    // Abbreviation 3: DW_TAG_subprogram without children
    WriteAbbrevEntry(result, 3, kDwTagSubprogram, false, {
        {kDwAtName, kDwFormStrp},
        {kDwAtDeclFile, kDwFormData4},
        {kDwAtDeclLine, kDwFormData4},
        {kDwAtLowPc, kDwFormAddr},
        {kDwAtHighPc, kDwFormData4},
        {kDwAtExternal, kDwFormFlagPresent}
    });
    
    // Abbreviation 4: DW_TAG_base_type
    WriteAbbrevEntry(result, 4, kDwTagBaseType, false, {
        {kDwAtName, kDwFormStrp},
        {kDwAtByteSize, kDwFormData1},
        {kDwAtEncoding, kDwFormData1}
    });
    
    // Abbreviation 5: DW_TAG_variable
    WriteAbbrevEntry(result, 5, kDwTagVariable, false, {
        {kDwAtName, kDwFormStrp},
        {kDwAtDeclFile, kDwFormData4},
        {kDwAtDeclLine, kDwFormData4},
        {kDwAtType, kDwFormRef4}
    });
    
    // Abbreviation 6: DW_TAG_formal_parameter
    WriteAbbrevEntry(result, 6, kDwTagFormalParameter, false, {
        {kDwAtName, kDwFormStrp},
        {kDwAtType, kDwFormRef4},
        {kDwAtLocation, kDwFormExprloc}
    });
    
    // Abbreviation 7: DW_TAG_pointer_type
    WriteAbbrevEntry(result, 7, kDwTagPointerType, false, {
        {kDwAtType, kDwFormRef4},
        {kDwAtByteSize, kDwFormData1}
    });
    
    // Abbreviation 8: DW_TAG_structure_type
    WriteAbbrevEntry(result, 8, kDwTagStructureType, true, {
        {kDwAtName, kDwFormStrp},
        {kDwAtByteSize, kDwFormData4}
    });
    
    // Abbreviation 9: DW_TAG_typedef
    WriteAbbrevEntry(result, 9, kDwTagTypedef, false, {
        {kDwAtName, kDwFormStrp},
        {kDwAtType, kDwFormRef4}
    });
    
    // End of abbreviation table
    result.push_back(0);
    
    return result;
}

std::vector<uint8_t> DwarfSectionBuilder::BuildDebugInfo(const DebugInfoBuilder &info) {
    std::vector<uint8_t> result;
    
    // Reserve space for unit_length (32-bit DWARF format)
    size_t unit_length_pos = result.size();
    WriteLE<uint32_t>(result, 0);  // Placeholder
    
    // DWARF version (5)
    WriteLE<uint16_t>(result, 5);
    
    // Unit type: DW_UT_compile (0x01)
    result.push_back(0x01);
    
    // Address size (8 bytes for 64-bit)
    result.push_back(8);
    
    // Debug abbrev offset
    WriteLE<uint32_t>(result, 0);
    
    // Ensure producer string is in string table
    uint32_t producer_offset = AddString("PolyglotCompiler v3.0");
    
    // Determine compilation directory and source file from debug info
    std::string comp_dir = ".";
    std::string source_file = "source.cpp";
    uint64_t low_pc = 0;
    uint64_t high_pc = 0;
    
    // Find bounds from symbols
    for (const auto &sym : info.Symbols()) {
        if (sym.is_function) {
            if (low_pc == 0 || sym.address < low_pc) {
                low_pc = sym.address;
            }
            if (sym.address + sym.size > high_pc) {
                high_pc = sym.address + sym.size;
            }
        }
    }
    
    // Get source file from first line entry
    if (!info.Lines().empty()) {
        auto [dir, fname] = SplitPath(info.Lines()[0].file);
        comp_dir = dir;
        source_file = fname;
    }
    
    // Compile unit DIE (abbrev 1)
    WriteULEB128(result, 1);  // Abbreviation code
    
    WriteLE<uint32_t>(result, producer_offset);  // DW_AT_producer
    WriteLE<uint16_t>(result, kDwLangCpp14);     // DW_AT_language
    WriteLE<uint32_t>(result, AddString(source_file));  // DW_AT_name
    WriteLE<uint32_t>(result, AddString(comp_dir));     // DW_AT_comp_dir
    WriteLE<uint64_t>(result, low_pc);           // DW_AT_low_pc
    WriteLE<uint32_t>(result, static_cast<uint32_t>(high_pc - low_pc));  // DW_AT_high_pc
    WriteLE<uint32_t>(result, 0);                // DW_AT_stmt_list (offset in .debug_line)
    
    // Keep track of DIE offset for type references
    uint32_t current_offset = static_cast<uint32_t>(result.size());
    
    // Add basic types
    struct BasicTypeInfo {
        const char *name;
        uint8_t size;
        uint8_t encoding;
    };
    
    std::vector<BasicTypeInfo> basic_types = {
        {"int", 4, kDwAteSigned},
        {"long", 8, kDwAteSigned},
        {"unsigned int", 4, kDwAteUnsigned},
        {"unsigned long", 8, kDwAteUnsigned},
        {"float", 4, kDwAteFloat},
        {"double", 8, kDwAteFloat},
        {"char", 1, kDwAteSignedChar},
        {"unsigned char", 1, kDwAteUnsignedChar},
        {"bool", 1, kDwAteBool}
    };
    
    for (const auto &type : basic_types) {
        type_offsets_[type.name] = current_offset;
        
        WriteULEB128(result, 4);  // Abbreviation code for base type
        WriteLE<uint32_t>(result, AddString(type.name));  // DW_AT_name
        result.push_back(type.size);   // DW_AT_byte_size
        result.push_back(type.encoding);  // DW_AT_encoding
        
        current_offset = static_cast<uint32_t>(result.size());
    }
    
    // Add custom types from debug info
    for (const auto &type : info.Types()) {
        if (type_offsets_.find(type.name) == type_offsets_.end()) {
            type_offsets_[type.name] = current_offset;
            
            // Structure type
            if (type.kind == "struct" || type.kind == "class") {
                WriteULEB128(result, 8);  // Abbreviation code for structure type
                WriteLE<uint32_t>(result, AddString(type.name));
                WriteLE<uint32_t>(result, static_cast<uint32_t>(type.size));
                result.push_back(0);  // End children
            } else {
                // Typedef or alias
                WriteULEB128(result, 9);  // Abbreviation code for typedef
                WriteLE<uint32_t>(result, AddString(type.name));
                // Reference to base type (default to int if unknown)
                auto base_it = type_offsets_.find("int");
                WriteLE<uint32_t>(result, base_it != type_offsets_.end() ? 
                                         base_it->second : 0);
            }
            
            current_offset = static_cast<uint32_t>(result.size());
        }
    }
    
    // Add subprograms (functions)
    for (const auto &sym : info.Symbols()) {
        if (sym.is_function) {
            // Use abbreviation 3 (subprogram without children) for simplicity
            WriteULEB128(result, 3);
            
            WriteLE<uint32_t>(result, AddString(sym.name));  // DW_AT_name
            
            // Find file and line for this function
            uint32_t file_idx = 1;
            uint32_t line_num = 0;
            for (const auto &line : info.Lines()) {
                // Simple heuristic: first line entry is likely the function declaration
                file_idx = GetOrAddFile(line.file);
                line_num = static_cast<uint32_t>(line.line);
                break;
            }
            
            WriteLE<uint32_t>(result, file_idx);  // DW_AT_decl_file
            WriteLE<uint32_t>(result, line_num);  // DW_AT_decl_line
            WriteLE<uint64_t>(result, sym.address);  // DW_AT_low_pc
            WriteLE<uint32_t>(result, static_cast<uint32_t>(sym.size));  // DW_AT_high_pc
            // DW_AT_external is flag_present, no value needed
        }
    }
    
    // Add variables
    for (const auto &var : info.Variables()) {
        WriteULEB128(result, 5);  // Abbreviation code for variable
        
        WriteLE<uint32_t>(result, AddString(var.name));  // DW_AT_name
        WriteLE<uint32_t>(result, GetOrAddFile(var.file));  // DW_AT_decl_file
        WriteLE<uint32_t>(result, static_cast<uint32_t>(var.line));  // DW_AT_decl_line
        
        // Type reference
        auto type_it = type_offsets_.find(var.type);
        WriteLE<uint32_t>(result, type_it != type_offsets_.end() ? 
                                 type_it->second : 0);
    }
    
    // End of compile unit children
    result.push_back(0);
    
    // Patch unit_length
    uint32_t unit_length = static_cast<uint32_t>(result.size() - unit_length_pos - 4);
    Patch32(result, unit_length_pos, unit_length);
    
    return result;
}

void DwarfSectionBuilder::EncodeLineHeader(std::vector<uint8_t> &out) {
    // DWARF 5 line number program header
    
    // Minimum instruction length
    out.push_back(1);
    
    // Maximum operations per instruction (for VLIW, usually 1)
    out.push_back(1);
    
    // Default is_stmt
    out.push_back(1);
    
    // Line base (for special opcodes)
    out.push_back(static_cast<uint8_t>(-5));  // Signed
    
    // Line range (for special opcodes)
    out.push_back(14);
    
    // Opcode base
    out.push_back(13);
    
    // Standard opcode lengths
    out.push_back(0);  // DW_LNS_copy
    out.push_back(1);  // DW_LNS_advance_pc
    out.push_back(1);  // DW_LNS_advance_line
    out.push_back(1);  // DW_LNS_set_file
    out.push_back(1);  // DW_LNS_set_column
    out.push_back(0);  // DW_LNS_negate_stmt
    out.push_back(0);  // DW_LNS_set_basic_block
    out.push_back(0);  // DW_LNS_const_add_pc
    out.push_back(1);  // DW_LNS_fixed_advance_pc
    out.push_back(0);  // DW_LNS_set_prologue_end
    out.push_back(0);  // DW_LNS_set_epilogue_begin
    out.push_back(1);  // DW_LNS_set_isa
    
    // DWARF 5: Directory entry format count
    out.push_back(1);  // One format
    
    // Directory entry format: DW_LNCT_path, DW_FORM_string
    WriteULEB128(out, 1);  // DW_LNCT_path
    WriteULEB128(out, kDwFormString);
    
    // Directory count
    WriteULEB128(out, directories_.size() + 1);  // +1 for compilation directory
    
    // Compilation directory (index 0)
    out.push_back('.');
    out.push_back(0);
    
    // Additional directories
    for (const auto &dir : directories_) {
        out.insert(out.end(), dir.begin(), dir.end());
        out.push_back(0);
    }
    
    // DWARF 5: File name entry format count
    out.push_back(2);  // Two formats
    
    // File name entry format: DW_LNCT_path, DW_FORM_string
    WriteULEB128(out, 1);  // DW_LNCT_path
    WriteULEB128(out, kDwFormString);
    
    // File name entry format: DW_LNCT_directory_index, DW_FORM_udata
    WriteULEB128(out, 2);  // DW_LNCT_directory_index
    WriteULEB128(out, 0x0f);  // DW_FORM_udata
    
    // File count
    WriteULEB128(out, files_.size());
    
    // File entries
    for (const auto &file : files_) {
        out.insert(out.end(), file.filename.begin(), file.filename.end());
        out.push_back(0);
        WriteULEB128(out, file.dir_index);
    }
}

void DwarfSectionBuilder::EncodeLineStatements(
    std::vector<uint8_t> &out,
    const std::vector<DebugLineInfo> &lines) {
    
    if (lines.empty()) {
        // End sequence for empty program
        out.push_back(0);  // Extended opcode marker
        WriteULEB128(out, 1);  // Length
        out.push_back(kDwLneEndSequence);
        return;
    }
    
    // State machine registers
    uint64_t address = 0;
    uint32_t file = 1;
    int32_t line = 1;
    uint32_t column = 0;
    bool is_stmt = true;
    
    for (const auto &entry : lines) {
        uint32_t entry_file = GetOrAddFile(entry.file);
        
        // Set address using extended opcode
        if (address == 0) {
            out.push_back(0);  // Extended opcode marker
            WriteULEB128(out, 9);  // Length (1 + 8 bytes)
            out.push_back(kDwLneSetAddress);
            WriteLE<uint64_t>(out, 0);  // Address will be relocated
        }
        
        // Set file if changed
        if (entry_file != file) {
            out.push_back(kDwLnsSetFile);
            WriteULEB128(out, entry_file);
            file = entry_file;
        }
        
        // Set column if changed
        if (static_cast<uint32_t>(entry.column) != column) {
            out.push_back(kDwLnsSetColumn);
            WriteULEB128(out, entry.column);
            column = entry.column;
        }
        
        // Advance line
        int32_t line_delta = entry.line - line;
        if (line_delta != 0) {
            out.push_back(kDwLnsAdvanceLine);
            WriteSLEB128(out, line_delta);
            line = entry.line;
        }
        
        // Copy to emit row
        out.push_back(kDwLnsCopy);
        
        address++;  // Simplified address tracking
    }
    
    // End sequence
    out.push_back(0);  // Extended opcode marker
    WriteULEB128(out, 1);  // Length
    out.push_back(kDwLneEndSequence);
}

std::vector<uint8_t> DwarfSectionBuilder::BuildDebugLine(const DebugInfoBuilder &info) {
    std::vector<uint8_t> result;
    
    // Pre-populate files from line info
    for (const auto &line : info.Lines()) {
        GetOrAddFile(line.file);
    }
    
    // Reserve space for unit_length
    size_t unit_length_pos = result.size();
    WriteLE<uint32_t>(result, 0);  // Placeholder
    
    // DWARF version (5)
    WriteLE<uint16_t>(result, 5);
    
    // Address size
    result.push_back(8);
    
    // Segment selector size
    result.push_back(0);
    
    // Reserve space for header_length
    size_t header_length_pos = result.size();
    WriteLE<uint32_t>(result, 0);  // Placeholder
    
    size_t header_start = result.size();
    
    // Encode header
    EncodeLineHeader(result);
    
    // Patch header_length
    uint32_t header_length = static_cast<uint32_t>(result.size() - header_start);
    Patch32(result, header_length_pos, header_length);
    
    // Encode line number statements
    EncodeLineStatements(result, info.Lines());
    
    // Patch unit_length
    uint32_t unit_length = static_cast<uint32_t>(result.size() - unit_length_pos - 4);
    Patch32(result, unit_length_pos, unit_length);
    
    return result;
}

std::vector<uint8_t> DwarfSectionBuilder::BuildDebugFrame(const DebugInfoBuilder &info) {
    std::vector<uint8_t> result;
    
    // CIE (Common Information Entry)
    size_t cie_length_pos = result.size();
    WriteLE<uint32_t>(result, 0);  // Placeholder for length
    
    WriteLE<uint32_t>(result, 0xFFFFFFFF);  // CIE_id (distinguishes from FDE)
    
    result.push_back(4);  // Version
    
    // Augmentation string (empty)
    result.push_back(0);
    
    // Address size
    result.push_back(8);
    
    // Segment selector size
    result.push_back(0);
    
    // Code alignment factor
    WriteULEB128(result, 1);
    
    // Data alignment factor
    WriteSLEB128(result, -8);
    
    // Return address register (platform-specific, using x86-64 convention)
    WriteULEB128(result, 16);  // RIP
    
    // Initial instructions: define CFA and register save rules
    // DW_CFA_def_cfa: register RSP(7), offset 8
    result.push_back(0x0c);  // DW_CFA_def_cfa
    WriteULEB128(result, 7);  // RSP
    WriteULEB128(result, 8);  // Offset (return address pushed by call)
    
    // DW_CFA_offset: return address register (RIP=16) at CFA-8
    result.push_back(0x80 | 16);  // DW_CFA_offset for register 16 (RIP)
    WriteULEB128(result, 1);  // offset / data_alignment_factor = 8 / 8 = 1
    
    // DW_CFA_nop padding to align CIE
    while ((result.size() - cie_length_pos) % 8 != 0) {
        result.push_back(0);  // DW_CFA_nop
    }
    
    // Patch CIE length
    uint32_t cie_length = static_cast<uint32_t>(result.size() - cie_length_pos - 4);
    Patch32(result, cie_length_pos, cie_length);
    
    // Add FDEs for each function
    for (const auto &sym : info.Symbols()) {
        if (sym.is_function && sym.size > 0) {
            AlignTo(result, 8);
            
            size_t fde_length_pos = result.size();
            WriteLE<uint32_t>(result, 0);  // Placeholder for length
            
            WriteLE<uint32_t>(result, 0);  // CIE pointer (offset from here)
            
            // Initial location (address of function)
            WriteLE<uint64_t>(result, sym.address);
            
            // Address range (function size)
            WriteLE<uint64_t>(result, sym.size);

            // Call frame instructions for a standard frame-pointer prologue:
            //   push rbp          (1 byte)  → CFA at RSP+16, RBP saved at CFA-16
            //   mov  rbp, rsp     (3 bytes) → CFA defined by RBP+16 for the body
            //
            // DW_CFA_advance_loc(1): after push rbp (1 byte)
            result.push_back(0x41);         // DW_CFA_advance_loc + 1
            // DW_CFA_def_cfa_offset(16): stack pointer moved by 8 (call) + 8 (push)
            result.push_back(0x0E);         // DW_CFA_def_cfa_offset
            WriteULEB128(result, 16);
            // DW_CFA_offset(RBP=6, 2): RBP saved at CFA-16  (offset_factor = 16/8 = 2)
            result.push_back(0x80 | 6);     // DW_CFA_offset for register 6 (RBP)
            WriteULEB128(result, 2);

            // DW_CFA_advance_loc(3): after mov rbp, rsp (3 bytes)
            result.push_back(0x43);         // DW_CFA_advance_loc + 3
            // DW_CFA_def_cfa_register(RBP=6): CFA is now RBP+16
            result.push_back(0x0D);         // DW_CFA_def_cfa_register
            WriteULEB128(result, 6);
            
            // Patch FDE length
            uint32_t fde_length = static_cast<uint32_t>(result.size() - fde_length_pos - 4);
            Patch32(result, fde_length_pos, fde_length);
        }
    }
    
    return result;
}

std::vector<uint8_t> DwarfSectionBuilder::BuildDebugAranges(const DebugInfoBuilder &info) {
    std::vector<uint8_t> result;
    
    // Reserve space for length
    size_t length_pos = result.size();
    WriteLE<uint32_t>(result, 0);  // Placeholder
    
    // Version
    WriteLE<uint16_t>(result, 2);
    
    // Debug info offset
    WriteLE<uint32_t>(result, 0);
    
    // Address size
    result.push_back(8);
    
    // Segment selector size
    result.push_back(0);
    
    // Padding to align to 2*address_size
    AlignTo(result, 16);
    
    // Address ranges
    for (const auto &sym : info.Symbols()) {
        if (sym.is_function) {
            WriteLE<uint64_t>(result, sym.address);
            WriteLE<uint64_t>(result, sym.size);
        }
    }
    
    // Terminating entry
    WriteLE<uint64_t>(result, 0);
    WriteLE<uint64_t>(result, 0);
    
    // Patch length
    uint32_t length = static_cast<uint32_t>(result.size() - length_pos - 4);
    Patch32(result, length_pos, length);
    
    return result;
}

// ============================================================================
// PDB Section Builder (for Windows targets)
// ============================================================================

class PdbSectionBuilder {
public:
    PdbSectionBuilder() = default;
    
    // Build PDB file format
    std::vector<uint8_t> Build(const DebugInfoBuilder &info);
    
private:
    // PDB stream types
    static constexpr uint32_t kPdbStreamRoot = 0;
    static constexpr uint32_t kPdbStreamPdb = 1;
    static constexpr uint32_t kPdbStreamTpi = 2;
    static constexpr uint32_t kPdbStreamDbi = 3;
    static constexpr uint32_t kPdbStreamIpi = 4;
    
    // Build individual streams
    std::vector<uint8_t> BuildPdbInfoStream();
    std::vector<uint8_t> BuildTpiStream(const DebugInfoBuilder &info);
    std::vector<uint8_t> BuildDbiStream(const DebugInfoBuilder &info);
    std::vector<uint8_t> BuildSymbolStream(const DebugInfoBuilder &info);
    
    // GUID generation (simplified)
    void GenerateGuid(uint8_t guid[16]);
};

void PdbSectionBuilder::GenerateGuid(uint8_t guid[16]) {
    // Use a proper random device for GUID generation
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 255);
    
    for (int i = 0; i < 16; ++i) {
        guid[i] = static_cast<uint8_t>(dist(gen));
    }
    
    // Set version 4 (random) and variant 1 bits per RFC 4122
    guid[6] = (guid[6] & 0x0F) | 0x40;  // Version 4
    guid[8] = (guid[8] & 0x3F) | 0x80;  // Variant 1
}

std::vector<uint8_t> PdbSectionBuilder::BuildPdbInfoStream() {
    std::vector<uint8_t> result;
    
    // PDB Info Header
    WriteLE<uint32_t>(result, 20000404);  // Version (VC70)
    WriteLE<uint32_t>(result, 0);         // Signature (timestamp)
    WriteLE<uint32_t>(result, 1);         // Age
    
    // GUID
    uint8_t guid[16];
    GenerateGuid(guid);
    result.insert(result.end(), guid, guid + 16);
    
    return result;
}

std::vector<uint8_t> PdbSectionBuilder::BuildTpiStream(const DebugInfoBuilder &info) {
    std::vector<uint8_t> result;
    
    // Build type records first to know their total size
    std::vector<uint8_t> type_records;
    uint32_t type_index_end = 0x1000;  // Start at min type index
    
    // Emit LF_ARGLIST (0x1201) for each function — empty arg list as baseline
    {
        uint16_t rec_len = 6;  // 2 kind + 4 count
        WriteLE<uint16_t>(type_records, rec_len);
        WriteLE<uint16_t>(type_records, 0x1201);  // LF_ARGLIST
        WriteLE<uint32_t>(type_records, 0);        // argcount = 0
        type_index_end++;
    }

    // Emit LF_POINTER (0x1002) for pointer types discovered in DebugType
    // and LF_MODIFIER (0x1001) for const-qualified types — this gives the
    // PDB consumer basic type layout information.
    for (const auto &dt : info.Types()) {
        if (dt.kind == "pointer") {
            uint16_t rec_len = 10;  // 2 kind + 4 referent + 4 attrs
            WriteLE<uint16_t>(type_records, rec_len);
            WriteLE<uint16_t>(type_records, 0x1002);  // LF_POINTER
            WriteLE<uint32_t>(type_records, 0x0074);   // referent: T_INT8 (char)
            // pointer attributes: 64-bit, near, plain
            WriteLE<uint32_t>(type_records, 0x0000000C);
            type_index_end++;
        }
    }
    
    // Emit LF_PROCEDURE (0x1008) for each function symbol
    for (const auto &sym : info.Symbols()) {
        if (sym.is_function) {
            uint16_t rec_len = 14;  // 2 kind + 4 ret + 1 callconv + 1 attrs + 2 parmcount + 4 arglist
            WriteLE<uint16_t>(type_records, rec_len);
            WriteLE<uint16_t>(type_records, 0x1008);  // LF_PROCEDURE
            WriteLE<uint32_t>(type_records, 0x0003);   // return type: T_VOID
            type_records.push_back(0);                  // calling convention: near C
            type_records.push_back(0);                  // attributes
            WriteLE<uint16_t>(type_records, 0);         // parameter count
            WriteLE<uint32_t>(type_records, 0x1000);    // arglist type index
            type_index_end++;
        }
    }
    
    uint32_t type_record_bytes = static_cast<uint32_t>(type_records.size());
    
    // TPI Stream Header (56 bytes)
    WriteLE<uint32_t>(result, 20040203);  // Version
    WriteLE<uint32_t>(result, 56);        // Header size
    WriteLE<uint32_t>(result, 0x1000);    // Type index begin
    WriteLE<uint32_t>(result, type_index_end);  // Type index end
    WriteLE<uint32_t>(result, type_record_bytes);  // Type record bytes
    
    // Hash stream index (-1 = not present)
    WriteLE<int16_t>(result, -1);
    WriteLE<int16_t>(result, -1);
    
    // Hash key size
    WriteLE<uint32_t>(result, 4);
    
    // Number of hash buckets
    WriteLE<uint32_t>(result, 0x3FFFF);
    
    // Hash value buffer offset/length
    WriteLE<int32_t>(result, 0);
    WriteLE<uint32_t>(result, 0);
    
    // Index offset buffer offset/length
    WriteLE<int32_t>(result, 0);
    WriteLE<uint32_t>(result, 0);
    
    // Hash adjustment buffer offset/length
    WriteLE<int32_t>(result, 0);
    WriteLE<uint32_t>(result, 0);
    
    // Append type records
    result.insert(result.end(), type_records.begin(), type_records.end());
    
    return result;
}

std::vector<uint8_t> PdbSectionBuilder::BuildDbiStream(const DebugInfoBuilder &info) {
    std::vector<uint8_t> result;
    
    // DBI Stream Header
    WriteLE<int32_t>(result, -1);         // Version signature
    WriteLE<uint32_t>(result, 19990903);  // Version
    WriteLE<uint32_t>(result, 1);         // Age
    WriteLE<int16_t>(result, -1);         // Global symbols stream index
    WriteLE<uint16_t>(result, 0x8E1D);    // Build number (internal version)
    WriteLE<int16_t>(result, -1);         // Public symbols stream index
    WriteLE<uint16_t>(result, 0);         // PDB DLL version
    WriteLE<int16_t>(result, -1);         // Symbol records stream index
    WriteLE<uint16_t>(result, 0);         // PDB DLL rebuild version
    
    // Module info size
    WriteLE<int32_t>(result, 0);
    
    // Section contribution size
    WriteLE<int32_t>(result, 0);
    
    // Section map size
    WriteLE<int32_t>(result, 0);
    
    // Source info size
    WriteLE<int32_t>(result, 0);
    
    // Type server map size
    WriteLE<int32_t>(result, 0);
    
    // MFC type server index
    WriteLE<uint32_t>(result, 0);
    
    // Optional debug header size
    WriteLE<int32_t>(result, 0);
    
    // EC substream size
    WriteLE<int32_t>(result, 0);
    
    // Flags
    WriteLE<uint16_t>(result, 0);
    
    // Machine type (x64)
    WriteLE<uint16_t>(result, 0x8664);
    
    // Padding
    WriteLE<uint32_t>(result, 0);
    
    return result;
}

std::vector<uint8_t> PdbSectionBuilder::BuildSymbolStream(const DebugInfoBuilder &info) {
    std::vector<uint8_t> result;
    
    // Symbol records for functions and variables
    for (const auto &sym : info.Symbols()) {
        if (sym.is_function) {
            // S_GPROC32_ID (global procedure)
            size_t start_pos = result.size();
            WriteLE<uint16_t>(result, 0);  // Record length (patched later)
            WriteLE<uint16_t>(result, 0x1147);  // S_GPROC32_ID
            
            WriteLE<uint32_t>(result, 0);  // Parent
            WriteLE<uint32_t>(result, 0);  // End
            WriteLE<uint32_t>(result, 0);  // Next
            WriteLE<uint32_t>(result, static_cast<uint32_t>(sym.size));  // Proc length
            WriteLE<uint32_t>(result, 0);  // Debug start
            WriteLE<uint32_t>(result, static_cast<uint32_t>(sym.size));  // Debug end
            WriteLE<uint32_t>(result, 0x1000);  // Type index
            WriteLE<uint32_t>(result, static_cast<uint32_t>(sym.address));  // Offset
            WriteLE<uint16_t>(result, 1);  // Segment
            result.push_back(0);  // Flags
            
            // Name (null-terminated)
            result.insert(result.end(), sym.name.begin(), sym.name.end());
            result.push_back(0);
            
            // Align to 4 bytes
            while (result.size() % 4 != 0) {
                result.push_back(0);
            }
            
            // Patch record length
            uint16_t record_length = static_cast<uint16_t>(result.size() - start_pos - 2);
            result[start_pos] = record_length & 0xFF;
            result[start_pos + 1] = (record_length >> 8) & 0xFF;
        }
    }
    
    return result;
}

std::vector<uint8_t> PdbSectionBuilder::Build(const DebugInfoBuilder &info) {
    std::vector<uint8_t> result;
    
    // Build streams
    std::vector<uint8_t> pdb_info = BuildPdbInfoStream();
    std::vector<uint8_t> tpi = BuildTpiStream(info);
    std::vector<uint8_t> dbi = BuildDbiStream(info);
    std::vector<uint8_t> symbols = BuildSymbolStream(info);
    
    // Collect all streams
    std::vector<std::vector<uint8_t>> streams = {
        {},           // Stream 0: old directory (empty)
        pdb_info,     // Stream 1: PDB info
        tpi,          // Stream 2: TPI
        dbi,          // Stream 3: DBI
        symbols       // Stream 4: Symbol records
    };
    
    // Calculate block assignments for each stream
    constexpr uint32_t kBlockSize = 4096;
    uint32_t next_block = 4;  // blocks 0-3 reserved (header, fpm1, fpm2, directory)
    
    struct StreamLayout {
        std::vector<uint32_t> blocks;
    };
    std::vector<StreamLayout> stream_layouts(streams.size());
    
    for (size_t si = 0; si < streams.size(); ++si) {
        uint32_t num_blocks = streams[si].empty() ? 0 :
            (static_cast<uint32_t>(streams[si].size()) + kBlockSize - 1) / kBlockSize;
        for (uint32_t b = 0; b < num_blocks; ++b) {
            stream_layouts[si].blocks.push_back(next_block++);
        }
    }
    
    // Build the stream directory
    std::vector<uint8_t> directory;
    WriteLE<uint32_t>(directory, static_cast<uint32_t>(streams.size()));  // Number of streams
    
    // Stream sizes
    for (const auto &stream : streams) {
        WriteLE<uint32_t>(directory, static_cast<uint32_t>(stream.size()));
    }
    
    // Stream block indices
    for (const auto &layout : stream_layouts) {
        for (uint32_t block : layout.blocks) {
            WriteLE<uint32_t>(directory, block);
        }
    }
    
    uint32_t total_blocks = next_block;
    
    // Build the final file
    result.clear();
    result.resize(static_cast<size_t>(total_blocks) * kBlockSize, 0);
    
    // Block 0: MSF Header
    const uint8_t magic[] = {
        'M', 'i', 'c', 'r', 'o', 's', 'o', 'f', 't', ' ',
        'C', '/', 'C', '+', '+', ' ', 'M', 'S', 'F', ' ',
        '7', '.', '0', '0', '\r', '\n', 0x1A, 'D', 'S',
        0, 0, 0
    };
    std::memcpy(result.data(), magic, 32);
    
    // MSF header fields (after magic)
    auto write_at = [&](size_t offset, uint32_t val) {
        for (int i = 0; i < 4; ++i) {
            result[offset + i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
        }
    };
    write_at(32, kBlockSize);          // Block size
    write_at(36, 1);                   // Free block map block (block 1)
    write_at(40, total_blocks);        // Number of blocks
    write_at(44, static_cast<uint32_t>(directory.size()));  // Directory byte size
    write_at(48, 0);                   // Unknown
    write_at(52, 3);                   // Block map address (directory is at block 3)
    
    // Block 1 & 2: Free block maps (mark all as allocated)
    std::memset(result.data() + kBlockSize, 0xFF, kBlockSize);
    std::memset(result.data() + 2 * kBlockSize, 0xFF, kBlockSize);
    
    // Block 3: Stream directory
    size_t copy_len = std::min(directory.size(), static_cast<size_t>(kBlockSize));
    std::memcpy(result.data() + 3 * kBlockSize, directory.data(), copy_len);
    
    // Write stream data to their assigned blocks
    for (size_t si = 0; si < streams.size(); ++si) {
        const auto &data = streams[si];
        const auto &layout = stream_layouts[si];
        size_t offset = 0;
        for (uint32_t block : layout.blocks) {
            size_t chunk = std::min(static_cast<size_t>(kBlockSize), data.size() - offset);
            std::memcpy(result.data() + static_cast<size_t>(block) * kBlockSize,
                       data.data() + offset, chunk);
            offset += chunk;
        }
    }
    
    return result;
}

}  // anonymous namespace

// ============================================================================
// Public API Implementation
// ============================================================================

bool DebugEmitter::EmitSourceMap(const DebugInfoBuilder &info, const std::string &path) {
    std::ofstream os(path, std::ios::out | std::ios::trunc);
    if (!os.is_open()) {
        return false;
    }
    os << info.EmitSourceMapJSON();
    return os.good();
}

bool DebugEmitter::EmitDWARF(const DebugInfoBuilder &info, const std::string &path) {
    DwarfSectionBuilder builder;
    
    // Build all DWARF sections
    std::vector<uint8_t> debug_str = builder.BuildDebugStr();
    std::vector<uint8_t> debug_abbrev = builder.BuildDebugAbbrev();
    std::vector<uint8_t> debug_info = builder.BuildDebugInfo(info);
    std::vector<uint8_t> debug_line = builder.BuildDebugLine(info);
    std::vector<uint8_t> debug_frame = builder.BuildDebugFrame(info);
    std::vector<uint8_t> debug_aranges = builder.BuildDebugAranges(info);
    
    // Rebuild debug_str after all strings have been collected during info building
    debug_str = builder.BuildDebugStr();
    
    // Prepare debug sections for ELF
    std::vector<DebugSection> sections;
    
    DebugSection str_section;
    str_section.name = ".debug_str";
    str_section.data = std::move(debug_str);
    sections.push_back(std::move(str_section));
    
    DebugSection abbrev_section;
    abbrev_section.name = ".debug_abbrev";
    abbrev_section.data = std::move(debug_abbrev);
    sections.push_back(std::move(abbrev_section));
    
    DebugSection info_section;
    info_section.name = ".debug_info";
    info_section.data = std::move(debug_info);
    sections.push_back(std::move(info_section));
    
    DebugSection line_section;
    line_section.name = ".debug_line";
    line_section.data = std::move(debug_line);
    sections.push_back(std::move(line_section));
    
    DebugSection frame_section;
    frame_section.name = ".debug_frame";
    frame_section.data = std::move(debug_frame);
    sections.push_back(std::move(frame_section));
    
    DebugSection aranges_section;
    aranges_section.name = ".debug_aranges";
    aranges_section.data = std::move(debug_aranges);
    sections.push_back(std::move(aranges_section));
    
    // Build ELF and write to file
    std::vector<uint8_t> elf_data = BuildELFObject(sections);
    
    std::ofstream os(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!os.is_open()) {
        return false;
    }
    
    os.write(reinterpret_cast<const char*>(elf_data.data()), 
             static_cast<std::streamsize>(elf_data.size()));
    
    return os.good();
}

bool DebugEmitter::EmitPDB(const DebugInfoBuilder &info, const std::string &path) {
    PdbSectionBuilder builder;
    
    // Build PDB file
    std::vector<uint8_t> pdb_data = builder.Build(info);
    
    // Write to file
    std::ofstream os(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!os.is_open()) {
        return false;
    }
    
    os.write(reinterpret_cast<const char*>(pdb_data.data()),
             static_cast<std::streamsize>(pdb_data.size()));
    
    return os.good();
}

}  // namespace polyglot::backends
