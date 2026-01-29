#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace polyglot::backends {

// Symbol entry for object files
struct Symbol {
    std::string name;
    std::string section;    // Section this symbol belongs to
    std::uint64_t offset;   // Offset within section
    std::uint64_t size;     // Symbol size (for functions/data)
    bool is_global;         // Global visibility
    bool is_function;       // Function vs data
};

// Relocation entry
struct Relocation {
    std::uint64_t offset;   // Offset within section to apply relocation
    std::string symbol;     // Symbol name to relocate against
    int type;               // Relocation type (platform-specific)
    std::int64_t addend;    // Addend for RELA relocations
};

// Section data
struct Section {
    std::string name;
    std::vector<std::uint8_t> data;
    std::vector<Relocation> relocations;
    std::uint64_t address{0};  // Virtual address (for executables)
    std::uint32_t flags{0};    // Section flags
};

// Object file builder
class ObjectFileBuilder {
public:
    virtual ~ObjectFileBuilder() = default;
    
    virtual void AddSection(const Section &section) = 0;
    virtual void AddSymbol(const Symbol &symbol) = 0;
    virtual std::vector<std::uint8_t> Build() = 0;
};

// ELF object file builder
class ELFBuilder : public ObjectFileBuilder {
public:
    explicit ELFBuilder(bool is_x64 = true) : is_x64_(is_x64) {}
    
    void AddSection(const Section &section) override;
    void AddSymbol(const Symbol &symbol) override;
    std::vector<std::uint8_t> Build() override;

private:
    bool is_x64_;
    std::vector<Section> sections_;
    std::vector<Symbol> symbols_;
};

// Mach-O object file builder
class MachOBuilder : public ObjectFileBuilder {
public:
    explicit MachOBuilder(bool is_arm64 = false) : is_arm64_(is_arm64) {}
    
    void AddSection(const Section &section) override;
    void AddSymbol(const Symbol &symbol) override;
    std::vector<std::uint8_t> Build() override;

private:
    bool is_arm64_;
    std::vector<Section> sections_;
    std::vector<Symbol> symbols_;
};

} // namespace polyglot::backends
