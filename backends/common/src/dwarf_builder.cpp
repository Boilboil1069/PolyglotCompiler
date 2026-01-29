#include "backends/common/include/dwarf_builder.h"

#include <algorithm>

namespace polyglot::backends {

void DwarfBuilder::AddDirectory(const std::string &path) {
    directories_.push_back(path);
}

std::uint32_t DwarfBuilder::AddFile(const std::string &name, std::uint32_t dir_index) {
    DwarfFile file;
    file.name = name;
    file.directory_index = dir_index;
    file.mod_time = 0;
    file.file_size = 0;
    
    files_.push_back(file);
    return static_cast<std::uint32_t>(files_.size());
}

void DwarfBuilder::AddLineEntry(const DwarfLineEntry &entry) {
    line_entries_.push_back(entry);
}

void DwarfBuilder::AddSubprogram(const DwarfSubprogram &subprogram) {
    subprograms_.push_back(subprogram);
}

std::uint32_t DwarfBuilder::AddString(const std::string &str) {
    auto it = string_table_.find(str);
    if (it != string_table_.end()) {
        return it->second;
    }
    
    std::uint32_t offset = 0;
    for (const auto &pair : string_table_) {
        offset += static_cast<std::uint32_t>(pair.first.size()) + 1;
    }
    
    string_table_[str] = offset;
    return offset;
}

void DwarfBuilder::WriteULEB128(std::vector<std::uint8_t> &out, std::uint64_t value) {
    do {
        std::uint8_t byte = value & 0x7F;
        value >>= 7;
        if (value != 0) {
            byte |= 0x80;
        }
        out.push_back(byte);
    } while (value != 0);
}

void DwarfBuilder::WriteSLEB128(std::vector<std::uint8_t> &out, std::int64_t value) {
    bool more = true;
    while (more) {
        std::uint8_t byte = value & 0x7F;
        value >>= 7;
        
        if ((value == 0 && !(byte & 0x40)) || (value == -1 && (byte & 0x40))) {
            more = false;
        } else {
            byte |= 0x80;
        }
        
        out.push_back(byte);
    }
}

std::vector<std::uint8_t> DwarfBuilder::BuildDebugAbbrev() {
    std::vector<std::uint8_t> result;
    
    // Abbreviation 1: DW_TAG_compile_unit
    WriteULEB128(result, 1);  // abbrev code
    WriteULEB128(result, 0x11);  // DW_TAG_compile_unit
    result.push_back(1);  // DW_CHILDREN_yes
    
    WriteULEB128(result, 0x25);  // DW_AT_producer
    WriteULEB128(result, 0x0E);  // DW_FORM_strp
    
    WriteULEB128(result, 0x13);  // DW_AT_language
    WriteULEB128(result, 0x0B);  // DW_FORM_data1
    
    WriteULEB128(result, 0x03);  // DW_AT_name
    WriteULEB128(result, 0x0E);  // DW_FORM_strp
    
    WriteULEB128(result, 0x11);  // DW_AT_low_pc
    WriteULEB128(result, 0x01);  // DW_FORM_addr
    
    WriteULEB128(result, 0x12);  // DW_AT_high_pc
    WriteULEB128(result, 0x01);  // DW_FORM_addr
    
    WriteULEB128(result, 0);  // End attributes
    WriteULEB128(result, 0);
    
    // Abbreviation 2: DW_TAG_subprogram
    WriteULEB128(result, 2);  // abbrev code
    WriteULEB128(result, 0x2E);  // DW_TAG_subprogram
    result.push_back(0);  // DW_CHILDREN_no
    
    WriteULEB128(result, 0x03);  // DW_AT_name
    WriteULEB128(result, 0x0E);  // DW_FORM_strp
    
    WriteULEB128(result, 0x11);  // DW_AT_low_pc
    WriteULEB128(result, 0x01);  // DW_FORM_addr
    
    WriteULEB128(result, 0x12);  // DW_AT_high_pc
    WriteULEB128(result, 0x01);  // DW_FORM_addr
    
    WriteULEB128(result, 0x3F);  // DW_AT_external
    WriteULEB128(result, 0x0C);  // DW_FORM_flag
    
    WriteULEB128(result, 0);  // End attributes
    WriteULEB128(result, 0);
    
    // End abbreviation table
    result.push_back(0);
    
    return result;
}

std::vector<std::uint8_t> DwarfBuilder::BuildDebugStr() {
    std::vector<std::uint8_t> result;
    result.push_back(0);  // Null string at offset 0
    
    // Sort strings by offset
    std::vector<std::pair<std::string, std::uint32_t>> sorted_strings(
        string_table_.begin(), string_table_.end());
    std::sort(sorted_strings.begin(), sorted_strings.end(),
              [](const auto &a, const auto &b) { return a.second < b.second; });
    
    for (const auto &pair : sorted_strings) {
        result.insert(result.end(), pair.first.begin(), pair.first.end());
        result.push_back(0);
    }
    
    return result;
}

std::vector<std::uint8_t> DwarfBuilder::BuildDebugInfo() {
    std::vector<std::uint8_t> result;
    
    // Compilation unit header
    std::size_t length_pos = result.size();
    for (int i = 0; i < 4; ++i) result.push_back(0);  // Length (filled later)
    
    result.push_back(4);  // DWARF version
    result.push_back(0);
    
    for (int i = 0; i < 4; ++i) result.push_back(0);  // Debug abbrev offset
    
    result.push_back(8);  // Address size
    
    // DIE: Compile unit
    WriteULEB128(result, 1);  // abbrev code 1
    
    std::uint32_t producer_offset = AddString("PolyglotCompiler");
    for (int i = 0; i < 4; ++i) {
        result.push_back((producer_offset >> (i * 8)) & 0xFF);
    }
    
    result.push_back(4);  // DW_LANG_C_plus_plus
    
    std::uint32_t name_offset = AddString("test.cpp");
    for (int i = 0; i < 4; ++i) {
        result.push_back((name_offset >> (i * 8)) & 0xFF);
    }
    
    // Low PC / High PC
    for (int i = 0; i < 8; ++i) result.push_back(0);
    for (int i = 0; i < 8; ++i) result.push_back(0);
    
    // DIEs for subprograms
    for (const auto &sub : subprograms_) {
        WriteULEB128(result, 2);  // abbrev code 2
        
        std::uint32_t func_name_offset = AddString(sub.name);
        for (int i = 0; i < 4; ++i) {
            result.push_back((func_name_offset >> (i * 8)) & 0xFF);
        }
        
        // Low PC
        for (int i = 0; i < 8; ++i) {
            result.push_back((sub.low_pc >> (i * 8)) & 0xFF);
        }
        
        // High PC
        for (int i = 0; i < 8; ++i) {
            result.push_back((sub.high_pc >> (i * 8)) & 0xFF);
        }
        
        result.push_back(sub.is_external ? 1 : 0);  // DW_AT_external
    }
    
    result.push_back(0);  // End children
    
    // Fill in length
    std::uint32_t length = static_cast<std::uint32_t>(result.size() - 4);
    for (int i = 0; i < 4; ++i) {
        result[length_pos + i] = (length >> (i * 8)) & 0xFF;
    }
    
    return result;
}

std::vector<std::uint8_t> DwarfBuilder::BuildDebugLine() {
    std::vector<std::uint8_t> result;
    
    // Line number program header
    std::size_t length_pos = result.size();
    for (int i = 0; i < 4; ++i) result.push_back(0);  // Length
    
    result.push_back(4);  // Version
    result.push_back(0);
    
    std::size_t header_length_pos = result.size();
    for (int i = 0; i < 4; ++i) result.push_back(0);  // Header length
    
    result.push_back(1);  // Minimum instruction length
    result.push_back(1);  // Default is_stmt
    result.push_back(static_cast<std::uint8_t>(-5));  // Line base
    result.push_back(14);  // Line range
    result.push_back(13);  // Opcode base
    
    // Standard opcode lengths
    std::uint8_t std_opcode_lengths[] = {0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1};
    for (auto len : std_opcode_lengths) {
        result.push_back(len);
    }
    
    // Include directories
    for (const auto &dir : directories_) {
        result.insert(result.end(), dir.begin(), dir.end());
        result.push_back(0);
    }
    result.push_back(0);  // End directories
    
    // File names
    for (const auto &file : files_) {
        result.insert(result.end(), file.name.begin(), file.name.end());
        result.push_back(0);
        WriteULEB128(result, file.directory_index);
        WriteULEB128(result, file.mod_time);
        WriteULEB128(result, file.file_size);
    }
    result.push_back(0);  // End files
    
    std::uint32_t header_length = static_cast<std::uint32_t>(
        result.size() - header_length_pos - 4);
    for (int i = 0; i < 4; ++i) {
        result[header_length_pos + i] = (header_length >> (i * 8)) & 0xFF;
    }
    
    // Line number program (simplified)
    for (const auto &entry : line_entries_) {
        // DW_LNE_set_address
        result.push_back(0);  // Extended opcode
        WriteULEB128(result, 9);  // Length
        result.push_back(2);  // DW_LNE_set_address
        for (int i = 0; i < 8; ++i) {
            result.push_back((entry.address >> (i * 8)) & 0xFF);
        }
        
        // Set file
        result.push_back(4);  // DW_LNS_set_file
        WriteULEB128(result, entry.file_index);
        
        // Advance line
        result.push_back(3);  // DW_LNS_advance_line
        WriteSLEB128(result, entry.line);
        
        // Copy row
        result.push_back(1);  // DW_LNS_copy
    }
    
    // End sequence
    result.push_back(0);  // Extended opcode
    WriteULEB128(result, 1);  // Length
    result.push_back(1);  // DW_LNE_end_sequence
    
    // Fill in total length
    std::uint32_t length = static_cast<std::uint32_t>(result.size() - 4);
    for (int i = 0; i < 4; ++i) {
        result[length_pos + i] = (length >> (i * 8)) & 0xFF;
    }
    
    return result;
}

} // namespace polyglot::backends
