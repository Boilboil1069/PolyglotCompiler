/**
 * @file     go_ast.h
 * @brief    Go language AST definitions
 *
 * @ingroup  Frontend / Go
 * @author   Manning Cyrus
 * @date     2026-04-26
 *
 * AST nodes covering a usable subset of the Go programming language:
 * package declarations, imports, named/struct/interface types, functions
 * (including methods with receivers and multiple return values), control
 * flow (if/for/switch/select), and expression forms (composite literals,
 * channel send, type assertions, slice/index/range).  Concurrency
 * keywords `go`, `defer`, `chan`, and `select` are represented to keep
 * the surface honest about Go semantics, even when the IR lowering only
 * targets the synchronous, statically typed numeric subset.
 */
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common/include/core/source_loc.h"

namespace polyglot::go {

struct AstNode { core::SourceLoc loc{}; virtual ~AstNode() = default; };

// ----------------------------- Types --------------------------------------

enum class TypeKind {
    kNamed,        // int, string, MyType, pkg.Name
    kPointer,      // *T
    kSlice,        // []T
    kArray,        // [N]T
    kMap,          // map[K]V
    kChan,         // chan T, chan<- T, <-chan T
    kFunc,         // func(...) ...
    kStruct,       // struct{...}
    kInterface,    // interface{...}
    kEllipsis,     // ...T (variadic)
};

struct TypeNode : AstNode {
    TypeKind kind{TypeKind::kNamed};
    std::string name;                                   // for kNamed: "pkg.Name" or "Name"
    std::shared_ptr<TypeNode> elem;                     // pointer/slice/array/chan/ellipsis
    std::shared_ptr<TypeNode> key;                      // map key
    long long array_len{0};                             // -1 for [...]T
    int chan_dir{0};                                    // 0=bi, 1=send, 2=recv
    std::vector<std::shared_ptr<TypeNode>> params;      // func params
    std::vector<std::shared_ptr<TypeNode>> results;     // func results
    struct Field {
        std::vector<std::string> names; // empty = embedded
        std::shared_ptr<TypeNode> type;
        std::string tag;
    };
    std::vector<Field> fields;                          // struct
    std::vector<Field> methods;                         // interface (Field.names[0] = method, type = func)
};

// --------------------------- Expressions ----------------------------------

struct Expression : AstNode {};

struct Identifier : Expression { std::string name; };

struct BasicLit : Expression {
    enum class Kind { kInt, kFloat, kImag, kString, kRune, kBool, kNil } kind{Kind::kInt};
    std::string value;
};

struct CompositeLit : Expression {
    std::shared_ptr<TypeNode> type;        // may be null when inferred
    struct Element {
        std::shared_ptr<Expression> key;   // optional
        std::shared_ptr<Expression> value;
    };
    std::vector<Element> elements;
};

struct FuncLit : Expression {
    std::shared_ptr<TypeNode> type;        // a kFunc TypeNode
    std::vector<std::string> param_names;
    std::vector<std::string> result_names;
    std::shared_ptr<struct Block> body;
};

struct UnaryExpr : Expression {
    std::string op;                        // "+", "-", "!", "^", "*", "&", "<-"
    std::shared_ptr<Expression> operand;
};

struct BinaryExpr : Expression {
    std::string op;
    std::shared_ptr<Expression> left;
    std::shared_ptr<Expression> right;
};

struct ParenExpr : Expression { std::shared_ptr<Expression> inner; };

struct SelectorExpr : Expression {
    std::shared_ptr<Expression> x;
    std::string sel;
};

struct IndexExpr : Expression {
    std::shared_ptr<Expression> x;
    std::shared_ptr<Expression> index;
};

struct SliceExpr : Expression {
    std::shared_ptr<Expression> x;
    std::shared_ptr<Expression> low, high, max;
    bool three_index{false};
};

struct TypeAssertExpr : Expression {
    std::shared_ptr<Expression> x;
    std::shared_ptr<TypeNode> type;        // null = type switch guard
};

struct CallExpr : Expression {
    std::shared_ptr<Expression> fun;
    std::vector<std::shared_ptr<Expression>> args;
    bool has_ellipsis{false};
};

struct StarExpr : Expression { std::shared_ptr<Expression> x; };       // *x dereference / pointer type in expr

struct KeyValueExpr : Expression {
    std::shared_ptr<Expression> key;
    std::shared_ptr<Expression> value;
};

// --------------------------- Statements -----------------------------------

struct Statement : AstNode {};

struct Block : Statement { std::vector<std::shared_ptr<Statement>> stmts; };

struct ExprStmt : Statement { std::shared_ptr<Expression> expr; };

struct SendStmt : Statement {                        // ch <- v
    std::shared_ptr<Expression> chan;
    std::shared_ptr<Expression> value;
};

struct IncDecStmt : Statement {
    std::shared_ptr<Expression> target;
    bool inc{true};                                   // false = "--"
};

struct AssignStmt : Statement {
    std::vector<std::shared_ptr<Expression>> lhs;
    std::vector<std::shared_ptr<Expression>> rhs;
    std::string op;                                   // "=", ":=", "+=", "-=", "*=", "/="...
};

struct DeclStmt : Statement {                         // var/const/type declaration inside func
    std::shared_ptr<struct GenDecl> decl;
};

struct ReturnStmt : Statement {
    std::vector<std::shared_ptr<Expression>> results;
};

struct BranchStmt : Statement {                       // break, continue, goto, fallthrough
    std::string keyword;
    std::string label;
};

struct LabeledStmt : Statement {
    std::string label;
    std::shared_ptr<Statement> stmt;
};

struct IfStmt : Statement {
    std::shared_ptr<Statement> init;                  // optional simple stmt
    std::shared_ptr<Expression> cond;
    std::shared_ptr<Block> body;
    std::shared_ptr<Statement> else_branch;
};

struct ForStmt : Statement {
    std::shared_ptr<Statement> init;
    std::shared_ptr<Expression> cond;                 // null = for { … }
    std::shared_ptr<Statement> post;
    std::shared_ptr<Block> body;
    bool is_range{false};
    std::vector<std::shared_ptr<Expression>> range_lhs;
    std::string range_assign;                         // ":=" or "="
    std::shared_ptr<Expression> range_x;
};

struct SwitchClause : AstNode {
    std::vector<std::shared_ptr<Expression>> values;  // empty = default
    std::vector<std::shared_ptr<Statement>> body;
};

struct SwitchStmt : Statement {
    std::shared_ptr<Statement> init;
    std::shared_ptr<Expression> tag;                  // optional
    std::vector<SwitchClause> clauses;
    bool is_type_switch{false};
    std::string type_switch_var;
};

struct CommClause : AstNode {
    std::shared_ptr<Statement> comm;                  // null = default
    std::vector<std::shared_ptr<Statement>> body;
};

struct SelectStmt : Statement { std::vector<CommClause> clauses; };

struct GoStmt    : Statement { std::shared_ptr<Expression> call; };
struct DeferStmt : Statement { std::shared_ptr<Expression> call; };

// --------------------------- Declarations ---------------------------------

struct ImportSpec : AstNode { std::string alias; std::string path; };

struct ValueSpec : AstNode {                          // for var / const
    std::vector<std::string> names;
    std::shared_ptr<TypeNode> type;
    std::vector<std::shared_ptr<Expression>> values;
};

struct TypeSpec : AstNode {
    std::string name;
    std::shared_ptr<TypeNode> type;
    bool is_alias{false};                              // type Foo = Bar
};

struct GenDecl : AstNode {                            // var / const / type / import
    std::string keyword;
    std::vector<ImportSpec> imports;
    std::vector<ValueSpec> values;
    std::vector<TypeSpec> types;
};

struct Receiver : AstNode {
    std::string name;
    std::shared_ptr<TypeNode> type;                   // *T or T
};

struct FuncDecl : AstNode {
    std::string name;
    std::optional<Receiver> receiver;
    std::vector<std::pair<std::string, std::shared_ptr<TypeNode>>> params;   // expanded
    std::vector<std::pair<std::string, std::shared_ptr<TypeNode>>> results;  // result names may be empty
    std::shared_ptr<Block> body;                       // null for forward declarations
    bool is_variadic{false};
    std::string doc;                                   // leading // comment block
};

struct File {
    std::string package_name;
    std::vector<GenDecl> decls;                        // var/const/type/import groups
    std::vector<std::shared_ptr<FuncDecl>> funcs;
};

}  // namespace polyglot::go
