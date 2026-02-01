#pragma once

#include <memory>
#include <string>
#include <vector>

#include "frontends/python/include/python_ast.h"

namespace polyglot::python {

// Python高级特性支持

// 1. 装饰器（Decorators）
struct Decorator {
  std::string name;
  std::vector<std::shared_ptr<Expression>> args;
  core::SourceLoc loc;
};

// 2. 上下文管理器（Context Managers - with statement）
struct WithStatement : Statement {
  std::shared_ptr<Expression> context_expr;  // __enter__/__exit__对象
  std::string as_var;  // 可选的as变量
  std::vector<std::shared_ptr<Statement>> body;

  WithStatement() { kind = StatementKind::kWith; }
};

// 3. 生成器（Generators）
struct YieldExpression : Expression {
  std::shared_ptr<Expression> value;

  YieldExpression() { kind = ExpressionKind::kYield; }
};

struct YieldFromExpression : Expression {
  std::shared_ptr<Expression> value;

  YieldFromExpression() { kind = ExpressionKind::kYieldFrom; }
};

// 4. 异步支持（async/await）
struct AsyncFunctionDecl : FunctionDecl {
  bool is_async{true};
};

struct AwaitExpression : Expression {
  std::shared_ptr<Expression> value;

  AwaitExpression() { kind = ExpressionKind::kAwait; }
};

// 5. 列表推导式（List Comprehension）
struct ComprehensionClause {
  std::string var;
  std::shared_ptr<Expression> iter;
  std::vector<std::shared_ptr<Expression>> conditions;  // if条件
};

struct ListComprehension : Expression {
  std::shared_ptr<Expression> element;
  std::vector<ComprehensionClause> clauses;

  ListComprehension() { kind = ExpressionKind::kListComp; }
};

// 6. 字典推导式（Dict Comprehension）
struct DictComprehension : Expression {
  std::shared_ptr<Expression> key;
  std::shared_ptr<Expression> value;
  std::vector<ComprehensionClause> clauses;

  DictComprehension() { kind = ExpressionKind::kDictComp; }
};

// 7. 集合推导式（Set Comprehension）
struct SetComprehension : Expression {
  std::shared_ptr<Expression> element;
  std::vector<ComprehensionClause> clauses;

  SetComprehension() { kind = ExpressionKind::kSetComp; }
};

// 8. Lambda表达式增强
struct LambdaExpression : Expression {
  std::vector<std::string> params;
  std::vector<std::shared_ptr<TypeNode>> param_types;  // 类型注解
  std::shared_ptr<Expression> body;

  LambdaExpression() { kind = ExpressionKind::kLambda; }
};

// 9. 匹配语句（Python 3.10+）
struct MatchCase {
  std::shared_ptr<Expression> pattern;
  std::shared_ptr<Expression> guard;  // 可选的if条件
  std::vector<std::shared_ptr<Statement>> body;
};

struct MatchStatement : Statement {
  std::shared_ptr<Expression> subject;
  std::vector<MatchCase> cases;

  MatchStatement() { kind = StatementKind::kMatch; }
};

// 10. 类型别名（Type Alias）
struct TypeAliasDecl : Statement {
  std::string name;
  std::shared_ptr<TypeNode> type;

  TypeAliasDecl() { kind = StatementKind::kTypeAlias; }
};

// 11. 数据类（Dataclass）支持
struct DataclassDecl : ClassDecl {
  bool frozen{false};       // 不可变
  bool order{false};        // 自动生成比较方法
  bool slots{false};        // 使用__slots__
  bool init{true};          // 生成__init__
  bool repr{true};          // 生成__repr__
  bool eq{true};            // 生成__eq__
};

// 12. 属性（Property）
struct PropertyDecl {
  std::shared_ptr<FunctionDecl> getter;
  std::shared_ptr<FunctionDecl> setter;
  std::shared_ptr<FunctionDecl> deleter;
  std::string doc;
};

// 13. 静态方法和类方法
struct MethodKind {
  enum Kind { kInstance, kStatic, kClass };
  Kind kind{kInstance};
};

// 14. 多重继承和MRO（Method Resolution Order）
struct InheritanceInfo {
  std::vector<std::string> base_classes;
  std::vector<std::string> mro;  // 方法解析顺序
};

// 15. 元类（Metaclass）支持
struct MetaclassInfo {
  std::string metaclass_name;
  std::vector<std::shared_ptr<Expression>> kwargs;  // 元类参数
};

// 16. 描述符（Descriptor）协议
struct DescriptorInfo {
  bool has_get{false};
  bool has_set{false};
  bool has_delete{false};
};

// 17. 切片对象（Slice）
struct SliceExpression : Expression {
  std::shared_ptr<Expression> start;
  std::shared_ptr<Expression> stop;
  std::shared_ptr<Expression> step;

  SliceExpression() { kind = ExpressionKind::kSlice; }
};

// 18. 解包操作（Unpacking）
struct UnpackExpression : Expression {
  std::shared_ptr<Expression> value;
  bool is_dict{false};  // **dict vs *iterable

  UnpackExpression() { kind = ExpressionKind::kUnpack; }
};

// 19. 全局和非局部声明
struct GlobalStatement : Statement {
  std::vector<std::string> names;

  GlobalStatement() { kind = StatementKind::kGlobal; }
};

struct NonlocalStatement : Statement {
  std::vector<std::string> names;

  NonlocalStatement() { kind = StatementKind::kNonlocal; }
};

// 20. 断言语句增强
struct AssertStatement : Statement {
  std::shared_ptr<Expression> condition;
  std::shared_ptr<Expression> message;  // 可选的错误消息

  AssertStatement() { kind = StatementKind::kAssert; }
};

// 21. 导入系统
struct ImportStatement : Statement {
  std::vector<std::string> modules;
  std::vector<std::string> aliases;
  std::vector<std::string> from_items;  // for "from X import Y"
  std::string from_module;

  ImportStatement() { kind = StatementKind::kImport; }
};

// 22. 注解（Annotations）
struct AnnotatedAssignment : Statement {
  std::string target;
  std::shared_ptr<TypeNode> annotation;
  std::shared_ptr<Expression> value;  // 可选

  AnnotatedAssignment() { kind = StatementKind::kAnnotatedAssign; }
};

// 23. f-string（格式化字符串字面量）
struct FormattedString : Expression {
  struct Part {
    bool is_literal{true};
    std::string literal;
    std::shared_ptr<Expression> expr;
    std::string format_spec;  // :后面的格式说明
  };
  std::vector<Part> parts;

  FormattedString() { kind = ExpressionKind::kFString; }
};

// 24. 海象运算符（Walrus Operator - :=）
struct WalrusExpression : Expression {
  std::string target;
  std::shared_ptr<Expression> value;

  WalrusExpression() { kind = ExpressionKind::kWalrus; }
};

// 25. 类型提示（Type Hints）增强
struct TypeHint {
  enum HintKind {
    kSimple,      // int, str
    kGeneric,     // List[int], Dict[str, int]
    kUnion,       // Union[int, str] or int | str
    kOptional,    // Optional[int]
    kCallable,    // Callable[[int, str], bool]
    kLiteral,     // Literal[1, 2, 3]
    kTypeVar,     // TypeVar('T')
    kProtocol,    // Protocol类型
    kAnnotated    // Annotated[int, "metadata"]
  };

  HintKind hint_kind;
  std::string name;
  std::vector<std::shared_ptr<TypeHint>> args;
};

}  // namespace polyglot::python
