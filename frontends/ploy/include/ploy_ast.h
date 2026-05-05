/**
 * @file     ploy_ast.h
 * @brief    Ploy language frontend
 *
 * @ingroup  Frontend / Ploy
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/include/core/source_loc.h"

namespace polyglot::ploy {

// ============================================================================
// Base AST Nodes
// ============================================================================

/** @brief AstNode data structure. */
struct AstNode {
  virtual ~AstNode() = default;
  core::SourceLoc loc{};
};

/** @brief Statement data structure. */
struct Statement : AstNode {};
/** @brief Expression data structure. */
struct Expression : AstNode {};
/** @brief TypeNode data structure. */
struct TypeNode : AstNode {};
/** @brief Pattern data structure (base for MATCH/CASE patterns). */
struct Pattern : AstNode {};

// ============================================================================
// Visibility and Attributes (since v1.16.0)
// ============================================================================

// Module-boundary visibility for top-level declarations.  `kPrivate` is the
// default; only `kPub` symbols may be referenced by `EXPORT` or imported
// from other modules.
enum class Visibility { kPrivate, kPub };

// `@name(arg, arg, ...)` annotation prefix on a top-level declaration.
// Recognised names are validated by sema against a built-in catalog;
// unknown names produce a warning.  Arguments are captured verbatim as
// strings (literal text, including any surrounding quotes); built-in
// attributes that need richer payloads parse the strings in sema.
struct Attribute {
  std::string name;
  std::vector<std::string> args;
  core::SourceLoc loc{};
};

// ============================================================================
// Type Nodes
// ============================================================================

// Simple named type: INT, FLOAT, BOOL, STRING, VOID, or user-defined
/** @brief SimpleType data structure. */
struct SimpleType : TypeNode {
  std::string name;
};

// Parameterized type: ARRAY[INT], etc.
/** @brief ParameterizedType data structure. */
struct ParameterizedType : TypeNode {
  std::string name;
  std::vector<std::shared_ptr<TypeNode>> type_args;
};

// Qualified type: cpp::int, python::str, rust::Vec<i32>
/** @brief QualifiedType data structure. */
struct QualifiedType : TypeNode {
  std::string language;
  std::string type_name;
};

// Function type: (INT, FLOAT) -> BOOL
/** @brief FunctionType data structure. */
struct FunctionType : TypeNode {
  std::vector<std::shared_ptr<TypeNode>> param_types;
  std::shared_ptr<TypeNode> return_type;
};

// Cross-language object handle type: HANDLE<lang::module::ClassName>.
//
// `language` carries the originating foreign language, `class_path` the
// fully qualified class path inside that language (e.g. "torch::nn::Linear").
// Resolved by sema to a `core::Type::Class(class_path, language)` instance,
// which is statically distinct from any class produced by another language.
/** @brief HandleType data structure. */
struct HandleType : TypeNode {
  std::string language;
  std::string class_path;
};

// ============================================================================
// Expression Nodes
// ============================================================================

// Identifier: my_var, module.func
/** @brief Identifier data structure. */
struct Identifier : Expression {
  std::string name;
};

// Qualified identifier: cpp::func, python::module::func
/** @brief QualifiedIdentifier data structure. */
struct QualifiedIdentifier : Expression {
  std::string qualifier;
  std::string name;
};

// Literal values: 42, 3.14, "hello", TRUE, FALSE, NULL
/** @brief Literal data structure. */
struct Literal : Expression {
  /** @brief Kind enumeration. */
  enum class Kind { kInteger, kFloat, kString, kBool, kNull };
  Kind kind{Kind::kInteger};
  std::string value;
};

// Template (interpolated) string literal (since v1.17.0).
//
// Syntax: f"text {expr} more {expr2}".  At parse time the lexer hands the
// parser a single `kString` token whose lexeme starts with `f"`; the parser
// splits the body into an alternating sequence of literal text segments
// and interpolated expression segments.  Sema verifies each interpolated
// expression has a formattable type (Int / Float / String / Bool); the
// expression result type is always String.  Lowering eagerly assembles
// the formatted bytes when every interpolated expression is a constant
// literal; the runtime-formatting helper is tracked as future work.
/** @brief TemplateString data structure. */
struct TemplateString : Expression {
  /** @brief One segment of a template string. */
  struct Part {
    bool is_text{true};                       ///< true = literal text, false = interpolated expression
    std::string text;                          ///< populated when is_text == true
    std::shared_ptr<Expression> expr;          ///< populated when is_text == false
    core::SourceLoc loc{};
  };
  std::vector<Part> parts;
};

// Unary expression: -x, !x, NOT x
/** @brief UnaryExpression data structure. */
struct UnaryExpression : Expression {
  std::string op;
  std::shared_ptr<Expression> operand;
};

// AWAIT <expr>
//
// Suspends the surrounding ASYNC function until the awaited future is
// resolved.  The operand must evaluate to a `Future<T>` value (typically
// the result of calling another ASYNC function or a cross-language
// asynchronous bridge); the await expression then evaluates to `T`.
// Sema rejects use outside an `ASYNC FUNC`.  Lowering emits a call to
// `__ploy_rt_await` (since v1.14.0).
/** @brief AwaitExpression data structure. */
struct AwaitExpression : Expression {
  std::shared_ptr<Expression> operand;
};

// Postfix `?` operator: short-circuit unwrap of an OPTION<T> (since v1.19.0).
//
// Semantics:
//   * `expr?` evaluates `expr`; when the value is `Some(v)` the whole
//     expression evaluates to `v`.
//   * When the value is `None`, the enclosing function returns the
//     zero/`None` value of its declared return type, mirroring Rust's
//     `?` short-hand for early returns out of an `Option`-shaped pipeline.
//
// Sema enforces that the operand has type `OPTION<T>` and that the
// enclosing function's return type is also some `OPTION<U>` whose `U` is
// assignment-compatible with `T`; lowering emits a single conditional
// branch on the OPTION tag (currently the operand's truthiness, matching
// the `IF LET` lowering shape).  Disambiguation against the future
// error-propagation form on `Result/Error` returning calls is by operand
// type — once the error-propagation work track lands the same token will
// resolve to the propagation variant when the operand is an `Error`.
/** @brief OptionUnwrapExpression data structure. */
struct OptionUnwrapExpression : Expression {
  std::shared_ptr<Expression> operand;
};

// Binary expression: a + b, x == y, p AND q
/** @brief BinaryExpression data structure. */
struct BinaryExpression : Expression {
  std::string op;
  std::shared_ptr<Expression> left;
  std::shared_ptr<Expression> right;
};

// Named argument expression: name = value
// Used in function calls to pass arguments by name: func(x = 42, y = 10)
/** @brief NamedArgument data structure. */
struct NamedArgument : Expression {
  std::string name;
  std::shared_ptr<Expression> value;
};

// Function call expression: func(arg1, arg2, name=value)
/** @brief CallExpression data structure. */
struct CallExpression : Expression {
  std::shared_ptr<Expression> callee;
  std::vector<std::shared_ptr<Expression>> args;
};

// Cross-language call: CALL(language, function, arg1, arg2, name=value, ...)
/** @brief CrossLangCallExpression data structure. */
struct CrossLangCallExpression : Expression {
  std::string language;
  std::string function;
  std::vector<std::shared_ptr<Expression>> args;
  // Filled in by sema from the active LANG / WITH LANG / @LANG scope.
  // Empty string means "use the language's default version".
  std::string lang_version_pin;
};

// Cross-language constructor: NEW(language, class, arg1, arg2, ...)
// Instantiates a class from a foreign language
/** @brief NewExpression data structure. */
struct NewExpression : Expression {
  std::string language;
  std::string class_name; // Possibly qualified: module::ClassName
  std::vector<std::shared_ptr<Expression>> args;
  std::string lang_version_pin; // Resolved by sema.
};

// Cross-language method call: METHOD(language, object, method_name, arg1, arg2, ...)
// Invokes a method on an object obtained from a foreign language
/** @brief MethodCallExpression data structure. */
struct MethodCallExpression : Expression {
  std::string language;
  std::shared_ptr<Expression> object;
  std::string method_name;
  std::vector<std::shared_ptr<Expression>> args;
  std::string lang_version_pin; // Resolved by sema.
};

// Cross-language attribute get: GET(language, object, attribute_name)
// Retrieves an attribute/property value from a foreign object
/** @brief GetAttrExpression data structure. */
struct GetAttrExpression : Expression {
  std::string language;
  std::shared_ptr<Expression> object;
  std::string attr_name;
  std::string lang_version_pin; // Resolved by sema.
};

// Cross-language attribute set: SET(language, object, attribute_name, value)
// Sets an attribute/property value on a foreign object
/** @brief SetAttrExpression data structure. */
struct SetAttrExpression : Expression {
  std::string language;
  std::shared_ptr<Expression> object;
  std::string attr_name;
  std::shared_ptr<Expression> value;
  std::string lang_version_pin; // Resolved by sema.
};

// Cross-language destructor call: DELETE(language, object)
// Explicitly calls the destructor / __del__ / drop on a foreign object
/** @brief DeleteExpression data structure. */
struct DeleteExpression : Expression {
  std::string language;
  std::shared_ptr<Expression> object;
  std::string lang_version_pin; // Resolved by sema.
};

// Cross-language inheritance: EXTEND(language, base_class) AS DerivedName { ... }
// Declares a .ploy type that extends a foreign language class
/** @brief ExtendDecl data structure. */
struct ExtendDecl : Statement {
  std::string language;
  std::string base_class;   // Possibly qualified: module::ClassName
  std::string derived_name; // The name of the derived type in .ploy
  // Override methods (each should be a FuncDecl)
  std::vector<std::shared_ptr<Statement>> methods;
  std::string lang_version_pin; // Resolved by sema.
};

// Foreign-class signature block: CLASS lang::module::Name { METHOD ...; ATTR ...; }
//
// Registers an explicit type schema for a foreign-language class so that
// cross-language NEW / METHOD / GET / SET expressions can be statically
// type-checked.  Without a class block, sema falls back to dynamic
// dispatch and emits a single warning per unknown method (kept warning,
// not error, to preserve backward compatibility for existing code that
// has no class blocks).
/** @brief ClassMethodSig data structure (one row inside a CLASS body). */
struct ClassMethodSig {
  std::string name;                                                    // method name
  std::vector<std::pair<std::string, std::shared_ptr<TypeNode>>> params; // (name, type) pairs
  std::shared_ptr<TypeNode> return_type;                                // nullptr => VOID
  core::SourceLoc loc{};
};

/** @brief ClassAttrSig data structure (one row inside a CLASS body). */
struct ClassAttrSig {
  std::string name;
  std::shared_ptr<TypeNode> type;
  core::SourceLoc loc{};
};

/** @brief ClassDecl data structure. */
struct ClassDecl : Statement {
  std::string language;            // e.g. "python", "cpp"
  std::string class_path;          // qualified name, e.g. "torch::nn::Linear"
  std::vector<ClassMethodSig> methods;
  std::vector<ClassAttrSig> attrs;
  std::string lang_version_pin;    // Resolved by sema.
};

// Member access: obj.member
/** @brief MemberExpression data structure. */
struct MemberExpression : Expression {
  std::shared_ptr<Expression> object;
  std::string member;
};

// Index access: arr[idx]
/** @brief IndexExpression data structure. */
struct IndexExpression : Expression {
  std::shared_ptr<Expression> object;
  std::shared_ptr<Expression> index;
};

// Range expression: 0..10
/** @brief RangeExpression data structure. */
struct RangeExpression : Expression {
  std::shared_ptr<Expression> start;
  std::shared_ptr<Expression> end;
};

// List literal: [1, 2, 3]
/** @brief ListLiteral data structure. */
struct ListLiteral : Expression {
  std::vector<std::shared_ptr<Expression>> elements;
};

// Tuple literal: (1, "hello", 3.14)
/** @brief TupleLiteral data structure. */
struct TupleLiteral : Expression {
  std::vector<std::shared_ptr<Expression>> elements;
};

// Dict literal: {"key1": value1, "key2": value2}
/** @brief DictLiteral data structure. */
struct DictLiteral : Expression {
  /** @brief Entry data structure. */
  struct Entry {
    std::shared_ptr<Expression> key;
    std::shared_ptr<Expression> value;
  };
  std::vector<Entry> entries;
};

// Struct literal: Point { x: 1.0, y: 2.0 }
/** @brief StructLiteral data structure. */
struct StructLiteral : Expression {
  std::string struct_name;
  /** @brief FieldInit data structure. */
  struct FieldInit {
    std::string name;
    std::shared_ptr<Expression> value;
  };
  std::vector<FieldInit> fields;
};

// Explicit type conversion: CONVERT(expr, TargetType)
/** @brief ConvertExpression data structure. */
struct ConvertExpression : Expression {
  std::shared_ptr<Expression> expr;
  std::shared_ptr<TypeNode> target_type;
};

// ============================================================================
// Statement Nodes
// ============================================================================

// LINK(target_lang, source_lang, target_func, source_func) { ... }
/** @brief LinkDecl data structure. */
struct LinkDecl : Statement {
  /** @brief LinkKind enumeration. */
  enum class LinkKind { kFunction, kVariable, kStruct };
  LinkKind link_kind{LinkKind::kFunction};
  std::string target_language;
  std::string source_language;
  std::string target_symbol;
  std::string source_symbol;
  // Optional RETURNS clause: the return type of the target function
  std::shared_ptr<TypeNode> return_type; // nullptr if not specified
  // Optional body containing MAP_TYPE directives
  std::vector<std::shared_ptr<Statement>> body;
  bool is_legacy_form{false};
};

// IMPORT "path" AS alias; or IMPORT lang::module; or IMPORT lang PACKAGE pkg AS alias;
// Extended with version constraints and selective imports:
//   IMPORT python PACKAGE numpy >= 1.20;
//   IMPORT python PACKAGE numpy::(array, mean);
/** @brief ImportDecl data structure. */
struct ImportDecl : Statement {
  std::string module_path;
  std::string alias;
  std::string language;     // empty if importing by path
  std::string package_name; // non-empty when importing a language-specific package

  // Version constraint: ">=", "<=", "==", ">", "<", "~="
  std::string version_op;
  std::string version_constraint; // e.g. "1.20", "2.0.0"

  // Selective imports: IMPORT python PACKAGE numpy::(array, mean);
  std::vector<std::string> selected_symbols;

  // Virtual environment path (populated from CONFIG VENV directive)
  std::string venv_path;
};

// EXPORT function_name; or EXPORT function_name AS "external_name";
/** @brief ExportDecl data structure. */
struct ExportDecl : Statement {
  std::string symbol_name;
  std::string external_name; // empty if no AS clause
};

// MAP_TYPE(source_type, target_type);
/** @brief MapTypeDecl data structure. */
struct MapTypeDecl : Statement {
  std::shared_ptr<TypeNode> source_type;
  std::shared_ptr<TypeNode> target_type;
};

// PIPELINE name { ... }
/** @brief PipelineDecl data structure. */
struct PipelineDecl : Statement {
  std::string name;
  std::vector<std::shared_ptr<Statement>> body;
};

// STAGE <name> CALL <lang>::<func>;
/** @brief StageDecl data structure. */
struct StageDecl : Statement {
  std::string name;
  // Language-qualified call target, e.g. cpp::math::add
  std::string call_target;
  // Optional language label for the stage (redundant with call_target,
  // kept for clarity in tools).
  std::string language;
};

// FUNC name(params) -> return_type { ... }
/** @brief FuncDecl data structure. */
struct FuncDecl : Statement {
  /** @brief Param data structure. */
  struct Param {
    std::string name;
    std::shared_ptr<TypeNode> type;
    // Optional compile-time default value used to fill the slot when the
    // call site omits this parameter.  Sema validates that the expression
    // is a constant-foldable literal/unary/binary so the lowering pass can
    // safely materialise it as an inline constant at every call site.
    std::shared_ptr<Expression> default_value;
  };
  // Generic type parameter declared in `<T: Bound1+Bound2, U>` after the
  // function name (since v1.15.0).  WHERE clauses are merged into the
  // bounds of the matching parameter during parsing.  An empty
  // `type_params` vector marks a non-generic function.
  struct TypeParam {
    std::string name;
    std::vector<std::string> bounds;
    core::SourceLoc loc{};
  };
  std::string name;
  std::vector<TypeParam> type_params;
  std::vector<Param> params;
  std::shared_ptr<TypeNode> return_type; // nullptr means VOID
  std::vector<std::shared_ptr<Statement>> body;
  // Set when the declaration is prefixed with the `ASYNC` keyword.  The
  // declared return type `T` is implicitly wrapped as `Future<T>` at the
  // ABI boundary; tooling continues to surface the raw `T` per the
  // language specification.  Resolved by sema and consumed by lowering
  // to emit cooperative-scheduler intrinsics (since v1.14.0).
  bool is_async{false};
  // Module-boundary visibility (since v1.16.0).  Defaults to kPrivate;
  // sema requires `EXPORT` targets to carry kPub.
  Visibility visibility{Visibility::kPrivate};
  // True when the source explicitly wrote `PUB` or `PRIVATE`; pre-v1.16.0
  // sources leave this false and are treated leniently by EXPORT sema.
  bool visibility_explicit{false};
  // `@name(args)` annotations recognised by sema (since v1.16.0).
  std::vector<Attribute> attributes;
  // `///` doc-comment block attached to this declaration (since v1.18.0).
  // One entry per source line, in source order, with the leading marker
  // and one optional space already stripped by the lexer.  Empty when no
  // doc comment was present immediately above the declaration.
  std::vector<std::string> doc_comment;
};

// LET x: TYPE = expr; or VAR y: TYPE = expr;
/** @brief VarDecl data structure. */
struct VarDecl : Statement {
  std::string name;
  bool is_mutable{false};         // VAR = true, LET = false
  std::shared_ptr<TypeNode> type; // nullptr for type inference
  std::shared_ptr<Expression> init;
  // `///` doc-comment block (since v1.18.0); see FuncDecl::doc_comment.
  std::vector<std::string> doc_comment;
};

// Expression statement: expr;
/** @brief ExprStatement data structure. */
struct ExprStatement : Statement {
  std::shared_ptr<Expression> expr;
};

// PRINTLN "literal";
//
// Writes a single string literal verbatim to the host's standard output and
// (per current language definition) appends no automatic line terminator —
// the literal must include any desired CRLF/LF inside the source program.
// `message` carries the *decoded* string (the lexer has already stripped
// quotes and resolved escape sequences); a trailing NUL is NOT included.
/** @brief PrintlnStmt data structure. */
struct PrintlnStmt : Statement {
  std::string message;
};

// RETURN expr;
/** @brief ReturnStatement data structure. */
struct ReturnStatement : Statement {
  std::shared_ptr<Expression> value; // nullptr for bare RETURN
};

// Block of statements: { ... }
/** @brief BlockStatement data structure. */
struct BlockStatement : Statement {
  std::vector<std::shared_ptr<Statement>> statements;
};

// IF condition { ... } ELSE IF condition { ... } ELSE { ... }
/** @brief IfStatement data structure. */
struct IfStatement : Statement {
  std::shared_ptr<Expression> condition;
  std::vector<std::shared_ptr<Statement>> then_body;
  std::vector<std::shared_ptr<Statement>> else_body; // may contain another IfStatement
};

// IF LET Some(x) = opt { ... } ELSE { ... }   (since v1.18.0)
//
// Conditional binding-and-unwrap of an OPTION<T> value.  When `scrutinee`
// matches the constructor named `ctor` (currently `Some` or `None`), the
// `bindings` are introduced into the THEN-body's scope.  `Some` requires
// exactly one binding; `None` requires zero.  ELSE-body is optional.
struct IfLetStatement : Statement {
  std::string ctor;                                  // "Some" or "None"
  std::vector<std::string> bindings;                 // names introduced
  std::shared_ptr<Expression> scrutinee;             // the OPTION<T> value
  std::vector<std::shared_ptr<Statement>> then_body;
  std::vector<std::shared_ptr<Statement>> else_body;
};

// WHILE condition { ... }
/** @brief WhileStatement data structure. */
struct WhileStatement : Statement {
  std::shared_ptr<Expression> condition;
  std::vector<std::shared_ptr<Statement>> body;
};

// FOR item IN collection { ... }
/** @brief ForStatement data structure. */
struct ForStatement : Statement {
  std::string iterator_name;
  std::shared_ptr<Expression> iterable;
  std::vector<std::shared_ptr<Statement>> body;
};

// ============================================================================
// MATCH patterns (demand 2026-04-28-10)
// ============================================================================
//
// `Pattern` is a separate AST family from `Expression` because pattern
// grammar overlaps but is not a subset of expression grammar (binding
// `name @ sub`, struct rest `..`, type guard `name: T`, etc.).  The
// MatchStatement::Case keeps each pattern alongside an optional `IF`
// guard and a list of body statements.

// Wildcard pattern: `_` — matches any value, binds nothing.
/** @brief WildcardPattern data structure. */
struct WildcardPattern : Pattern {};

// Literal pattern: a constant scrutinised by structural equality.
/** @brief LiteralPattern data structure. */
struct LiteralPattern : Pattern {
  std::shared_ptr<Literal> literal;
};

// Identifier pattern: a single bare name binds the scrutinee unconditionally.
// (Unlike Rust we do not have a `const` shadowing rule — bare names always
// bind, mirroring the rest of `.ploy`'s let-style introduction.)
/** @brief IdentifierPattern data structure. */
struct IdentifierPattern : Pattern {
  std::string name;
};

// Range pattern: `lo..hi` (exclusive) or `lo..=hi` (inclusive).
/** @brief RangePattern data structure. */
struct RangePattern : Pattern {
  std::shared_ptr<Literal> low;
  std::shared_ptr<Literal> high;
  bool inclusive{false};
};

// Tuple destructuring: `(p1, p2, ...)`.
/** @brief TuplePattern data structure. */
struct TuplePattern : Pattern {
  std::vector<std::shared_ptr<Pattern>> elements;
};

// Struct destructuring: `Point { x, y, .. }`. A field with no explicit
// sub-pattern is shorthand for `field: <IdentifierPattern field>`.
/** @brief StructPattern data structure. */
struct StructPattern : Pattern {
  /** @brief FieldPattern data structure (one row inside a struct pattern). */
  struct FieldPattern {
    std::string name;
    std::shared_ptr<Pattern> sub; // nullptr means shorthand `field` => `field: field`
  };
  std::string struct_name;
  std::vector<FieldPattern> fields;
  bool has_rest{false}; // trailing `..`
};

// Or-pattern: `p1 | p2 | ...`. All alternatives must bind the same set of
// names (sema-enforced) so that a uniform body can refer to them.
/** @brief OrPattern data structure. */
struct OrPattern : Pattern {
  std::vector<std::shared_ptr<Pattern>> alternatives;
};

// Binding pattern: `name @ sub` — bind `name` to the whole scrutinee while
// also asserting the sub-pattern matches.
/** @brief BindingPattern data structure. */
struct BindingPattern : Pattern {
  std::string name;
  std::shared_ptr<Pattern> sub;
};

// Constructor pattern: `Some(x)`, `None`, generally `Name(p1, p2, ...)`.
// Used for `OPTION` destructuring and (forward-compatibly) any nominal
// algebraic constructor.
/** @brief ConstructorPattern data structure. */
struct ConstructorPattern : Pattern {
  std::string name;
  std::vector<std::shared_ptr<Pattern>> args;
};

// Type-guard binding pattern: `name : Type`. The scrutinee must be
// runtime-assignable to `Type`; on success, `name` is bound with that
// refined static type. Combined with the case-level `IF` guard this gives
// the demand's `CASE x: i32 IF x > 0` form.
/** @brief TypePattern data structure. */
struct TypePattern : Pattern {
  std::string name;                     // empty allowed: anonymous type test
  std::shared_ptr<TypeNode> type_node;
};

// MATCH value { CASE pattern { ... } ... DEFAULT { ... } }
/** @brief MatchStatement data structure. */
struct MatchStatement : Statement {
  std::shared_ptr<Expression> value;
  /** @brief Case data structure. */
  struct Case {
    std::shared_ptr<Pattern> pattern;       // nullptr for DEFAULT
    std::shared_ptr<Expression> guard;      // optional `IF expr` clause
    std::vector<std::shared_ptr<Statement>> body;
  };
  std::vector<Case> cases;
};

// BREAK;
/** @brief BreakStatement data structure. */
struct BreakStatement : Statement {};

// CONTINUE;
/** @brief ContinueStatement data structure. */
struct ContinueStatement : Statement {};

// THROW <expression>;
//
// Raises a polyglot Error.  The expression must evaluate to either an
// existing `Error` handle (re-throw) or a value convertible to one
// (string literal, user-defined Error-shaped struct).  Lowering
// translates this to a call to `__ploy_rt_throw`.
/** @brief ThrowStatement data structure. */
struct ThrowStatement : Statement {
  std::shared_ptr<Expression> value;
};

// TRY { body } CATCH (name: Error) { handler } FINALLY { cleanup }
//
// Structured exception handling.  Both `catches` and `finally_body`
// are optional — `TRY { … } FINALLY { … }` (no catch) and
// `TRY { … } CATCH (…) { … }` (no finally) are both valid.  Multiple
// `CATCH` clauses are supported in the AST so future demands can add
// type-discriminating handlers without an AST change.
/** @brief TryStatement data structure. */
struct TryStatement : Statement {
  /** @brief CatchClause data structure. */
  struct CatchClause {
    core::SourceLoc loc;
    std::string var_name;                 // bound name for the caught Error
    std::shared_ptr<TypeNode> var_type;   // declared type (defaults to Error)
    std::vector<std::shared_ptr<Statement>> body;
  };

  std::vector<std::shared_ptr<Statement>> body;
  std::vector<CatchClause> catches;
  std::vector<std::shared_ptr<Statement>> finally_body;
  bool has_finally{false};
};

// WITH(language, expression) AS name { body }
// Automatic resource management: calls __enter__ before and __exit__ after the body
/** @brief WithStatement data structure. */
struct WithStatement : Statement {
  std::string language;
  std::shared_ptr<Expression> resource_expr;
  std::string var_name; // bound variable name after AS
  std::vector<std::shared_ptr<Statement>> body;
  std::string lang_version_pin; // Resolved by sema.
};

// STRUCT Name { field1: Type1, field2: Type2, ... }
/** @brief StructDecl data structure. */
struct StructDecl : Statement {
  std::string name;
  // Generic type parameter declared in `<A, B>` after the struct name
  // (since v1.15.0).  An empty vector marks a non-generic struct.
  std::vector<FuncDecl::TypeParam> type_params;
  /** @brief Field data structure. */
  struct Field {
    std::string name;
    std::shared_ptr<TypeNode> type;
  };
  std::vector<Field> fields;
  // Module-boundary visibility (since v1.16.0).
  Visibility visibility{Visibility::kPrivate};
  bool visibility_explicit{false};
  // `@name(args)` annotations recognised by sema (since v1.16.0).
  std::vector<Attribute> attributes;
  // `///` doc-comment block (since v1.18.0); see FuncDecl::doc_comment.
  std::vector<std::string> doc_comment;
};
// MAP_FUNC name(params) -> ReturnType { body }
// A specialised function for type mapping between languages
/** @brief MapFuncDecl data structure. */
struct MapFuncDecl : Statement {
  std::string name;
  std::vector<FuncDecl::Param> params;
  std::shared_ptr<TypeNode> return_type;
  std::vector<std::shared_ptr<Statement>> body;
};

// TYPE <name> = <type_expr>;
//
// Declares a name that is a textual alias for an existing type expression.
// The alias has no run-time representation of its own; sema substitutes the
// aliased type at every use-site, but remembers the original alias name for
// diagnostics so an error message can read e.g.
//   "type mismatch: expected `Pixel` (alias of `i32`) but got `f32`".
//
// Aliases are resolved eagerly at the point of declaration; forward
// references to types declared later in the module are NOT supported in
// this version (mirrors C++'s `using` rule rather than Rust's `type`).
/** @brief TypeAliasDecl data structure. */
struct TypeAliasDecl : Statement {
  std::string name;
  std::shared_ptr<TypeNode> aliased_type;
};

// CONST <name>: <type> = <const_expr>;
//
// Declares a compile-time constant.  The initializer must be foldable by
// the sema's constant-evaluation pass: integer / float / boolean / string
// literals, references to previously declared CONSTs, and the unary `-`,
// `!`, plus binary arithmetic / comparison operators.  The declared type
// is mandatory (no inference) because CONSTs participate in const-
// propagation across modules and need a stable interface.
/** @brief ConstDecl data structure. */
struct ConstDecl : Statement {
  std::string name;
  std::shared_ptr<TypeNode> type;       ///< Declared type — mandatory.
  std::shared_ptr<Expression> value;    ///< Constant initializer expression.
};

// Legacy form (kept for backward compatibility, sema emits a deprecation
// warning suggesting the new stringified form):
//   CONFIG VENV "path/to/venv";
//   CONFIG CONDA env_name;
//   CONFIG UV "path/to/venv";
//   CONFIG PIPENV "path/to/project";
//   CONFIG POETRY "path/to/project";
//
// Canonical form (since 1.12.0):
//   CONFIG <language> "<package_manager>" "<path_or_env>";
// e.g.
//   CONFIG python "venv"   ".venv";
//   CONFIG python "conda"  "myenv";
//   CONFIG rust   "cargo"  ".";
//   CONFIG javascript "npm"     "./node_modules";
//   CONFIG java       "maven"   "./pom.xml";
//   CONFIG dotnet     "nuget"   "./packages";
//   CONFIG ruby       "bundler" "./Gemfile";
//   CONFIG go         "gomod"   "./go.mod";
//
// Both forms produce a `VenvConfigDecl` AST node.  `is_legacy_form` is
// `true` for any node parsed via the legacy keyword-driven path; sema
// uses that flag to decide whether to surface the deprecation warning.
// `manager_name` always carries the canonical lower-case manager string
// (`"venv"`, `"conda"`, `"npm"`, ...) so downstream consumers do not
// need to switch on the enum to recover the human-readable spelling.
/** @brief VenvConfigDecl data structure. */
struct VenvConfigDecl : Statement {
  /** @brief ManagerKind enumeration. */
  enum class ManagerKind {
    kVenv,    // Standard Python venv / virtualenv
    kConda,   // Conda environment
    kUv,      // uv-managed virtual environment
    kPipenv,  // Pipenv project
    kPoetry,  // Poetry project
    kNpm,     // JavaScript / TypeScript node_modules tree
    kCargo,   // Rust cargo workspace
    kMaven,   // Java maven project (pom.xml)
    kNuget,   // .NET nuget package directory
    kBundler, // Ruby bundler Gemfile
    kGoMod,   // Go go.mod-rooted module
    kUnknown  // Resolution failed / unregistered manager
  };
  ManagerKind manager{ManagerKind::kVenv};
  std::string language;     // canonical short language name (e.g. "python")
  std::string venv_path;    // path to the virtual environment / project / env name
  std::string manager_name; // canonical manager string ("venv", "npm", ...)
  bool is_legacy_form{false}; // true when parsed from `CONFIG VENV "..."` etc.
};

// ============================================================================
// Module (top-level AST root)
// ============================================================================

// Language-version pinning syntax.
//
// `LANG <lang> = <version>;`             鈥?module-wide pin (top-level only).
// `WITH LANG (<lang>=<ver>, ...) { ... }` 鈥?scoped pin around a block.
// `@LANG(<lang>=<ver>, ...) <stmt>`       鈥?single-statement annotation.
//
// All three forms feed the same scope-stack consulted by sema, which then
// stamps the resolved value into every cross-language node's
// `lang_version_pin` field. The string format matches the canonical tokens
// produced by `polyglot::frontends::CanonicalizeLanguageVersion` (e.g.
// `"c++23"`, `"3.11"`, `"net8"`, `"2021"`).

/** @brief LangPragma data structure. */
struct LangPragma : Statement {
  std::string language; // canonical short name: cpp / python / java / dotnet / rust / go / javascript / ruby
  std::string version;  // canonical version token; empty == "auto"
};

/** @brief WithLangBlock data structure. */
struct WithLangBlock : Statement {
  /** @brief Pin: one (language, version) pair. */
  struct Pin {
    std::string language;
    std::string version;
  };
  std::vector<Pin> pins;
  std::vector<std::shared_ptr<Statement>> body;
};

/** @brief LangAnnotation data structure. */
struct LangAnnotation : Statement {
  std::vector<WithLangBlock::Pin> pins;
  std::shared_ptr<Statement> target; // exactly one statement
};

/** @brief Module data structure. */
struct Module {
  std::string filename;
  std::vector<std::shared_ptr<Statement>> declarations;
};

} // namespace polyglot::ploy
