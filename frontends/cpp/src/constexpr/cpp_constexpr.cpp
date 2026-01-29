#include "frontends/cpp/include/cpp_constexpr.h"
#include <stdexcept>
#include <sstream>

namespace polyglot::cpp {

// ============================================================================
// ConstexprValue Implementation
// ============================================================================

std::string ConstexprValue::ToString() const {
    std::ostringstream oss;
    switch (type_) {
        case Type::kInt:
            oss << int_value_;
            break;
        case Type::kFloat:
            oss << float_value_;
            break;
        case Type::kBool:
            oss << (bool_value_ ? "true" : "false");
            break;
        case Type::kNull:
            oss << "nullptr";
            break;
        case Type::kPointer:
            oss << "<pointer>";
            break;
        case Type::kUndefined:
            oss << "<undefined>";
            break;
    }
    return oss.str();
}

// ============================================================================
// ConstexprEvaluator Implementation
// ============================================================================

ConstexprEvaluator::ConstexprEvaluator() {
    scopes_.push_back(Scope{});  // Global scope
}

ConstexprValue ConstexprEvaluator::Evaluate(Expression* expr) {
    if (!expr) return ConstexprValue();
    
    // Check specific expression types
    if (auto* lit = dynamic_cast<Literal*>(expr)) {
        return EvaluateLiteral(lit);
    }
    
    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        return EvaluateVariable(id);
    }
    
    if (auto* bin = dynamic_cast<BinaryExpression*>(expr)) {
        return EvaluateBinaryOp(bin);
    }
    
    if (auto* unary = dynamic_cast<UnaryExpression*>(expr)) {
        return EvaluateUnaryOp(unary);
    }
    
    if (auto* cond = dynamic_cast<ConditionalExpression*>(expr)) {
        return EvaluateConditional(cond);
    }
    
    if (auto* member = dynamic_cast<MemberExpression*>(expr)) {
        return EvaluateMemberAccess(member);
    }
    
    if (auto* call = dynamic_cast<CallExpression*>(expr)) {
        return EvaluateCall(call);
    }
    
    if (auto* init_list = dynamic_cast<InitializerListExpression*>(expr)) {
        return EvaluateInitializerList(init_list);
    }
    
    if (auto* cast = dynamic_cast<StaticCastExpression*>(expr)) {
        return EvaluateCast(cast);
    }
    
    // Unsupported expression type
    return ConstexprValue();
}

ConstexprValue ConstexprEvaluator::EvaluateBinaryOp(BinaryExpression* expr) {
    if (!expr) return ConstexprValue();
    
    ConstexprValue lhs = Evaluate(expr->left.get());
    
    // Short-circuit evaluation for && and ||
    if (expr->op == \"&&\") {
        if (lhs.GetType() == ConstexprValue::Type::kBool && !lhs.AsBool()) {
            return ConstexprValue(false);
        }
        ConstexprValue rhs = Evaluate(expr->right.get());
        return ApplyBinaryOp(expr->op, lhs, rhs);
    }
    
    if (expr->op == \"||\") {
        if (lhs.GetType() == ConstexprValue::Type::kBool && lhs.AsBool()) {
            return ConstexprValue(true);
        }
        ConstexprValue rhs = Evaluate(expr->right.get());
        return ApplyBinaryOp(expr->op, lhs, rhs);
    }
    
    ConstexprValue rhs = Evaluate(expr->right.get());
    return ApplyBinaryOp(expr->op, lhs, rhs);
}

ConstexprValue ConstexprEvaluator::ApplyBinaryOp(const std::string& op,
                                                  const ConstexprValue& lhs,
                                                  const ConstexprValue& rhs) {
    // Integer operations
    if (lhs.GetType() == ConstexprValue::Type::kInt &&
        rhs.GetType() == ConstexprValue::Type::kInt) {
        long long l = lhs.AsInt();
        long long r = rhs.AsInt();
        
        if (op == "+") return ConstexprValue(l + r);
        if (op == "-") return ConstexprValue(l - r);
        if (op == "*") return ConstexprValue(l * r);
        if (op == "/") {
            if (r == 0) return ConstexprValue();
            return ConstexprValue(l / r);
        }
        if (op == "%") {
            if (r == 0) return ConstexprValue();
            return ConstexprValue(l % r);
        }
        if (op == "==") return ConstexprValue(l == r);
        if (op == "!=") return ConstexprValue(l != r);
        if (op == "<") return ConstexprValue(l < r);
        if (op == "<=") return ConstexprValue(l <= r);
        if (op == ">") return ConstexprValue(l > r);
        if (op == ">=") return ConstexprValue(l >= r);
        if (op == "&") return ConstexprValue(l & r);
        if (op == "|") return ConstexprValue(l | r);
        if (op == "^") return ConstexprValue(l ^ r);
        if (op == "<<") return ConstexprValue(l << r);
        if (op == ">>") return ConstexprValue(l >> r);
        if (op == "&&") return ConstexprValue(l && r);
        if (op == "||") return ConstexprValue(l || r);
    }
    
    // Float operations
    if (lhs.GetType() == ConstexprValue::Type::kFloat &&
        rhs.GetType() == ConstexprValue::Type::kFloat) {
        double l = lhs.AsFloat();
        double r = rhs.AsFloat();
        
        if (op == "+") return ConstexprValue(l + r);
        if (op == "-") return ConstexprValue(l - r);
        if (op == "*") return ConstexprValue(l * r);
        if (op == "/") return ConstexprValue(l / r);
        if (op == "==") return ConstexprValue(l == r);
        if (op == "!=") return ConstexprValue(l != r);
        if (op == "<") return ConstexprValue(l < r);
        if (op == "<=") return ConstexprValue(l <= r);
        if (op == ">") return ConstexprValue(l > r);
        if (op == ">=") return ConstexprValue(l >= r);
    }
    
    return ConstexprValue();
}

ConstexprValue ConstexprEvaluator::EvaluateUnaryOp(UnaryExpression* expr) {
    if (!expr) return ConstexprValue();
    
    ConstexprValue val = Evaluate(expr->operand.get());
    return ApplyUnaryOp(expr->op, val);
}

ConstexprValue ConstexprEvaluator::ApplyUnaryOp(const std::string& op,
                                                 const ConstexprValue& val) {
    if (val.GetType() == ConstexprValue::Type::kInt) {
        long long v = val.AsInt();
        if (op == "-") return ConstexprValue(-v);
        if (op == "~") return ConstexprValue(~v);
        if (op == "!") return ConstexprValue(!v);
    }
    
    if (val.GetType() == ConstexprValue::Type::kFloat) {
        double v = val.AsFloat();
        if (op == "-") return ConstexprValue(-v);
    }
    
    if (val.GetType() == ConstexprValue::Type::kBool) {
        bool v = val.AsBool();
        if (op == "!") return ConstexprValue(!v);
    }
    
    return ConstexprValue();
}

ConstexprValue ConstexprEvaluator::EvaluateConditional(ConditionalExpression* expr) {
    if (!expr) return ConstexprValue();
    
    // Evaluate condition (need to check actual ConditionalExpression structure)
    // This is a placeholder - real implementation needs actual fields
    // ConstexprValue cond = Evaluate(expr->condition.get());
    // if (cond.GetType() == ConstexprValue::Type::kBool) {
    //     return cond.AsBool() ? Evaluate(expr->true_expr.get()) : Evaluate(expr->false_expr.get());
    // }
    
    return ConstexprValue();
}

ConstexprValue ConstexprEvaluator::EvaluateLiteral(Literal* expr) {
    if (!expr) return ConstexprValue();
    
    const std::string& val = expr->value;
    
    // Try to parse as integer
    try {
        size_t idx = 0;
        long long ival = std::stoll(val, &idx, 0);
        if (idx == val.size() || (idx < val.size() && (val[idx] == 'L' || val[idx] == 'l' || 
                                                       val[idx] == 'U' || val[idx] == 'u'))) {
            return ConstexprValue(ival);
        }
    } catch (...) {}
    
    // Try to parse as float
    try {
        size_t idx = 0;
        double fval = std::stod(val, &idx);
        if (idx == val.size() || (idx < val.size() && (val[idx] == 'f' || val[idx] == 'F' ||
                                                       val[idx] == 'l' || val[idx] == 'L'))) {
            return ConstexprValue(fval);
        }
    } catch (...) {}
    
    // Try to parse as boolean
    if (val == "true") return ConstexprValue(true);
    if (val == "false") return ConstexprValue(false);
    
    // Check for nullptr
    if (val == "nullptr" || val == "NULL") {
        return ConstexprValue::Null();
    }
    
    // String literal (simplified - just store as undefined)
    return ConstexprValue();
}

ConstexprValue ConstexprEvaluator::EvaluateVariable(Identifier* expr) {
    if (!expr) return ConstexprValue();
    
    // Look up variable in scopes (from innermost to outermost)
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto var_it = it->variables.find(expr->name);
        if (var_it != it->variables.end()) {
            return var_it->second;
        }
    }
    
    // Variable not found
    return ConstexprValue();
}

ConstexprValue ConstexprEvaluator::EvaluateCallExpr(CallExpression* expr) {
    if (!expr) return ConstexprValue();
    
    // Evaluate arguments
    std::vector<ConstexprValue> arg_values;
    for (const auto& arg : expr->args) {
        arg_values.push_back(Evaluate(arg.get()));
        if (arg_values.back().IsUndefined()) {
            return ConstexprValue();  // Failed to evaluate argument
        }
    }
    
    // Check if callee is a constexpr function
    // Simplified - would need symbol table lookup
    if (auto* id = dynamic_cast<Identifier*>(expr->callee.get())) {
        // Look up function in function cache or symbol table
        // For now, just return undefined
        return ConstexprValue();
    }
    
    return ConstexprValue();
}

ConstexprValue ConstexprEvaluator::EvaluateCall(VarDecl* func,
                                                 const std::vector<ConstexprValue>& args) {
    if (!func || !func->is_constexpr) {
        return ConstexprValue();  // Not a constexpr function
    }
    
    // Check recursion depth
    if (recursion_depth_ >= kMaxRecursionDepth) {
        return ConstexprValue();  // Too deep
    }
    
    // Push new scope for function parameters
    PushScope();
    recursion_depth_++;
    
    // TODO: Bind parameters to arguments
    // This would require access to function parameter list
    
    // Evaluate function body
    ConstexprValue return_value;
    // TODO: Evaluate statements in function body
    
    // Pop scope
    recursion_depth_--;
    PopScope();
    
    return return_value;
}

bool ConstexprEvaluator::EvaluateStmt(Statement* stmt, ConstexprValue& return_value) {
    if (!stmt) return false;
    
    if (auto* ret = dynamic_cast<ReturnStatement*>(stmt)) {
        return EvaluateReturnStmt(ret, return_value);
    }
    
    if (auto* if_stmt = dynamic_cast<IfStatement*>(stmt)) {
        return EvaluateIfStmt(if_stmt, return_value);
    }
    
    if (auto* decl = dynamic_cast<VarDecl*>(stmt)) {
        return EvaluateDeclStmt(decl);
    }
    
    if (auto* expr_stmt = dynamic_cast<ExprStatement*>(stmt)) {
        Evaluate(expr_stmt->expr.get());
        return false;  // No return
    }
    
    return false;
}

bool ConstexprEvaluator::EvaluateIfStmt(IfStatement* stmt, ConstexprValue& return_value) {
    if (!stmt) return false;
    
    // Evaluate condition
    ConstexprValue cond = Evaluate(stmt->condition.get());
    if (cond.GetType() != ConstexprValue::Type::kBool) {
        return false;  // Invalid condition
    }
    
    // Execute appropriate branch
    const auto& body = cond.AsBool() ? stmt->then_body : stmt->else_body;
    for (const auto& s : body) {
        if (EvaluateStmt(s.get(), return_value)) {
            return true;  // Return statement executed
        }
    }
    
    return false;
}

bool ConstexprEvaluator::EvaluateReturnStmt(ReturnStatement* stmt,
                                             ConstexprValue& return_value) {
    if (!stmt) return false;
    
    if (stmt->value) {
        return_value = Evaluate(stmt->value.get());
    } else {
        return_value = ConstexprValue();  // void return
    }
    
    return true;  // Signal that return statement was executed
}

bool ConstexprEvaluator::EvaluateDeclStmt(VarDecl* stmt) {
    if (!stmt) return false;
    
    // Evaluate initializer
    ConstexprValue init_value;
    if (stmt->init) {
        init_value = Evaluate(stmt->init.get());
    }
    
    // Store in current scope
    SetVariable(stmt->name, init_value);
    
    return false;  // No return
}

ConstexprValue ConstexprEvaluator::EvaluateMemberAccess(MemberExpression* expr) {
    // Simplified - member access for constexpr objects is complex
    // Would need to track object values and field offsets
    return ConstexprValue();
}

ConstexprValue ConstexprEvaluator::EvaluateCall(CallExpression* expr) {
    // Call to constexpr function - would need function table
    return ConstexprValue();
}

ConstexprValue ConstexprEvaluator::EvaluateInitializerList(InitializerListExpression* expr) {
    // Aggregate initialization - would need to track composite values
    return ConstexprValue();
}

ConstexprValue ConstexprEvaluator::EvaluateCast(StaticCastExpression* expr) {
    if (!expr) return ConstexprValue();
    
    ConstexprValue val = Evaluate(expr->operand.get());
    
    // Simplified cast - would need type information to do proper casting
    return val;
}

ConstexprValue ConstexprEvaluator::EvaluateArraySubscript(IndexExpression* expr) {
    // Array subscript evaluation - would need array value tracking
    return ConstexprValue();
}

void ConstexprEvaluator::PushScope() {
    scopes_.push_back(Scope{});
}

void ConstexprEvaluator::PopScope() {
    if (!scopes_.empty()) {
        scopes_.pop_back();
    }
}

bool ConstexprEvaluator::IsConstexprFunction(VarDecl* func) const {
    return func && func->is_constexpr;
}

void ConstexprEvaluator::SetVariable(const std::string& name, const ConstexprValue& value) {
    if (!scopes_.empty()) {
        scopes_.back().variables[name] = value;
    }
}

ConstexprValue ConstexprEvaluator::GetVariable(const std::string& name) const {
    // Search scopes from innermost to outermost
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto var_it = it->variables.find(name);
        if (var_it != it->variables.end()) {
            return var_it->second;
        }
    }
    return ConstexprValue();  // Not found
}

void ConstexprEvaluator::PushScope() {
    scopes_.push_back(Scope{});
}

void ConstexprEvaluator::PopScope() {
    if (scopes_.size() > 1) {  // Keep global scope
        scopes_.pop_back();
    }
}

// ============================================================================
// ConstexprChecker Implementation
// ============================================================================

ConstexprChecker::ConstexprChecker() {}

bool ConstexprChecker::IsConstantExpression(Expression* expr) const {
    return IsConstexprExpr(expr);
}

bool ConstexprChecker::IsConstexprExpr(Expression* expr) const {
    // Simplified implementation
    return false;
}

bool ConstexprChecker::CheckCallExpr(CallExpression* expr) const {
    // TODO: Implement
    return false;
}

bool ConstexprChecker::CanBeConstexpr(VarDecl* func) const {
    return func && func->is_constexpr;
}

bool ConstexprChecker::IsConstexprStatement(Statement* stmt) const {
    // TODO: Implement
    return false;
}

}  // namespace polyglot::cpp

// ============================================================================
// ConstexprValue Implementation
// ============================================================================

std::string ConstexprValue::ToString() const {
    std::ostringstream oss;
    switch (type_) {
        case Type::kInt:
            oss << int_value_;
            break;
        case Type::kFloat:
            oss << float_value_;
            break;
        case Type::kBool:
            oss << (bool_value_ ? "true" : "false");
            break;
        case Type::kNull:
            oss << "nullptr";
            break;
        case Type::kPointer:
            oss << "<pointer>";
            break;
        case Type::kUndefined:
            oss << "<undefined>";
            break;
    }
    return oss.str();
}

// ============================================================================
// ConstexprEvaluator Implementation
// ============================================================================

ConstexprEvaluator::ConstexprEvaluator() {
    scopes_.push_back(Scope{});  // Global scope
}

ConstexprValue ConstexprEvaluator::Evaluate(Expr* expr) {
    if (!expr) return ConstexprValue();
    
    if (auto* bin = dynamic_cast<BinaryExpr*>(expr)) {
        return EvaluateBinaryOp(bin);
    } else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return EvaluateUnaryOp(unary);
    } else if (auto* cond = dynamic_cast<ConditionalExpr*>(expr)) {
        return EvaluateConditional(cond);
    } else if (auto* lit = dynamic_cast<LiteralExpr*>(expr)) {
        return EvaluateLiteral(lit);
    } else if (auto* ref = dynamic_cast<DeclRefExpr*>(expr)) {
        return EvaluateVariable(ref);
    } else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        return EvaluateCallExpr(call);
    } else if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        return EvaluateMemberAccess(member);
    } else if (auto* subscript = dynamic_cast<ArraySubscriptExpr*>(expr)) {
        return EvaluateArraySubscript(subscript);
    } else if (auto* cast = dynamic_cast<CastExpr*>(expr)) {
        return EvaluateCast(cast);
    }
    
    return ConstexprValue();  // Undefined
}

ConstexprValue ConstexprEvaluator::EvaluateBinaryOp(BinaryExpr* expr) {
    ConstexprValue lhs = Evaluate(expr->lhs.get());
    ConstexprValue rhs = Evaluate(expr->rhs.get());
    
    if (lhs.IsUndefined() || rhs.IsUndefined()) {
        return ConstexprValue();
    }
    
    return ApplyBinaryOp(expr->op, lhs, rhs);
}

ConstexprValue ConstexprEvaluator::ApplyBinaryOp(BinaryExpr::Op op,
                                                  const ConstexprValue& lhs,
                                                  const ConstexprValue& rhs) {
    // Integer operations
    if (lhs.GetType() == ConstexprValue::Type::kInt &&
        rhs.GetType() == ConstexprValue::Type::kInt) {
        long long l = lhs.AsInt();
        long long r = rhs.AsInt();
        
        switch (op) {
            case BinaryExpr::Op::kAdd: return ConstexprValue(l + r);
            case BinaryExpr::Op::kSub: return ConstexprValue(l - r);
            case BinaryExpr::Op::kMul: return ConstexprValue(l * r);
            case BinaryExpr::Op::kDiv:
                if (r == 0) return ConstexprValue();  // Division by zero
                return ConstexprValue(l / r);
            case BinaryExpr::Op::kMod:
                if (r == 0) return ConstexprValue();
                return ConstexprValue(l % r);
            case BinaryExpr::Op::kEq: return ConstexprValue(l == r);
            case BinaryExpr::Op::kNe: return ConstexprValue(l != r);
            case BinaryExpr::Op::kLt: return ConstexprValue(l < r);
            case BinaryExpr::Op::kLe: return ConstexprValue(l <= r);
            case BinaryExpr::Op::kGt: return ConstexprValue(l > r);
            case BinaryExpr::Op::kGe: return ConstexprValue(l >= r);
            case BinaryExpr::Op::kAnd: return ConstexprValue(l & r);
            case BinaryExpr::Op::kOr: return ConstexprValue(l | r);
            case BinaryExpr::Op::kXor: return ConstexprValue(l ^ r);
            case BinaryExpr::Op::kShl: return ConstexprValue(l << r);
            case BinaryExpr::Op::kShr: return ConstexprValue(l >> r);
            case BinaryExpr::Op::kLogicalAnd: return ConstexprValue(l && r);
            case BinaryExpr::Op::kLogicalOr: return ConstexprValue(l || r);
            default: return ConstexprValue();
        }
    }
    
    // Float operations
    if (lhs.GetType() == ConstexprValue::Type::kFloat &&
        rhs.GetType() == ConstexprValue::Type::kFloat) {
        double l = lhs.AsFloat();
        double r = rhs.AsFloat();
        
        switch (op) {
            case BinaryExpr::Op::kAdd: return ConstexprValue(l + r);
            case BinaryExpr::Op::kSub: return ConstexprValue(l - r);
            case BinaryExpr::Op::kMul: return ConstexprValue(l * r);
            case BinaryExpr::Op::kDiv: return ConstexprValue(l / r);
            case BinaryExpr::Op::kEq: return ConstexprValue(l == r);
            case BinaryExpr::Op::kNe: return ConstexprValue(l != r);
            case BinaryExpr::Op::kLt: return ConstexprValue(l < r);
            case BinaryExpr::Op::kLe: return ConstexprValue(l <= r);
            case BinaryExpr::Op::kGt: return ConstexprValue(l > r);
            case BinaryExpr::Op::kGe: return ConstexprValue(l >= r);
            default: return ConstexprValue();
        }
    }
    
    return ConstexprValue();
}

ConstexprValue ConstexprEvaluator::EvaluateUnaryOp(UnaryExpr* expr) {
    ConstexprValue val = Evaluate(expr->operand.get());
    if (val.IsUndefined()) return ConstexprValue();
    
    return ApplyUnaryOp(expr->op, val);
}

ConstexprValue ConstexprEvaluator::ApplyUnaryOp(UnaryExpr::Op op,
                                                 const ConstexprValue& val) {
    if (val.GetType() == ConstexprValue::Type::kInt) {
        long long v = val.AsInt();
        switch (op) {
            case UnaryExpr::Op::kNeg: return ConstexprValue(-v);
            case UnaryExpr::Op::kNot: return ConstexprValue(~v);
            case UnaryExpr::Op::kLogicalNot: return ConstexprValue(!v);
            default: return ConstexprValue();
        }
    }
    
    if (val.GetType() == ConstexprValue::Type::kFloat) {
        double v = val.AsFloat();
        switch (op) {
            case UnaryExpr::Op::kNeg: return ConstexprValue(-v);
            default: return ConstexprValue();
        }
    }
    
    if (val.GetType() == ConstexprValue::Type::kBool) {
        bool v = val.AsBool();
        switch (op) {
            case UnaryExpr::Op::kLogicalNot: return ConstexprValue(!v);
            default: return ConstexprValue();
        }
    }
    
    return ConstexprValue();
}

ConstexprValue ConstexprEvaluator::EvaluateConditional(ConditionalExpr* expr) {
    ConstexprValue cond = Evaluate(expr->condition.get());
    if (cond.IsUndefined()) return ConstexprValue();
    
    bool cond_val = false;
    if (cond.GetType() == ConstexprValue::Type::kBool) {
        cond_val = cond.AsBool();
    } else if (cond.GetType() == ConstexprValue::Type::kInt) {
        cond_val = cond.AsInt() != 0;
    } else {
        return ConstexprValue();
    }
    
    if (cond_val) {
        return Evaluate(expr->true_expr.get());
    } else {
        return Evaluate(expr->false_expr.get());
    }
}

ConstexprValue ConstexprEvaluator::EvaluateLiteral(LiteralExpr* expr) {
    switch (expr->kind) {
        case LiteralExpr::Kind::kInteger:
            return ConstexprValue(expr->int_value);
        case LiteralExpr::Kind::kFloat:
            return ConstexprValue(expr->float_value);
        case LiteralExpr::Kind::kBool:
            return ConstexprValue(expr->bool_value);
        case LiteralExpr::Kind::kNull:
            return ConstexprValue::Null();
        default:
            return ConstexprValue();
    }
}

ConstexprValue ConstexprEvaluator::EvaluateVariable(DeclRefExpr* expr) {
    return GetVariable(expr->name);
}

ConstexprValue ConstexprEvaluator::EvaluateCallExpr(CallExpr* expr) {
    // Get function declaration
    FunctionDecl* func = expr->callee_decl;
    if (!func || !IsConstexprFunction(func)) {
        return ConstexprValue();
    }
    
    // Evaluate arguments
    std::vector<ConstexprValue> args;
    for (auto& arg_expr : expr->arguments) {
        ConstexprValue arg = Evaluate(arg_expr.get());
        if (arg.IsUndefined()) return ConstexprValue();
        args.push_back(arg);
    }
    
    return EvaluateCall(func, args);
}

ConstexprValue ConstexprEvaluator::EvaluateCall(FunctionDecl* func,
                                                 const std::vector<ConstexprValue>& args) {
    // Check recursion depth
    if (recursion_depth_ >= kMaxRecursionDepth) {
        return ConstexprValue();  // Too deep
    }
    
    recursion_depth_++;
    PushScope();
    
    // Bind parameters to arguments
    for (size_t i = 0; i < func->parameters.size() && i < args.size(); ++i) {
        SetVariable(func->parameters[i]->name, args[i]);
    }
    
    // Evaluate function body
    ConstexprValue result;
    bool returned = EvaluateStmt(func->body.get(), result);
    
    PopScope();
    recursion_depth_--;
    
    return returned ? result : ConstexprValue();
}

bool ConstexprEvaluator::EvaluateStmt(Stmt* stmt, ConstexprValue& return_value) {
    if (!stmt) return false;
    
    if (auto* compound = dynamic_cast<CompoundStmt*>(stmt)) {
        return EvaluateCompoundStmt(compound, return_value);
    } else if (auto* if_stmt = dynamic_cast<IfStmt*>(stmt)) {
        return EvaluateIfStmt(if_stmt, return_value);
    } else if (auto* ret = dynamic_cast<ReturnStmt*>(stmt)) {
        return EvaluateReturnStmt(ret, return_value);
    } else if (auto* decl = dynamic_cast<DeclStmt*>(stmt)) {
        return EvaluateDeclStmt(decl);
    }
    
    return false;
}

bool ConstexprEvaluator::EvaluateCompoundStmt(CompoundStmt* stmt, 
                                               ConstexprValue& return_value) {
    for (auto& s : stmt->statements) {
        if (EvaluateStmt(s.get(), return_value)) {
            return true;  // Return statement found
        }
    }
    return false;
}

bool ConstexprEvaluator::EvaluateIfStmt(IfStmt* stmt, ConstexprValue& return_value) {
    ConstexprValue cond = Evaluate(stmt->condition.get());
    if (cond.IsUndefined()) return false;
    
    bool cond_val = false;
    if (cond.GetType() == ConstexprValue::Type::kBool) {
        cond_val = cond.AsBool();
    } else if (cond.GetType() == ConstexprValue::Type::kInt) {
        cond_val = cond.AsInt() != 0;
    }
    
    if (cond_val) {
        return EvaluateStmt(stmt->then_stmt.get(), return_value);
    } else if (stmt->else_stmt) {
        return EvaluateStmt(stmt->else_stmt.get(), return_value);
    }
    
    return false;
}

bool ConstexprEvaluator::EvaluateReturnStmt(ReturnStmt* stmt,
                                             ConstexprValue& return_value) {
    if (stmt->value) {
        return_value = Evaluate(stmt->value.get());
    }
    return true;
}

bool ConstexprEvaluator::EvaluateDeclStmt(DeclStmt* stmt) {
    // Handle variable declarations
    if (stmt->var_decl) {
        ConstexprValue init_val;
        if (stmt->var_decl->init) {
            init_val = Evaluate(stmt->var_decl->init.get());
        }
        SetVariable(stmt->var_decl->name, init_val);
    }
    return false;
}

ConstexprValue ConstexprEvaluator::EvaluateMemberAccess(MemberExpr* expr) {
    // TODO: Implement member access for constexpr structs
    return ConstexprValue();
}

ConstexprValue ConstexprEvaluator::EvaluateArraySubscript(ArraySubscriptExpr* expr) {
    // TODO: Implement array subscript for constexpr arrays
    return ConstexprValue();
}

ConstexprValue ConstexprEvaluator::EvaluateCast(CastExpr* expr) {
    ConstexprValue val = Evaluate(expr->operand.get());
    if (val.IsUndefined()) return ConstexprValue();
    
    // Handle type conversions
    // TODO: Implement proper type casting
    return val;
}

bool ConstexprEvaluator::IsConstexprFunction(FunctionDecl* func) const {
    return func && func->is_constexpr;
}

void ConstexprEvaluator::SetVariable(const std::string& name, const ConstexprValue& value) {
    if (!scopes_.empty()) {
        scopes_.back().variables[name] = value;
    }
}

ConstexprValue ConstexprEvaluator::GetVariable(const std::string& name) const {
    // Search scopes from innermost to outermost
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto var_it = it->variables.find(name);
        if (var_it != it->variables.end()) {
            return var_it->second;
        }
    }
    return ConstexprValue();  // Not found
}

void ConstexprEvaluator::PushScope() {
    scopes_.push_back(Scope{});
}

void ConstexprEvaluator::PopScope() {
    if (scopes_.size() > 1) {  // Keep global scope
        scopes_.pop_back();
    }
}

// ============================================================================
// ConstexprChecker Implementation
// ============================================================================

ConstexprChecker::ConstexprChecker() {}

bool ConstexprChecker::IsConstantExpression(Expr* expr) const {
    return IsConstexprExpr(expr);
}

bool ConstexprChecker::IsConstexprExpr(Expr* expr) const {
    if (!expr) return false;
    
    if (dynamic_cast<LiteralExpr*>(expr)) {
        return true;
    }
    
    if (auto* bin = dynamic_cast<BinaryExpr*>(expr)) {
        return IsConstexprExpr(bin->lhs.get()) && IsConstexprExpr(bin->rhs.get());
    }
    
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return IsConstexprExpr(unary->operand.get());
    }
    
    if (auto* cond = dynamic_cast<ConditionalExpr*>(expr)) {
        return IsConstexprExpr(cond->condition.get()) &&
               IsConstexprExpr(cond->true_expr.get()) &&
               IsConstexprExpr(cond->false_expr.get());
    }
    
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        return CheckCallExpr(call);
    }
    
    return false;
}

bool ConstexprChecker::CheckCallExpr(CallExpr* expr) const {
    FunctionDecl* func = expr->callee_decl;
    if (!func || !func->is_constexpr) {
        return false;
    }
    
    // All arguments must be constant expressions
    for (auto& arg : expr->arguments) {
        if (!IsConstexprExpr(arg.get())) {
            return false;
        }
    }
    
    return true;
}

bool ConstexprChecker::CanBeConstexpr(FunctionDecl* func) const {
    if (!func || !func->body) return false;
    
    // Check if all statements in the function are valid in constexpr context
    return IsConstexprStatement(func->body.get());
}

bool ConstexprChecker::IsConstexprStatement(Stmt* stmt) const {
    if (!stmt) return true;
    
    if (auto* compound = dynamic_cast<CompoundStmt*>(stmt)) {
        for (auto& s : compound->statements) {
            if (!IsConstexprStatement(s.get())) {
                return false;
            }
        }
        return true;
    }
    
    if (dynamic_cast<ReturnStmt*>(stmt)) return true;
    if (dynamic_cast<IfStmt*>(stmt)) return true;
    if (dynamic_cast<DeclStmt*>(stmt)) return true;
    
    // Loops, goto, labels are not allowed in constexpr (C++11/14)
    // C++20 allows more statements
    
    return false;
}

}  // namespace polyglot::cpp
