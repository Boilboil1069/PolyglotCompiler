#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/include/core/source_loc.h"

namespace polyglot::ploy {

// ============================================================================
// Base AST Nodes
// ============================================================================

struct AstNode {
    virtual ~AstNode() = default;
    core::SourceLoc loc{};
};

struct Statement : AstNode {};
struct Expression : AstNode {};
struct TypeNode : AstNode {};

// ============================================================================
// Type Nodes
// ============================================================================

// Simple named type: INT, FLOAT, BOOL, STRING, VOID, or user-defined
struct SimpleType : TypeNode {
    std::string name;
};

// Parameterized type: ARRAY[INT], etc.
struct ParameterizedType : TypeNode {
    std::string name;
    std::vector<std::shared_ptr<TypeNode>> type_args;
};

// Qualified type: cpp::int, python::str, rust::Vec<i32>
struct QualifiedType : TypeNode {
    std::string language;
    std::string type_name;
};

// Function type: (INT, FLOAT) -> BOOL
struct FunctionType : TypeNode {
    std::vector<std::shared_ptr<TypeNode>> param_types;
    std::shared_ptr<TypeNode> return_type;
};

// ============================================================================
// Expression Nodes
// ============================================================================

// Identifier: my_var, module.func
struct Identifier : Expression {
    std::string name;
};

// Qualified identifier: cpp::func, python::module::func
struct QualifiedIdentifier : Expression {
    std::string qualifier;
    std::string name;
};

// Literal values: 42, 3.14, "hello", TRUE, FALSE, NULL
struct Literal : Expression {
    enum class Kind { kInteger, kFloat, kString, kBool, kNull };
    Kind kind{Kind::kInteger};
    std::string value;
};

// Unary expression: -x, !x, NOT x
struct UnaryExpression : Expression {
    std::string op;
    std::shared_ptr<Expression> operand;
};

// Binary expression: a + b, x == y, p AND q
struct BinaryExpression : Expression {
    std::string op;
    std::shared_ptr<Expression> left;
    std::shared_ptr<Expression> right;
};

// Function call expression: func(arg1, arg2)
struct CallExpression : Expression {
    std::shared_ptr<Expression> callee;
    std::vector<std::shared_ptr<Expression>> args;
};

// Cross-language call: CALL(language, function, arg1, arg2, ...)
struct CrossLangCallExpression : Expression {
    std::string language;
    std::string function;
    std::vector<std::shared_ptr<Expression>> args;
};

// Member access: obj.member
struct MemberExpression : Expression {
    std::shared_ptr<Expression> object;
    std::string member;
};

// Index access: arr[idx]
struct IndexExpression : Expression {
    std::shared_ptr<Expression> object;
    std::shared_ptr<Expression> index;
};

// Range expression: 0..10
struct RangeExpression : Expression {
    std::shared_ptr<Expression> start;
    std::shared_ptr<Expression> end;
};

// List literal: [1, 2, 3]
struct ListLiteral : Expression {
    std::vector<std::shared_ptr<Expression>> elements;
};

// Tuple literal: (1, "hello", 3.14)
struct TupleLiteral : Expression {
    std::vector<std::shared_ptr<Expression>> elements;
};

// Dict literal: {"key1": value1, "key2": value2}
struct DictLiteral : Expression {
    struct Entry {
        std::shared_ptr<Expression> key;
        std::shared_ptr<Expression> value;
    };
    std::vector<Entry> entries;
};

// Struct literal: Point { x: 1.0, y: 2.0 }
struct StructLiteral : Expression {
    std::string struct_name;
    struct FieldInit {
        std::string name;
        std::shared_ptr<Expression> value;
    };
    std::vector<FieldInit> fields;
};

// Explicit type conversion: CONVERT(expr, TargetType)
struct ConvertExpression : Expression {
    std::shared_ptr<Expression> expr;
    std::shared_ptr<TypeNode> target_type;
};

// ============================================================================
// Statement Nodes
// ============================================================================

// LINK(target_lang, source_lang, target_func, source_func) { ... }
struct LinkDecl : Statement {
    enum class LinkKind { kFunction, kVariable, kStruct };
    LinkKind link_kind{LinkKind::kFunction};
    std::string target_language;
    std::string source_language;
    std::string target_symbol;
    std::string source_symbol;
    // Optional body containing MAP_TYPE directives
    std::vector<std::shared_ptr<Statement>> body;
};

// IMPORT "path" AS alias; or IMPORT lang::module;
struct ImportDecl : Statement {
    std::string module_path;
    std::string alias;
    std::string language;  // empty if importing by path
};

// EXPORT function_name; or EXPORT function_name AS "external_name";
struct ExportDecl : Statement {
    std::string symbol_name;
    std::string external_name;  // empty if no AS clause
};

// MAP_TYPE(source_type, target_type);
struct MapTypeDecl : Statement {
    std::shared_ptr<TypeNode> source_type;
    std::shared_ptr<TypeNode> target_type;
};

// PIPELINE name { ... }
struct PipelineDecl : Statement {
    std::string name;
    std::vector<std::shared_ptr<Statement>> body;
};

// FUNC name(params) -> return_type { ... }
struct FuncDecl : Statement {
    struct Param {
        std::string name;
        std::shared_ptr<TypeNode> type;
    };
    std::string name;
    std::vector<Param> params;
    std::shared_ptr<TypeNode> return_type;  // nullptr means VOID
    std::vector<std::shared_ptr<Statement>> body;
};

// LET x: TYPE = expr; or VAR y: TYPE = expr;
struct VarDecl : Statement {
    std::string name;
    bool is_mutable{false};  // VAR = true, LET = false
    std::shared_ptr<TypeNode> type;  // nullptr for type inference
    std::shared_ptr<Expression> init;
};

// Expression statement: expr;
struct ExprStatement : Statement {
    std::shared_ptr<Expression> expr;
};

// RETURN expr;
struct ReturnStatement : Statement {
    std::shared_ptr<Expression> value;  // nullptr for bare RETURN
};

// Block of statements: { ... }
struct BlockStatement : Statement {
    std::vector<std::shared_ptr<Statement>> statements;
};

// IF condition { ... } ELSE IF condition { ... } ELSE { ... }
struct IfStatement : Statement {
    std::shared_ptr<Expression> condition;
    std::vector<std::shared_ptr<Statement>> then_body;
    std::vector<std::shared_ptr<Statement>> else_body;  // may contain another IfStatement
};

// WHILE condition { ... }
struct WhileStatement : Statement {
    std::shared_ptr<Expression> condition;
    std::vector<std::shared_ptr<Statement>> body;
};

// FOR item IN collection { ... }
struct ForStatement : Statement {
    std::string iterator_name;
    std::shared_ptr<Expression> iterable;
    std::vector<std::shared_ptr<Statement>> body;
};

// MATCH value { CASE pattern { ... } ... DEFAULT { ... } }
struct MatchStatement : Statement {
    std::shared_ptr<Expression> value;
    struct Case {
        std::shared_ptr<Expression> pattern;  // nullptr for DEFAULT
        std::vector<std::shared_ptr<Statement>> body;
    };
    std::vector<Case> cases;
};

// BREAK;
struct BreakStatement : Statement {};

// CONTINUE;
struct ContinueStatement : Statement {};

// STRUCT Name { field1: Type1, field2: Type2, ... }
struct StructDecl : Statement {
    std::string name;
    struct Field {
        std::string name;
        std::shared_ptr<TypeNode> type;
    };
    std::vector<Field> fields;
};

// MAP_FUNC name(params) -> ReturnType { body }
// A specialised function for type mapping between languages
struct MapFuncDecl : Statement {
    std::string name;
    std::vector<FuncDecl::Param> params;
    std::shared_ptr<TypeNode> return_type;
    std::vector<std::shared_ptr<Statement>> body;
};

// ============================================================================
// Module (top-level AST root)
// ============================================================================

struct Module {
    std::string filename;
    std::vector<std::shared_ptr<Statement>> declarations;
};

} // namespace polyglot::ploy
