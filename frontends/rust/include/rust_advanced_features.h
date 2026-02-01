#pragma once

#include <memory>
#include <string>
#include <vector>

#include "frontends/rust/include/rust_ast.h"

namespace polyglot::rust {

// Rust高级特性支持

// 1. 特征（Traits）
struct TraitDecl : Statement {
  std::string name;
  std::vector<std::string> type_params;
  std::vector<std::string> super_traits;  // trait bounds
  std::vector<std::shared_ptr<FunctionDecl>> methods;
  std::vector<std::shared_ptr<TypeAliasDecl>> associated_types;

  TraitDecl() { kind = StatementKind::kTrait; }
};

// 2. 特征实现（Impl）
struct ImplDecl : Statement {
  std::string trait_name;  // 为空则是固有实现
  std::shared_ptr<TypeNode> for_type;
  std::vector<std::string> type_params;
  std::vector<std::shared_ptr<FunctionDecl>> methods;
  std::vector<std::shared_ptr<TypeAliasDecl>> associated_items;

  ImplDecl() { kind = StatementKind::kImpl; }
};

// 3. 生命周期（Lifetimes）
struct Lifetime {
  std::string name;  // 'a, 'b, 'static
  bool is_static{false};

  bool operator==(const Lifetime &other) const {
    return name == other.name && is_static == other.is_static;
  }
};

// 4. 生命周期约束
struct LifetimeBound {
  Lifetime lifetime;
  std::vector<Lifetime> bounds;  // 'a: 'b表示'a比'b活得长
};

// 5. 完整的借用检查器数据结构
struct BorrowInfo {
  enum Kind { kShared, kMutable, kMove };
  Kind kind;
  Lifetime lifetime;
  std::string borrowed_var;
  core::SourceLoc loc;
};

struct BorrowChecker {
  std::vector<BorrowInfo> active_borrows;
  std::vector<std::string> moved_vars;  // 已移动的变量

  // 检查借用冲突
  bool CheckBorrow(const BorrowInfo &borrow);
  
  // 检查移动后使用
  bool CheckMoveAfterUse(const std::string &var);

  // 清理生命周期结束的借用
  void EndLifetime(const Lifetime &lifetime);
};

// 6. 所有权语义
struct OwnershipInfo {
  enum Ownership { kOwned, kBorrowed, kMutBorrowed };
  Ownership ownership;
  Lifetime lifetime;
};

// 7. 闭包（Closures）
struct ClosureExpression : Expression {
  enum CaptureMode { kMove, kRef, kMutRef };
  CaptureMode capture_mode{kRef};
  
  std::vector<std::string> params;
  std::vector<std::shared_ptr<TypeNode>> param_types;
  std::shared_ptr<TypeNode> return_type;
  std::vector<std::shared_ptr<Statement>> body;
  
  // 捕获的变量
  std::vector<std::string> captures;
  std::vector<OwnershipInfo> capture_ownership;

  ClosureExpression() { kind = ExpressionKind::kClosure; }
};

// 8. 模式匹配（Pattern Matching）
struct Pattern {
  enum PatternKind {
    kWildcard,     // _
    kLiteral,      // 42, "hello"
    kVariable,     // x
    kTuple,        // (x, y)
    kStruct,       // Point { x, y }
    kEnum,         // Some(x), None
    kSlice,        // [x, y, ..]
    kRange,        // 1..=10
    kOr,           // x | y
    kRef,          // &x, &mut x
  };

  PatternKind pattern_kind;
  std::string name;  // for variable, struct, enum
  std::vector<std::shared_ptr<Pattern>> subpatterns;
  std::shared_ptr<Expression> value;  // for literal
  bool is_mutable{false};
};

struct MatchArm {
  std::shared_ptr<Pattern> pattern;
  std::shared_ptr<Expression> guard;  // if条件
  std::vector<std::shared_ptr<Statement>> body;
};

struct MatchExpression : Expression {
  std::shared_ptr<Expression> scrutinee;
  std::vector<MatchArm> arms;

  MatchExpression() { kind = ExpressionKind::kMatch; }
};

// 9. 枚举（Enums）
struct EnumVariant {
  std::string name;
  enum VariantKind { kUnit, kTuple, kStruct };
  VariantKind variant_kind;
  
  // Tuple variant
  std::vector<std::shared_ptr<TypeNode>> tuple_fields;
  
  // Struct variant
  std::vector<std::string> field_names;
  std::vector<std::shared_ptr<TypeNode>> field_types;
  
  int discriminant{-1};  // 可选的判别值
};

struct EnumDecl : Statement {
  std::string name;
  std::vector<std::string> type_params;
  std::vector<EnumVariant> variants;

  EnumDecl() { kind = StatementKind::kEnum; }
};

// 10. 泛型约束（Generic Constraints）
struct TypeBound {
  enum BoundKind { kTrait, kLifetime, kSized };
  BoundKind bound_kind;
  std::string trait_name;
  Lifetime lifetime;
};

struct GenericParam {
  std::string name;
  std::vector<TypeBound> bounds;
  std::shared_ptr<TypeNode> default_type;
};

// 11. 关联类型（Associated Types）
struct AssociatedType {
  std::string name;
  std::vector<TypeBound> bounds;
  std::shared_ptr<TypeNode> default_type;
};

// 12. 宏（Macros）
struct MacroInvocation : Expression {
  std::string macro_name;
  std::vector<std::string> tokens;  // 原始token流

  MacroInvocation() { kind = ExpressionKind::kMacro; }
};

struct MacroDecl : Statement {
  std::string name;
  std::vector<std::string> rules;  // macro_rules! patterns

  MacroDecl() { kind = StatementKind::kMacro; }
};

// 13. 属性（Attributes）
struct Attribute {
  std::string name;
  std::vector<std::string> args;
  bool is_outer{true};  // #[] vs #![]
};

// 14. 可见性（Visibility）
struct Visibility {
  enum Level { kPrivate, kPub, kPubCrate, kPubSuper, kPubIn };
  Level level{kPrivate};
  std::string path;  // for pub(in path)
};

// 15. 模块（Modules）
struct ModuleDecl : Statement {
  std::string name;
  std::vector<std::shared_ptr<Statement>> items;
  Visibility visibility;
  bool is_inline{true};  // vs. mod name; (external file)

  ModuleDecl() { kind = StatementKind::kModule; }
};

// 16. use声明（Use Declarations）
struct UseDecl : Statement {
  std::vector<std::string> path;
  std::string alias;
  bool is_glob{false};  // use path::*;
  std::vector<std::string> items;  // use path::{A, B, C};

  UseDecl() { kind = StatementKind::kUse; }
};

// 17. 常量和静态变量
struct ConstDecl : Statement {
  std::string name;
  std::shared_ptr<TypeNode> type;
  std::shared_ptr<Expression> value;
  Visibility visibility;

  ConstDecl() { kind = StatementKind::kConst; }
};

struct StaticDecl : Statement {
  std::string name;
  std::shared_ptr<TypeNode> type;
  std::shared_ptr<Expression> value;
  bool is_mutable{false};
  Visibility visibility;

  StaticDecl() { kind = StatementKind::kStatic; }
};

// 18. 类型别名
struct TypeAliasDecl : Statement {
  std::string name;
  std::vector<GenericParam> type_params;
  std::shared_ptr<TypeNode> aliased_type;
  Visibility visibility;

  TypeAliasDecl() { kind = StatementKind::kTypeAlias; }
};

// 19. 智能指针类型
struct SmartPointerType : TypeNode {
  enum PointerKind { kBox, kRc, kArc, kCell, kRefCell };
  PointerKind pointer_kind;
  std::shared_ptr<TypeNode> inner;

  SmartPointerType() { kind = TypeKind::kSmartPointer; }
};

// 20. 切片类型（Slice）
struct SliceType : TypeNode {
  std::shared_ptr<TypeNode> element_type;

  SliceType() { kind = TypeKind::kSlice; }
};

// 21. 元组类型
struct TupleType : TypeNode {
  std::vector<std::shared_ptr<TypeNode>> elements;

  TupleType() { kind = TypeKind::kTuple; }
};

// 22. 函数指针类型
struct FnPointerType : TypeNode {
  std::vector<std::shared_ptr<TypeNode>> param_types;
  std::shared_ptr<TypeNode> return_type;
  bool is_unsafe{false};

  FnPointerType() { kind = TypeKind::kFnPointer; }
};

// 23. 引用类型增强
struct ReferenceType : TypeNode {
  std::shared_ptr<TypeNode> referent;
  bool is_mutable{false};
  Lifetime lifetime;

  ReferenceType() { kind = TypeKind::kReference; }
};

// 24. Unsafe代码块
struct UnsafeBlock : Statement {
  std::vector<std::shared_ptr<Statement>> body;

  UnsafeBlock() { kind = StatementKind::kUnsafe; }
};

// 25. 异步/等待（Async/Await）
struct AsyncBlock : Expression {
  std::vector<std::shared_ptr<Statement>> body;

  AsyncBlock() { kind = ExpressionKind::kAsync; }
};

struct AwaitExpression : Expression {
  std::shared_ptr<Expression> future;

  AwaitExpression() { kind = ExpressionKind::kAwait; }
};

// 26. 区间表达式（Range）
struct RangeExpression : Expression {
  enum RangeKind { kExclusive, kInclusive, kFrom, kTo, kFull };
  RangeKind range_kind;
  std::shared_ptr<Expression> start;
  std::shared_ptr<Expression> end;

  RangeExpression() { kind = ExpressionKind::kRange; }
};

// 27. 解引用强制转换（Deref Coercion）
struct DerefCoercion {
  std::shared_ptr<TypeNode> from_type;
  std::shared_ptr<TypeNode> to_type;
  int deref_count{0};
};

// 28. 自动引用/解引用
struct AutoRef {
  bool add_ref{false};
  bool add_mut_ref{false};
  int deref_count{0};
};

}  // namespace polyglot::rust
