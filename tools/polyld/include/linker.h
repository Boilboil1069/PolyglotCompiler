#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace polyglot::linker {

// Symbol information
struct Symbol {
    std::string name;
    std::string section;
    std::uint64_t offset;
    std::uint64_t size;
    bool is_global;
    bool is_defined;
    int object_file_index{-1};  // Which object file defines this symbol
};

// Relocation entry
struct Relocation {
    std::uint64_t offset;
    std::string symbol;
    int type;
    std::int64_t addend;
    std::string section;
};

// Section from object file
struct InputSection {
    std::string name;
    std::vector<std::uint8_t> data;
    std::uint64_t alignment{1};
    std::uint64_t flags{0};
    int object_file_index{-1};
};

// Object file representation
struct ObjectFile {
    std::string path;
    std::vector<Symbol> symbols;
    std::vector<InputSection> sections;
    std::vector<Relocation> relocations;
};

// Linked output section
struct OutputSection {
    std::string name;
    std::uint64_t virtual_address{0};
    std::uint64_t file_offset{0};
    std::vector<std::uint8_t> data;
    std::uint64_t flags{0};
    std::uint64_t alignment{16};
};

// Linker configuration
struct LinkerConfig {
    std::vector<std::string> input_files;
    std::string output_file{"a.out"};
    std::uint64_t base_address{0x400000};  // Default base for x86_64
    bool emit_executable{true};
    bool strip_debug{false};
    std::string entry_point{"_start"};
    std::vector<std::string> library_paths;
};

// Main linker class
class Linker {
public:
    explicit Linker(const LinkerConfig &config);
    
    // Load object files
    bool LoadObjectFiles();
    
    // Resolve symbols
    bool ResolveSymbols();
    
    // Layout sections and assign addresses
    bool LayoutSections();
    
    // Apply relocations
    bool ApplyRelocations();
    
    // Generate executable
    bool GenerateExecutable();
    
    // Run complete link process
    bool Link();
    
    // Get diagnostics
    const std::vector<std::string> &GetErrors() const { return errors_; }
    const std::vector<std::string> &GetWarnings() const { return warnings_; }

private:
    LinkerConfig config_;
    std::vector<ObjectFile> objects_;
    std::unordered_map<std::string, Symbol> symbol_table_;
    std::vector<OutputSection> output_sections_;
    std::vector<std::string> errors_;
    std::vector<std::string> warnings_;
    
    bool LoadELF(const std::string &path, ObjectFile &obj);
    bool LoadMachO(const std::string &path, ObjectFile &obj);
    void ReportError(const std::string &msg);
    void ReportWarning(const std::string &msg);
    std::uint64_t AlignTo(std::uint64_t value, std::uint64_t align);
};

} // namespace polyglot::linker
