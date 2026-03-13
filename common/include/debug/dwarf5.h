#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>

namespace polyglot::debug {

// DWARF 5 Debug Information Generator
// Generates comprehensive debug information for source-level debugging

// DWARF encoding constants
namespace dwarf {
    // Debug sections
    constexpr const char* kDebugInfo = ".debug_info";
    constexpr const char* kDebugLine = ".debug_line";
    constexpr const char* kDebugAbbrev = ".debug_abbrev";
    constexpr const char* kDebugStr = ".debug_str";
    constexpr const char* kDebugRanges = ".debug_ranges";
    constexpr const char* kDebugLoc = ".debug_loc";
    constexpr const char* kDebugFrame = ".debug_frame";
    constexpr const char* kDebugLineStr = ".debug_line_str";
    
    // DWARF 5 version
    constexpr uint16_t kDwarf5Version = 5;
    
    // Tag encoding
    enum class Tag : uint16_t {
        kCompileUnit = 0x11,
        kSubprogram = 0x2e,
        kVariable = 0x34,
        kFormalParameter = 0x05,
        kLexicalBlock = 0x0b,
        kBaseType = 0x24,
        kPointerType = 0x0f,
        kReferenceType = 0x10,
        kStructType = 0x13,
        kClassType = 0x02,
        kUnionType = 0x17,
        kMember = 0x0d,
        kInheritance = 0x1c,
        kTemplateTypeParameter = 0x2f,
        kTemplateValueParameter = 0x30,
        kNamespace = 0x39,
        kEnumerationType = 0x04,
        kEnumerator = 0x28,
        kTypedef = 0x16,
        kArrayType = 0x01,
        kSubrangeType = 0x21,
        kConstType = 0x26,
        kVolatileType = 0x35,
        kRvalueReferenceType = 0x42,
    };
    
    // Attribute encoding
    enum class Attribute : uint16_t {
        kName = 0x03,
        kType = 0x49,
        kByteSize = 0x0b,
        kEncoding = 0x3e,
        kDeclaration = 0x3c,
        kLowPC = 0x11,
        kHighPC = 0x12,
        kFrameBase = 0x40,
        kLocation = 0x02,
        kCompDir = 0x1b,
        kProducer = 0x25,
        kLanguage = 0x13,
        kLineNumber = 0x3b,
        kStmtList = 0x10,
        kRanges = 0x55,
        kCallLine = 0x59,
        kCallFile = 0x58,
        kExternal = 0x3f,
        kDataMemberLocation = 0x38,
        kAccessibility = 0x32,
        kVirtuality = 0x4c,
        kVtableElemLocation = 0x4d,
        kConstValue = 0x1c,
        kUpperBound = 0x2f,
    };
    
    // Form encoding
    enum class Form : uint8_t {
        kAddr = 0x01,
        kBlock1 = 0x0a,
        kBlock2 = 0x03,
        kBlock4 = 0x04,
        kData1 = 0x0b,
        kData2 = 0x05,
        kData4 = 0x06,
        kData8 = 0x07,
        kString = 0x08,
        kStrp = 0x0e,
        kFlag = 0x0c,
        kLineStrp = 0x1f,
        kSecOffset = 0x17,
        kExprloc = 0x18,
        kRef4 = 0x13,
        kStrx = 0x1a,
        kAddrx = 0x1b,
    };
    
    // Base type encoding
    enum class BaseTypeEncoding : uint8_t {
        kAddress = 0x01,
        kBoolean = 0x02,
        kFloat = 0x04,
        kSigned = 0x05,
        kSignedChar = 0x06,
        kUnsigned = 0x07,
        kUnsignedChar = 0x08,
    };
    
    // Language encoding (DWARF 5)
    enum class Language : uint16_t {
        kC = 0x0c,
        kCpp14 = 0x21,
        kCpp17 = 0x22,
        kCpp20 = 0x23,
        kPython = 0x14,
        kRust = 0x1c,
    };
}

// Source location information
struct SourceLocation {
    std::string file;
    uint32_t line;
    uint32_t column;
    
    SourceLocation() : line(0), column(0) {}
    SourceLocation(const std::string& f, uint32_t l, uint32_t c = 0)
        : file(f), line(l), column(c) {}
};

// Variable location expression (DWARF expression)
class LocationExpr {
public:
    enum class Op : uint8_t {
        kReg = 0x50,         // DW_OP_reg0..31
        kBReg = 0x70,        // DW_OP_breg0..31 (base register + offset)
        kFBReg = 0x91,       // DW_OP_fbreg (frame base + offset)
        kAddr = 0x03,        // DW_OP_addr
        kConst1u = 0x08,     // DW_OP_const1u
        kConst2u = 0x0a,     // DW_OP_const2u
        kConst4u = 0x0c,     // DW_OP_const4u
        kPlus = 0x22,        // DW_OP_plus
        kDeref = 0x06,       // DW_OP_deref
    };
    
    void AddOp(Op op, int64_t operand = 0);
    std::vector<uint8_t> Encode() const;
    
private:
    std::vector<std::pair<Op, int64_t>> operations_;
};

// Debug Information Entry (DIE)
class DIE {
public:
    explicit DIE(dwarf::Tag tag) : tag_(tag), offset_(0) {}
    
    void SetAttribute(dwarf::Attribute attr, dwarf::Form form, uint64_t value);
    void SetAttribute(dwarf::Attribute attr, const std::string& value);
    void SetAttribute(dwarf::Attribute attr, const LocationExpr& expr);
    
    void AddChild(std::unique_ptr<DIE> child);
    
    dwarf::Tag GetTag() const { return tag_; }
    uint32_t GetOffset() const { return offset_; }
    void SetOffset(uint32_t offset) { offset_ = offset; }
    
    const std::vector<std::unique_ptr<DIE>>& GetChildren() const { return children_; }
    
    std::vector<uint8_t> Encode() const;
    
private:
    struct AttributeValue {
        dwarf::Attribute attr;
        dwarf::Form form;
        uint64_t numeric_value;
        std::string string_value;
        std::vector<uint8_t> expr_value;
    };
    
    dwarf::Tag tag_;
    uint32_t offset_;
    std::vector<AttributeValue> attributes_;
    std::vector<std::unique_ptr<DIE>> children_;
};

// Line number program for .debug_line
class LineNumberProgram {
public:
    LineNumberProgram();
    
    void AddFile(const std::string& filename, const std::string& directory);
    void AddLine(uint64_t address, const SourceLocation& loc);
    void SetEndSequence(uint64_t address);
    
    std::vector<uint8_t> Encode() const;
    
private:
    struct FileEntry {
        std::string filename;
        uint32_t directory_index;
    };
    
    struct LineEntry {
        uint64_t address;
        uint32_t file_index;
        uint32_t line;
        uint32_t column;
        bool is_stmt;
        bool end_sequence;
    };
    
    std::vector<std::string> directories_;
    std::vector<FileEntry> files_;
    std::vector<LineEntry> lines_;
    
    // Lookup file index by filepath (returns 1-based index, or default if not found)
    uint32_t LookupFileIndex(const std::string& filepath) const;
    
    // Helper methods for LEB128 encoding
    void EncodeULEB128(std::vector<uint8_t>& out, uint64_t value) const;
    void EncodeSLEB128(std::vector<uint8_t>& out, int64_t value) const;
};

// DWARF 5 debug information builder
class DwarfBuilder {
public:
    DwarfBuilder();
    
    // Set compilation unit information
    void SetCompileUnit(const std::string& filename,
                       const std::string& directory,
                       const std::string& producer,
                       dwarf::Language language);
    
    // Add type information
    DIE* AddBaseType(const std::string& name, uint32_t byte_size,
                     dwarf::BaseTypeEncoding encoding);
    DIE* AddPointerType(DIE* pointee_type);
    DIE* AddStructType(const std::string& name, uint32_t byte_size);
    DIE* AddClassType(const std::string& name, uint32_t byte_size);
    DIE* AddArrayType(DIE* element_type, uint32_t size);
    DIE* AddEnumType(const std::string& name, DIE* base_type);
    DIE* AddTypedef(const std::string& name, DIE* base_type);
    DIE* AddConstType(DIE* base_type);
    DIE* AddVolatileType(DIE* base_type);
    DIE* AddReferenceType(DIE* pointee_type, bool is_rvalue = false);
    DIE* AddUnionType(const std::string& name, uint32_t byte_size);
    DIE* AddMember(DIE* struct_type, const std::string& name,
                   DIE* type, uint32_t offset);
    void AddEnumerator(DIE* enum_type, const std::string& name, int64_t value);
    
    // Template support
    DIE* AddTemplateTypeParameter(DIE* parent, const std::string& name, DIE* type);
    DIE* AddTemplateValueParameter(DIE* parent, const std::string& name, 
                                  DIE* type, const std::string& value);
    
    // Namespace support
    DIE* AddNamespace(const std::string& name);
    void SetCurrentNamespace(DIE* ns);
    
    // Add function information
    DIE* AddSubprogram(const std::string& name,
                      DIE* return_type,
                      uint64_t low_pc,
                      uint64_t high_pc,
                      const SourceLocation& loc);
    void AddParameter(DIE* subprogram,
                     const std::string& name,
                     DIE* type,
                     const LocationExpr& location);
    void AddLocalVariable(DIE* subprogram,
                         const std::string& name,
                         DIE* type,
                         const LocationExpr& location,
                         const SourceLocation& loc);
    
    // Add line number information
    void AddLineEntry(uint64_t address, const SourceLocation& loc);
    
    // Generate debug sections
    std::unordered_map<std::string, std::vector<uint8_t>> GenerateSections();
    
private:
    std::unique_ptr<DIE> compile_unit_;
    LineNumberProgram line_program_;
    std::vector<DIE*> all_dies_;  // For reference lookup
    DIE* current_namespace_;      // Current namespace for type additions
    
    // String table for .debug_str
    std::unordered_map<std::string, uint32_t> string_table_;
    std::vector<uint8_t> string_data_;
    
    uint32_t AddString(const std::string& str);
    std::vector<uint8_t> EncodeDebugInfo();
    std::vector<uint8_t> EncodeDebugAbbrev();
    std::vector<uint8_t> EncodeDebugStr();
    std::vector<uint8_t> EncodeDebugLine();
};

// High-level debug info generator
class DebugInfoGenerator {
public:
    DebugInfoGenerator();
    
    void SetSourceLanguage(dwarf::Language lang) { language_ = lang; }
    void SetCompilationDirectory(const std::string& dir) { comp_dir_ = dir; }
    void SetProducer(const std::string& producer) { producer_ = producer; }
    
    // Register basic types
    void RegisterType(const std::string& name, uint32_t size,
                     dwarf::BaseTypeEncoding encoding);
    
    // Function debugging
    void BeginFunction(const std::string& name,
                      const std::string& return_type,
                      uint64_t start_address,
                      const SourceLocation& loc);
    void AddFunctionParameter(const std::string& name,
                             const std::string& type,
                             int frame_offset);
    void AddLocalVariable(const std::string& name,
                         const std::string& type,
                         int frame_offset,
                         const SourceLocation& loc);
    void EndFunction(uint64_t end_address);
    
    // Line mapping
    void AddSourceLine(uint64_t address, const SourceLocation& loc);
    
    // Generate sections
    std::unordered_map<std::string, std::vector<uint8_t>> Generate();
    
private:
    DwarfBuilder builder_;
    dwarf::Language language_;
    std::string comp_dir_;
    std::string producer_;
    
    std::unordered_map<std::string, DIE*> type_map_;
    DIE* current_function_;
};

}  // namespace polyglot::debug
