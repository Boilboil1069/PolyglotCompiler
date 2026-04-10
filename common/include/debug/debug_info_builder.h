/**
 * @file     debug_info_builder.h
 * @brief    Debug information generation utilities
 *
 * @ingroup  Common / Debug
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
/**
 * 调试信息构建器 - 增强版
 * 
 * 完整支持：
 * - DWARF 5调试信息
 * - 源码级调试
 * - 变量位置跟踪
 * - 内联调试信息
 * - 优化代码调试
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <optional>
#include <unordered_map>

#include "common/include/debug/dwarf5.h"

namespace polyglot::debug {

// SourceLocation is defined in dwarf5.h — reuse it here.
// Convenience alias for ranges:

/** @brief SourceRange data structure. */
struct SourceRange {
    SourceLocation begin;
    SourceLocation end;
};

/** @name 类型调试信息 */
/** @{ */

/** @brief DIType class. */
class DIType {
public:
    /** @brief Kind enumeration. */
    enum class Kind {
        Basic,      // int, float, etc.
        Pointer,
        Reference,
        Array,
        Struct,
        Class,
        Union,
        Enum,
        Function,
        Typedef,
        Const,
        Volatile
    };
    
    virtual ~DIType() = default;
    virtual Kind GetKind() const = 0;
    virtual size_t GetSize() const = 0;
    virtual size_t GetAlignment() const = 0;
    virtual std::string GetName() const = 0;
};

/** @brief DIBasicType class. */
class DIBasicType : public DIType {
public:
    /** @brief Encoding enumeration. */
    enum class Encoding {
        Signed,
        Unsigned,
        Float,
        Bool,
        Char
    };
    
    DIBasicType(const std::string& name, size_t size, Encoding encoding)
        : name_(name), size_(size), encoding_(encoding) {}
    
    Kind GetKind() const override { return Kind::Basic; }
    size_t GetSize() const override { return size_; }
    size_t GetAlignment() const override { return size_; }
    std::string GetName() const override { return name_; }
    
    Encoding GetEncoding() const { return encoding_; }
    
private:
    std::string name_;
    size_t size_;
    Encoding encoding_;
};

/** @brief DICompositeType class. */
class DICompositeType : public DIType {
public:
    /** @brief Member data structure. */
    struct Member {
        std::string name;
        DIType* type;
        size_t offset;
        uint32_t flags;
    };
    
    DICompositeType(Kind kind, const std::string& name, size_t size)
        : kind_(kind), name_(name), size_(size) {}
    
    Kind GetKind() const override { return kind_; }
    size_t GetSize() const override { return size_; }
    size_t GetAlignment() const override { return alignment_; }
    std::string GetName() const override { return name_; }
    
    void AddMember(const Member& member) { members_.push_back(member); }
    const std::vector<Member>& GetMembers() const { return members_; }
    
private:
    Kind kind_;
    std::string name_;
    size_t size_;
    size_t alignment_ = 0;
    std::vector<Member> members_;
};

/** @} */

/** @name 变量调试信息 */
/** @{ */

/** @brief DIVariable class. */
class DIVariable {
public:
    /** @brief Kind enumeration. */
    enum class Kind {
        Local,
        Parameter,
        Global,
        Member
    };
    
    DIVariable(Kind kind, const std::string& name, DIType* type, 
               const SourceLocation& location)
        : kind_(kind), name_(name), type_(type), location_(location) {}
    
    Kind GetKind() const { return kind_; }
    const std::string& GetName() const { return name_; }
    DIType* GetType() const { return type_; }
    const SourceLocation& GetLocation() const { return location_; }
    
    // 变量位置（寄存器、栈偏移等）
    /** @brief Location data structure. */
    struct Location {
        /** @brief Kind enumeration. */
        enum class Kind {
            Register,
            Memory,
            Constant,
            Composite  // 部分在寄存器，部分在内存
        };
        
        Kind kind;
        union {
            uint32_t reg_num;      // Register
            int64_t stack_offset;  // Memory
            int64_t const_value;   // Constant
        };
        
        // 位置有效的指令范围
        uint64_t start_pc;
        uint64_t end_pc;
    };
    
    void AddLocation(const Location& loc) { locations_.push_back(loc); }
    const std::vector<Location>& GetLocations() const { return locations_; }
    
private:
    Kind kind_;
    std::string name_;
    DIType* type_;
    SourceLocation location_;
    std::vector<Location> locations_;
};

/** @} */

/** @name 函数调试信息 */
/** @{ */

/** @brief DIFunction class. */
class DIFunction {
public:
    DIFunction(const std::string& name, const SourceLocation& location,
               DIType* return_type)
        : name_(name), location_(location), return_type_(return_type) {}
    
    const std::string& GetName() const { return name_; }
    const SourceLocation& GetLocation() const { return location_; }
    DIType* GetReturnType() const { return return_type_; }
    
    // 参数
    void AddParameter(DIVariable* param) { parameters_.push_back(param); }
    const std::vector<DIVariable*>& GetParameters() const { return parameters_; }
    
    // 局部变量
    void AddLocalVariable(DIVariable* var) { local_variables_.push_back(var); }
    const std::vector<DIVariable*>& GetLocalVariables() const { 
        return local_variables_; 
    }
    
    // 代码范围
    /** @brief CodeRange data structure. */
    struct CodeRange {
        uint64_t low_pc;
        uint64_t high_pc;
    };
    void SetCodeRange(const CodeRange& range) { code_range_ = range; }
    const CodeRange& GetCodeRange() const { return code_range_; }
    
    // 内联实例
    /** @brief InlineInstance data structure. */
    struct InlineInstance {
        std::string caller_name;
        SourceLocation call_site;
        uint64_t low_pc;
        uint64_t high_pc;
    };
    void AddInlineInstance(const InlineInstance& instance) {
        inline_instances_.push_back(instance);
    }
    const std::vector<InlineInstance>& GetInlineInstances() const {
        return inline_instances_;
    }
    
private:
    std::string name_;
    SourceLocation location_;
    DIType* return_type_;
    std::vector<DIVariable*> parameters_;
    std::vector<DIVariable*> local_variables_;
    CodeRange code_range_;
    std::vector<InlineInstance> inline_instances_;
};

/** @} */

/** @name 编译单元调试信息 */
/** @{ */

/** @brief DICompileUnit class. */
class DICompileUnit {
public:
    DICompileUnit(const std::string& source_file, const std::string& producer)
        : source_file_(source_file), producer_(producer) {}
    
    const std::string& GetSourceFile() const { return source_file_; }
    const std::string& GetProducer() const { return producer_; }
    
    void AddFunction(DIFunction* func) { functions_.push_back(func); }
    void AddGlobalVariable(DIVariable* var) { globals_.push_back(var); }
    void AddType(DIType* type) { types_.push_back(type); }
    
    const std::vector<DIFunction*>& GetFunctions() const { return functions_; }
    const std::vector<DIVariable*>& GetGlobals() const { return globals_; }
    const std::vector<DIType*>& GetTypes() const { return types_; }
    
private:
    std::string source_file_;
    std::string producer_;
    std::vector<DIFunction*> functions_;
    std::vector<DIVariable*> globals_;
    std::vector<DIType*> types_;
};

/** @} */

/** @name 行号表 */
/** @{ */

/** @brief LineTable class. */
class LineTable {
public:
    /** @brief Entry data structure. */
    struct Entry {
        uint64_t address;
        uint32_t line;
        uint32_t column;
        const std::string* file;
        bool is_stmt;          // Statement boundary
        bool basic_block;      // Basic block start
        bool prologue_end;     // Function prologue end
        bool epilogue_begin;   // Function epilogue start
    };
    
    void AddEntry(const Entry& entry) { entries_.push_back(entry); }
    const std::vector<Entry>& GetEntries() const { return entries_; }
    
    // 查找地址对应的源码位置
    std::optional<SourceLocation> GetLocationForAddress(uint64_t address) const;
    
    // 查找源码位置对应的地址
    std::vector<uint64_t> GetAddressesForLocation(const SourceLocation& loc) const;
    
private:
    std::vector<Entry> entries_;
};

/** @} */

/** @name 调试信息构建器 */
/** @{ */

/** @brief DebugInfoBuilder class. */
class DebugInfoBuilder {
public:
    DebugInfoBuilder() = default;
    
    // 创建类型
    DIBasicType* CreateBasicType(const std::string& name, size_t size,
                                DIBasicType::Encoding encoding);
    DICompositeType* CreateStructType(const std::string& name, size_t size);
    DICompositeType* CreateClassType(const std::string& name, size_t size);
    DIType* CreatePointerType(DIType* pointee);
    DIType* CreateArrayType(DIType* element, size_t count);
    
    // 创建变量
    DIVariable* CreateLocalVariable(const std::string& name, DIType* type,
                                   const SourceLocation& location);
    DIVariable* CreateParameter(const std::string& name, DIType* type,
                               const SourceLocation& location);
    DIVariable* CreateGlobalVariable(const std::string& name, DIType* type,
                                    const SourceLocation& location);
    
    // 创建函数
    DIFunction* CreateFunction(const std::string& name, 
                              const SourceLocation& location,
                              DIType* return_type);
    
    // 创建编译单元
    DICompileUnit* CreateCompileUnit(const std::string& source_file);
    
    // 行号表
    LineTable& GetLineTable() { return line_table_; }
    const LineTable& GetLineTable() const { return line_table_; }
    
    // Generate DWARF debug sections (.debug_info, .debug_line, etc.)
    std::unordered_map<std::string, std::vector<uint8_t>> GenerateDWARF() const;
    
    // 优化代码调试支持
    void TrackValueLocation(DIVariable* var, uint64_t pc, 
                           const DIVariable::Location& location);
    
    // 内联调试信息
    void RecordInlineInstance(DIFunction* inlined_func, 
                             const std::string& caller,
                             const SourceLocation& call_site,
                             uint64_t low_pc, uint64_t high_pc);
    
private:
    std::vector<std::unique_ptr<DIType>> types_;
    std::vector<std::unique_ptr<DIVariable>> variables_;
    std::vector<std::unique_ptr<DIFunction>> functions_;
    std::vector<std::unique_ptr<DICompileUnit>> compile_units_;
    LineTable line_table_;
    
    std::string producer_ = "PolyglotCompiler v3.0";
};

/** @} */

/** @name 调试信息验证器 */
/** @{ */

/** @brief DebugInfoValidator class. */
class DebugInfoValidator {
public:
    explicit DebugInfoValidator(const DebugInfoBuilder& builder)
        : builder_(builder) {}
    
    // 验证调试信息完整性
    /** @brief ValidationResult data structure. */
    struct ValidationResult {
        bool valid;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };
    ValidationResult Validate() const;
    
private:
    const DebugInfoBuilder& builder_;
    
    void ValidateTypes(ValidationResult& result) const;
    void ValidateVariables(ValidationResult& result) const;
    void ValidateFunctions(ValidationResult& result) const;
    void ValidateLineTable(ValidationResult& result) const;
};

/** @} */

/** @name 调试信息打印器 */
/** @{ */

/** @brief DebugInfoPrinter class. */
class DebugInfoPrinter {
public:
    explicit DebugInfoPrinter(const DebugInfoBuilder& builder)
        : builder_(builder) {}
    
    // 打印为人类可读格式
    void Print(std::ostream& out) const;
    
    // 打印类型信息
    void PrintTypes(std::ostream& out) const;
    
    // 打印变量信息
    void PrintVariables(std::ostream& out) const;
    
    // 打印函数信息
    void PrintFunctions(std::ostream& out) const;
    
    // 打印行号表
    void PrintLineTable(std::ostream& out) const;
    
private:
    const DebugInfoBuilder& builder_;
};

} // namespace polyglot::debug

/** @} */