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

// Cross-language constructor: NEW(language, class, arg1, arg2, ...)
// Instantiates a class from a foreign language
struct NewExpression : Expression {
    std::string language;
    std::string class_name;  // Possibly qualified: module::ClassName
    std::vector<std::shared_ptr<Expression>> args;
};

// Cross-language method call: METHOD(language, object, method_name, arg1, arg2, ...)
// Invokes a method on an object obtained from a foreign language
struct MethodCallExpression : Expression {
    std::string language;
    std::shared_ptr<Expression> object;
    std::string method_name;
    std::vector<std::shared_ptr<Expression>> args;
};

// Cross-language attribute get: GET(language, object, attribute_name)
// Retrieves an attribute/property value from a foreign object
struct GetAttrExpression : Expression {
    std::string language;
    std::shared_ptr<Expression> object;
    std::string attr_name;
};

// Cross-language attribute set: SET(language, object, attribute_name, value)
// Sets an attribute/property value on a foreign object
struct SetAttrExpression : Expression {
    std::string language;
    std::shared_ptr<Expression> object;
    std::string attr_name;
    std::shared_ptr<Expression> value;
};

// Cross-language destructor call: DELETE(language, object)
// Explicitly calls the destructor / __del__ / drop on a foreign object
struct DeleteExpression : Expression {
    std::string language;
    std::shared_ptr<Expression> object;
};

// Cross-language inheritance: EXTEND(language, base_class) AS DerivedName { ... }
// Declares a .ploy type that extends a foreign language class
struct ExtendDecl : Statement {
    std::string language;
    std::string base_class;     // Possibly qualified: module::ClassName
    std::string derived_name;   // The name of the derived type in .ploy
    // Override methods (each should be a FuncDecl)
    std::vector<std::shared_ptr<Statement>> methods;
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

// IMPORT "path" AS alias; or IMPORT lang::module; or IMPORT lang PACKAGE pkg AS alias;
// Extended with version constraints and selective imports:
//   IMPORT python PACKAGE numpy >= 1.20;
//   IMPORT python PACKAGE numpy::(array, mean);
struct ImportDecl : Statement {
    std::string module_path;
    std::string alias;
    std::string language;       // empty if importing by path
    std::string package_name;   // non-empty when importing a language-specific package

    // Version constraint: ">=", "<=", "==", ">", "<", "~="
    std::string version_op;
    std::string version_constraint;  // e.g. "1.20", "2.0.0"

    // Selective imports: IMPORT python PACKAGE numpy::(array, mean);
    std::vector<std::string> selected_symbols;

    // Virtual environment path (populated from CONFIG VENV directive)
    std::string venv_path;
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

// WITH(language, expression) AS name { body }
// Automatic resource management: calls __enter__ before and __exit__ after the body
struct WithStatement : Statement {
    std::string language;
    std::shared_ptr<Expression> resource_expr;
    std::string var_name;                       // bound variable name after AS
    std::vector<std::shared_ptr<Statement>> body;
};

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

// CONFIG VENV "path/to/venv";
// CONFIG CONDA env_name;
// CONFIG UV "path/to/venv";
// CONFIG PIPENV "path/to/project";
// CONFIG POETRY "path/to/project";
// Specifies a virtual environment or package manager to use for package resolution
struct VenvConfigDecl : Statement {
    enum class ManagerKind {
        kVenv,      // Standard Python venv / virtualenv
        kConda,     // Conda environment
        kUv,        // uv-managed virtual environment
        kPipenv,    // Pipenv project
        kPoetry     // Poetry project
    };
    ManagerKind manager{ManagerKind::kVenv};
    std::string language;       // e.g. "python"
    std::string venv_path;      // path to the virtual environment / project / env name
};

// ============================================================================
// Module (top-level AST root)
// ============================================================================

struct Module {
    std::string filename;
    std::vector<std::shared_ptr<Statement>> declarations;
};

} // namespace polyglot::ploy
