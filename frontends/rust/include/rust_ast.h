/**
 * @file     rust_ast.h
 * @brief    Rust language frontend
 *
 * @ingroup  Frontend / Rust
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "common/include/core/source_loc.h"

namespace polyglot::rust {

/** @brief Attribute data structure. */
struct Attribute {
    std::string text;
    bool is_inner{false};
};

/** @brief AstNode data structure. */
struct AstNode {
    virtual ~AstNode() = default;
    core::SourceLoc loc{};
    std::vector<Attribute> attributes;
};

/** @brief Statement data structure. */
struct Statement : AstNode {};

/** @brief Expression data structure. */
struct Expression : AstNode {};

/** @brief Pattern data structure. */
struct Pattern : AstNode {};

/** @brief TypeNode data structure. */
struct TypeNode : AstNode {};

/** @brief IdentifierPattern data structure. */
struct IdentifierPattern : Pattern {
    std::string name;
    bool is_ref{false};
    bool is_mut{false};
};

/** @brief WildcardPattern data structure. */
struct WildcardPattern : Pattern {};

/** @brief TuplePattern data structure. */
struct TuplePattern : Pattern {
    std::vector<std::shared_ptr<Pattern>> elements;
};

/** @brief LiteralPattern data structure. */
struct LiteralPattern : Pattern {
    std::string value;
};

/** @brief PathPattern data structure. */
struct PathPattern : Pattern {
    bool is_absolute{false};
    std::vector<std::string> segments;
};

/** @brief OrPattern data structure. */
struct OrPattern : Pattern {
    std::vector<std::shared_ptr<Pattern>> patterns;
};

/** @brief RefPattern data structure. */
struct RefPattern : Pattern {
    bool is_mut{false};
    std::shared_ptr<Pattern> inner;
};

/** @brief RangePattern data structure. */
struct RangePattern : Pattern {
    std::shared_ptr<Pattern> start;
    std::shared_ptr<Pattern> end;
    bool inclusive{false};
};

/** @brief SlicePattern data structure. */
struct SlicePattern : Pattern {
    std::vector<std::shared_ptr<Pattern>> elements;
    bool has_rest{false};
};

/** @brief TupleStructPattern data structure. */
struct TupleStructPattern : Pattern {
    PathPattern path;
    std::vector<std::shared_ptr<Pattern>> elements;
};

/** @brief BindingPattern data structure. */
struct BindingPattern : Pattern {
    std::string name;
    bool is_ref{false};
    bool is_mut{false};
    std::shared_ptr<Pattern> pattern;
};

/** @brief StructPatternField data structure. */
struct StructPatternField {
    std::string name;
    std::shared_ptr<Pattern> pattern;
    bool is_shorthand{false};
};

/** @brief StructPattern data structure. */
struct StructPattern : Pattern {
    PathPattern path;
    std::vector<StructPatternField> fields;
    bool has_rest{false};
};

/** @brief Identifier data structure. */
struct Identifier : Expression {
    std::string name;
};

/** @brief Literal data structure. */
struct Literal : Expression {
    std::string value;
};

/** @brief PathExpression data structure. */
struct PathExpression : Expression {
    bool is_absolute{false};
    std::vector<std::string> segments;
    std::vector<std::vector<std::shared_ptr<TypeNode>>> generic_args;
};

/** @brief UnaryExpression data structure. */
struct UnaryExpression : Expression {
    std::string op;
    std::shared_ptr<Expression> operand;
};

/** @brief BinaryExpression data structure. */
struct BinaryExpression : Expression {
    std::string op;
    std::shared_ptr<Expression> left;
    std::shared_ptr<Expression> right;
};

/** @brief MemberExpression data structure. */
struct MemberExpression : Expression {
    std::shared_ptr<Expression> object;
    std::string member;
    std::vector<std::shared_ptr<TypeNode>> generic_args;
};

/** @brief IndexExpression data structure. */
struct IndexExpression : Expression {
    std::shared_ptr<Expression> object;
    std::shared_ptr<Expression> index;
};

/** @brief RangeExpression data structure. */
struct RangeExpression : Expression {
    /** @brief RangeKind enumeration. */
    enum class RangeKind { kExclusive, kInclusive, kFrom, kTo, kFull };
    RangeKind kind{RangeKind::kExclusive};
    std::shared_ptr<Expression> start;
    std::shared_ptr<Expression> end;
    bool inclusive{false};
};

/** @brief CallExpression data structure. */
struct CallExpression : Expression {
    std::shared_ptr<Expression> callee;
    std::vector<std::shared_ptr<Expression>> args;
};

/** @brief AwaitExpression data structure. */
struct AwaitExpression : Expression {
    std::shared_ptr<Expression> value;
    std::shared_ptr<Expression> future;
};

/** @brief TryExpression data structure. */
struct TryExpression : Expression {
    std::shared_ptr<Expression> value;
};

/** @brief ClosureExpression data structure. */
struct ClosureExpression : Expression {
    bool is_move{false};
    /** @brief Param data structure. */
    struct Param {
        std::string name;
        std::shared_ptr<TypeNode> type;
    };
    std::vector<Param> params;
    std::shared_ptr<Expression> body;
};

/** @brief MacroCallExpression data structure. */
struct MacroCallExpression : Expression {
    PathExpression path;
    std::string delimiter;
    std::string body;
};

/** @brief AssignmentExpression data structure. */
struct AssignmentExpression : Expression {
    std::string op;
    std::shared_ptr<Expression> left;
    std::shared_ptr<Expression> right;
};

/** @brief TypePath data structure. */
struct TypePath : TypeNode {
    bool is_absolute{false};
    std::vector<std::string> segments;
    std::vector<std::vector<std::shared_ptr<TypeNode>>> generic_args;
};

/** @brief ConstExprType data structure. */
struct ConstExprType : TypeNode {
    std::string expr;
};

/** @brief TraitObjectType data structure. */
struct TraitObjectType : TypeNode {
    std::vector<std::shared_ptr<TypeNode>> bounds;
};

/** @brief ImplTraitType data structure. */
struct ImplTraitType : TypeNode {
    std::vector<std::shared_ptr<TypeNode>> bounds;
};

/** @brief LifetimeType data structure. */
struct LifetimeType : TypeNode {
    std::string name;
};

/** @brief ReferenceType data structure. */
struct ReferenceType : TypeNode {
    bool is_mut{false};
    std::shared_ptr<LifetimeType> lifetime;
    std::shared_ptr<TypeNode> inner;
};

/** @brief SliceType data structure. */
struct SliceType : TypeNode {
    std::shared_ptr<TypeNode> inner;
};

/** @brief ArrayType data structure. */
struct ArrayType : TypeNode {
    std::shared_ptr<TypeNode> inner;
    std::string size_expr;
};

/** @brief TupleType data structure. */
struct TupleType : TypeNode {
    std::vector<std::shared_ptr<TypeNode>> elements;
};

/** @brief FunctionType data structure. */
struct FunctionType : TypeNode {
    std::vector<std::shared_ptr<TypeNode>> params;
    std::shared_ptr<TypeNode> return_type;
};

/** @brief LetStatement data structure. */
struct LetStatement : Statement {
    bool is_mut{false};
    std::shared_ptr<Pattern> pattern;
    std::shared_ptr<Expression> init;
    std::shared_ptr<TypeNode> type_annotation;
};

/** @brief ExprStatement data structure. */
struct ExprStatement : Statement {
    std::shared_ptr<Expression> expr;
};

/** @brief ReturnStatement data structure. */
struct ReturnStatement : Statement {
    std::shared_ptr<Expression> value;
};

/** @brief BreakStatement data structure. */
struct BreakStatement : Statement {
    std::shared_ptr<Expression> value;
};

/** @brief ContinueStatement data structure. */
struct ContinueStatement : Statement {};

/** @brief LoopStatement data structure. */
struct LoopStatement : Statement {
    std::vector<std::shared_ptr<Statement>> body;
};

/** @brief ForStatement data structure. */
struct ForStatement : Statement {
    std::shared_ptr<Pattern> pattern;
    std::shared_ptr<Expression> iterable;
    std::vector<std::shared_ptr<Statement>> body;
};

/** @brief IfExpression data structure. */
struct IfExpression : Expression {
    std::shared_ptr<Expression> condition;
    std::vector<std::shared_ptr<Statement>> then_body;
    std::vector<std::shared_ptr<Statement>> else_body;
};

/** @brief WhileExpression data structure. */
struct WhileExpression : Expression {
    std::shared_ptr<Expression> condition;
    std::vector<std::shared_ptr<Statement>> body;
};

/** @brief BlockExpression data structure. */
struct BlockExpression : Expression {
    std::vector<std::shared_ptr<Statement>> statements;
};

/** @brief MatchArm data structure. */
struct MatchArm : AstNode {
    std::shared_ptr<Pattern> pattern;
    std::shared_ptr<Expression> guard;
    std::shared_ptr<Expression> body;
};

/** @brief MatchExpression data structure. */
struct MatchExpression : Expression {
    std::shared_ptr<Expression> scrutinee;
    std::vector<std::shared_ptr<MatchArm>> arms;
};

/** @brief UseDeclaration data structure. */
struct UseDeclaration : Statement {
    std::string path;
    std::string visibility;
};

/** @brief ImplItem data structure. */
struct ImplItem : Statement {
    std::string visibility;
    bool is_unsafe{false};
    std::vector<std::string> type_params;
    std::shared_ptr<TypeNode> target_type;
    std::shared_ptr<TypeNode> trait_type;
    std::string where_clause;
    std::vector<std::shared_ptr<Statement>> items;
};

/** @brief TraitItem data structure. */
struct TraitItem : Statement {
    std::string name;
    std::string visibility;
    std::vector<std::string> type_params;
    std::vector<std::shared_ptr<TypeNode>> super_traits;
    std::string where_clause;
    std::vector<std::shared_ptr<Statement>> items;
};

/** @brief ModItem data structure. */
struct ModItem : Statement {
    std::string name;
    std::string visibility;
    std::vector<std::shared_ptr<Statement>> items;
};

/** @brief FunctionItem data structure. */
struct FunctionItem : Statement {
    std::string name;
    std::vector<std::string> type_params;
    std::string visibility;
    bool is_async{false};
    bool is_const{false};
    bool is_unsafe{false};
    bool is_extern{false};
    std::string extern_abi;
    std::string where_clause;
    bool has_body{true};
    /** @brief Param data structure. */
    struct Param {
        std::string name;
        std::shared_ptr<TypeNode> type;
    };
    std::vector<Param> params;
    std::shared_ptr<TypeNode> return_type;
    std::vector<std::shared_ptr<Statement>> body;
};

/** @brief StructField data structure. */
struct StructField {
    std::string name;
    std::shared_ptr<TypeNode> type;
    std::string visibility;
};

/** @brief StructItem data structure. */
struct StructItem : Statement {
    std::string name;
    std::string visibility;
    std::vector<StructField> fields;
    bool is_tuple{false};
    bool is_unit{false};
};

/** @brief EnumVariant data structure. */
struct EnumVariant {
    std::string name;
    std::vector<std::shared_ptr<TypeNode>> tuple_fields;
    std::vector<StructField> struct_fields;
    std::shared_ptr<Expression> discriminant;
};

/** @brief EnumItem data structure. */
struct EnumItem : Statement {
    std::string name;
    std::string visibility;
    std::vector<EnumVariant> variants;
};

/** @brief ConstItem data structure. */
struct ConstItem : Statement {
    std::string name;
    std::shared_ptr<TypeNode> type;
    std::shared_ptr<Expression> value;
    std::string visibility;
};

/** @brief TypeAliasItem data structure. */
struct TypeAliasItem : Statement {
    std::string name;
    std::shared_ptr<TypeNode> alias;
    std::string visibility;
};

/** @brief MacroRulesItem data structure. */
struct MacroRulesItem : Statement {
    std::string name;
    std::string body;
};

// Advanced Rust language features consolidated here to avoid separate headers.

/** @brief TraitDecl data structure. */
struct TraitDecl : Statement {
    std::string name;
    std::vector<std::string> type_params;
    std::vector<std::string> super_traits;
    std::vector<std::string> method_names;
    std::vector<std::string> associated_type_names;
};

/** @brief ImplDecl data structure. */
struct ImplDecl : Statement {
    std::string trait_name;
    std::shared_ptr<TypeNode> for_type;
    std::vector<std::string> type_params;
    std::vector<std::string> method_names;
    std::vector<std::string> associated_item_names;
};

/** @brief Lifetime data structure. */
struct Lifetime {
    std::string name;
    bool is_static{false};

    bool operator==(const Lifetime &other) const {
        return name == other.name && is_static == other.is_static;
    }
};

/** @brief LifetimeBound data structure. */
struct LifetimeBound {
    Lifetime lifetime;
    std::vector<Lifetime> bounds;
};

/** @brief BorrowInfo data structure. */
struct BorrowInfo {
    /** @brief Kind enumeration. */
    enum Kind { kShared, kMutable, kMove };
    Kind kind{kShared};
    Lifetime lifetime;
    std::string borrowed_var;
    core::SourceLoc loc;
};

/** @brief BorrowChecker data structure. */
struct BorrowChecker {
    std::vector<BorrowInfo> active_borrows;
    std::vector<std::string> moved_vars;

    bool CheckBorrow(const BorrowInfo &borrow) {
        for (const auto &active : active_borrows) {
            if (active.borrowed_var != borrow.borrowed_var) {
                continue;
            }
            if (borrow.kind == BorrowInfo::kMutable || active.kind == BorrowInfo::kMutable) {
                return false;  // mutable conflicts with any existing borrow of the same variable
            }
        }
        active_borrows.push_back(borrow);
        return true;
    }

    bool CheckMoveAfterUse(const std::string &var) {
        if (std::find(moved_vars.begin(), moved_vars.end(), var) != moved_vars.end()) {
            return false;
        }
        moved_vars.push_back(var);
        return true;
    }

    void EndLifetime(const Lifetime &lifetime) {
        active_borrows.erase(
            std::remove_if(active_borrows.begin(), active_borrows.end(), [&](const BorrowInfo &info) {
                return info.lifetime == lifetime;
            }),
            active_borrows.end());
    }
};

/** @brief OwnershipInfo data structure. */
struct OwnershipInfo {
    /** @brief Ownership enumeration. */
    enum Ownership { kOwned, kBorrowed, kMutBorrowed };
    Ownership ownership{kOwned};
    Lifetime lifetime;
};

/** @brief CaptureMode enumeration. */
enum CaptureMode { kMove, kRef, kMutRef };

/** @brief ClosureCaptureInfo data structure. */
struct ClosureCaptureInfo {
    CaptureMode capture_mode{kRef};
    std::vector<std::string> captures;
    std::vector<OwnershipInfo> capture_ownership;
};

/** @brief PatternExtension data structure. */
struct PatternExtension {
    /** @brief PatternKind enumeration. */
    enum PatternKind {
        kWildcard,
        kLiteral,
        kVariable,
        kTuple,
        kStruct,
        kEnum,
        kSlice,
        kRange,
        kOr,
        kRef,
    };

    PatternKind pattern_kind{kWildcard};
    std::string name;
    std::vector<std::shared_ptr<Pattern>> subpatterns;
    std::shared_ptr<Expression> value;
    bool is_mutable{false};
};

/** @brief EnumDeclExtension data structure. */
struct EnumDeclExtension {
    std::string name;
    std::vector<std::string> type_params;
    std::vector<std::string> variant_names;
};

/** @brief TypeBound data structure. */
struct TypeBound {
    /** @brief BoundKind enumeration. */
    enum BoundKind { kTrait, kLifetime, kSized };
    BoundKind bound_kind{kTrait};
    std::string trait_name;
    Lifetime lifetime;
};

/** @brief GenericParam data structure. */
struct GenericParam {
    std::string name;
    std::vector<TypeBound> bounds;
    std::shared_ptr<TypeNode> default_type;
};

/** @brief AssociatedType data structure. */
struct AssociatedType {
    std::string name;
    std::vector<TypeBound> bounds;
    std::shared_ptr<TypeNode> default_type;
};

/** @brief MacroInvocation data structure. */
struct MacroInvocation : Expression {
    std::string macro_name;
    std::vector<std::string> tokens;
};

/** @brief MacroDecl data structure. */
struct MacroDecl : Statement {
    std::string name;
    std::vector<std::string> rules;
};

/** @brief Visibility data structure. */
struct Visibility {
    /** @brief Level enumeration. */
    enum Level { kPrivate, kPub, kPubCrate, kPubSuper, kPubIn };
    Level level{kPrivate};
    std::string path;
};

/** @brief ModuleDecl data structure. */
struct ModuleDecl : Statement {
    std::string name;
    std::vector<std::shared_ptr<Statement>> items;
    Visibility visibility;
    bool is_inline{true};
};

/** @brief UseDecl data structure. */
struct UseDecl : Statement {
    std::vector<std::string> path;
    std::string alias;
    bool is_glob{false};
    std::vector<std::string> items;
};

/** @brief ConstDecl data structure. */
struct ConstDecl : Statement {
    std::string name;
    std::shared_ptr<TypeNode> type;
    std::shared_ptr<Expression> value;
    Visibility visibility;
};

/** @brief StaticDecl data structure. */
struct StaticDecl : Statement {
    std::string name;
    std::shared_ptr<TypeNode> type;
    std::shared_ptr<Expression> value;
    bool is_mutable{false};
    Visibility visibility;
};

/** @brief TypeAliasDecl data structure. */
struct TypeAliasDecl : Statement {
    std::string name;
    std::vector<GenericParam> type_params;
    std::shared_ptr<TypeNode> aliased_type;
    Visibility visibility;
};

/** @brief SmartPointerType data structure. */
struct SmartPointerType : TypeNode {
    /** @brief PointerKind enumeration. */
    enum PointerKind { kBox, kRc, kArc, kCell, kRefCell };
    PointerKind pointer_kind{kBox};
    std::shared_ptr<TypeNode> inner;
};

/** @brief FnPointerType data structure. */
struct FnPointerType : TypeNode {
    std::vector<std::shared_ptr<TypeNode>> param_types;
    std::shared_ptr<TypeNode> return_type;
    bool is_unsafe{false};
};

/** @brief UnsafeBlock data structure. */
struct UnsafeBlock : Statement {
    std::vector<std::shared_ptr<Statement>> body;
};

/** @brief AsyncBlock data structure. */
struct AsyncBlock : Expression {
    std::vector<std::shared_ptr<Statement>> body;
};

/** @brief DerefCoercion data structure. */
struct DerefCoercion {
    std::shared_ptr<TypeNode> from_type;
    std::shared_ptr<TypeNode> to_type;
    int deref_count{0};
};

/** @brief AutoRef data structure. */
struct AutoRef {
    bool add_ref{false};
    bool add_mut_ref{false};
    int deref_count{0};
};

/** @brief Module data structure. */
struct Module : AstNode {
    std::vector<std::shared_ptr<Statement>> items;
};

} // namespace polyglot::rust
