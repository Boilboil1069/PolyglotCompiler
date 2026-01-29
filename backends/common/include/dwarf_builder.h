#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace polyglot::backends {

// DWARF debugging information structures
struct DwarfLineEntry {
    std::uint64_t address;
    std::uint32_t file_index;
    std::uint32_t line;
    std::uint32_t column;
    bool is_stmt;
    bool basic_block;
    bool end_sequence;
};

struct DwarfFile {
    std::string name;
    std::uint32_t directory_index;
    std::uint64_t mod_time;
    std::uint64_t file_size;
};

struct DwarfSubprogram {
    std::string name;
    std::uint64_t low_pc;
    std::uint64_t high_pc;
    std::uint32_t file;
    std::uint32_t line;
    bool is_external;
    std::vector<std::string> parameters;
};

// DWARF Debug Information Builder
class DwarfBuilder {
public:
    DwarfBuilder() = default;
    
    // File and directory management
    void AddDirectory(const std::string &path);
    std::uint32_t AddFile(const std::string &name, std::uint32_t dir_index = 0);
    
    // Line information
    void AddLineEntry(const DwarfLineEntry &entry);
    
    // Function information
    void AddSubprogram(const DwarfSubprogram &subprogram);
    
    // Generate DWARF sections
    std::vector<std::uint8_t> BuildDebugInfo();
    std::vector<std::uint8_t> BuildDebugLine();
    std::vector<std::uint8_t> BuildDebugAbbrev();
    std::vector<std::uint8_t> BuildDebugStr();

private:
    std::vector<std::string> directories_;
    std::vector<DwarfFile> files_;
    std::vector<DwarfLineEntry> line_entries_;
    std::vector<DwarfSubprogram> subprograms_;
    std::unordered_map<std::string, std::uint32_t> string_table_;
    
    std::uint32_t AddString(const std::string &str);
    void WriteULEB128(std::vector<std::uint8_t> &out, std::uint64_t value);
    void WriteSLEB128(std::vector<std::uint8_t> &out, std::int64_t value);
};

} // namespace polyglot::backends
