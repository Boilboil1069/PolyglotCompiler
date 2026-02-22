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
    if (expr->op == "&&") {
        if (lhs.GetType() == ConstexprValue::Type::kBool && !lhs.AsBool()) {
            return ConstexprValue(false);
        }
        ConstexprValue rhs = Evaluate(expr->right.get());
        return ApplyBinaryOp(expr->op, lhs, rhs);
    }
    
    if (expr->op == "||") {
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
    if (!expr || !expr->condition) {
        return ConstexprValue();
    }

    ConstexprValue cond = Evaluate(expr->condition.get());
    if (cond.GetType() != ConstexprValue::Type::kBool) {
        return ConstexprValue();
    }

    Expression* branch = cond.AsBool() ? expr->then_expr.get() : expr->else_expr.get();
    if (!branch) {
        return ConstexprValue();
    }

    return Evaluate(branch);
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
    
    // VarDecl-based functions don't have parameter lists
    // This is a legacy interface - prefer EvaluateFunctionCall for FunctionDecl
    // For now, just evaluate the initializer if present
    ConstexprValue return_value;
    if (func->init) {
        return_value = Evaluate(func->init.get());
    }
    
    // Pop scope
    recursion_depth_--;
    PopScope();
    
    return return_value;
}

ConstexprValue ConstexprEvaluator::EvaluateFunctionCall(FunctionDecl* func,
                                                         const std::vector<ConstexprValue>& args) {
    if (!func || !func->is_constexpr) {
        return ConstexprValue();  // Not a constexpr function
    }
    
    // Check recursion depth
    if (recursion_depth_ >= kMaxRecursionDepth) {
        return ConstexprValue();  // Recursion depth exceeded
    }
    
    // Push new scope for function parameters
    PushScope();
    recursion_depth_++;
    
    // Bind parameters to arguments
    // Each parameter gets bound to the corresponding argument value
    const auto& params = func->params;
    for (size_t i = 0; i < params.size(); ++i) {
        if (i < args.size()) {
            // Argument provided - bind parameter to argument value
            SetVariable(params[i].name, args[i]);
        } else if (params[i].default_value) {
            // No argument but default value exists - evaluate and bind
            ConstexprValue default_val = Evaluate(params[i].default_value.get());
            SetVariable(params[i].name, default_val);
        } else {
            // No argument and no default value - error
            recursion_depth_--;
            PopScope();
            return ConstexprValue();
        }
    }
    
    // Evaluate function body statements
    ConstexprValue return_value;
    for (const auto& stmt : func->body) {
        if (EvaluateStmt(stmt.get(), return_value)) {
            // Return statement was executed
            break;
        }
    }
    
    // Pop scope
    recursion_depth_--;
    PopScope();
    
    return return_value;
}

void ConstexprEvaluator::RegisterFunction(const std::string& name, FunctionDecl* func) {
    if (func) {
        function_table_[name] = func;
    }
}

FunctionDecl* ConstexprEvaluator::LookupFunction(const std::string& name) const {
    auto it = function_table_.find(name);
    if (it != function_table_.end()) {
        return it->second;
    }
    return nullptr;
}

bool ConstexprEvaluator::IsConstexprFunction(FunctionDecl* func) const {
    return func && func->is_constexpr;
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
    
    if (auto* while_stmt = dynamic_cast<WhileStatement*>(stmt)) {
        return EvaluateWhileStmt(while_stmt, return_value);
    }
    
    if (auto* for_stmt = dynamic_cast<ForStatement*>(stmt)) {
        return EvaluateForStmt(for_stmt, return_value);
    }
    
    if (auto* compound = dynamic_cast<CompoundStatement*>(stmt)) {
        return EvaluateCompoundStmt(compound, return_value);
    }
    
    return false;
}

bool ConstexprEvaluator::EvaluateWhileStmt(WhileStatement* stmt, ConstexprValue& return_value) {
    if (!stmt) return false;
    
    int iterations = 0;
    while (iterations < kMaxIterations) {
        // Evaluate condition
        ConstexprValue cond = Evaluate(stmt->condition.get());
        if (cond.GetType() != ConstexprValue::Type::kBool) {
            return false;  // Invalid condition
        }
        
        // Exit loop if condition is false
        if (!cond.AsBool()) {
            break;
        }
        
        // Execute loop body
        for (const auto& s : stmt->body) {
            if (EvaluateStmt(s.get(), return_value)) {
                return true;  // Return statement executed
            }
        }
        
        ++iterations;
    }
    
    return false;
}

bool ConstexprEvaluator::EvaluateForStmt(ForStatement* stmt, ConstexprValue& return_value) {
    if (!stmt) return false;
    
    // Create a new scope for the loop
    PushScope();
    
    // Evaluate initializer
    if (stmt->init) {
        ConstexprValue unused;
        EvaluateStmt(stmt->init.get(), unused);
    }
    
    int iterations = 0;
    while (iterations < kMaxIterations) {
        // Evaluate condition (if present)
        if (stmt->condition) {
            ConstexprValue cond = Evaluate(stmt->condition.get());
            if (cond.GetType() != ConstexprValue::Type::kBool) {
                PopScope();
                return false;  // Invalid condition
            }
            
            // Exit loop if condition is false
            if (!cond.AsBool()) {
                break;
            }
        }
        
        // Execute loop body
        for (const auto& s : stmt->body) {
            if (EvaluateStmt(s.get(), return_value)) {
                PopScope();
                return true;  // Return statement executed
            }
        }
        
        // Evaluate increment expression
        if (stmt->increment) {
            Evaluate(stmt->increment.get());
        }
        
        ++iterations;
    }
    
    PopScope();
    return false;
}

bool ConstexprEvaluator::EvaluateCompoundStmt(CompoundStatement* stmt, ConstexprValue& return_value) {
    if (!stmt) return false;
    
    PushScope();
    
    for (const auto& s : stmt->statements) {
        if (EvaluateStmt(s.get(), return_value)) {
            PopScope();
            return true;  // Return statement executed
        }
    }
    
    PopScope();
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
    if (!expr) return ConstexprValue();
    
    // Evaluate arguments first
    std::vector<ConstexprValue> arg_values;
    for (const auto& arg : expr->args) {
        ConstexprValue val = Evaluate(arg.get());
        if (val.IsUndefined()) {
            return ConstexprValue();  // Failed to evaluate argument
        }
        arg_values.push_back(val);
    }
    
    // Check if callee is a constexpr function
    if (auto* id = dynamic_cast<Identifier*>(expr->callee.get())) {
        // Look up function in function table
        FunctionDecl* func = LookupFunction(id->name);
        if (func && func->is_constexpr) {
            return EvaluateFunctionCall(func, arg_values);
        }
        
        // Built-in functions that can be evaluated at compile time
        if (id->name == "sizeof") {
            // Simplified: return 8 for all types (would need type info)
            return ConstexprValue(8LL);
        }
        
        // Function not found or not constexpr
        return ConstexprValue();
    }
    
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

ConstexprChecker::ConstexprChecker() {
    // Register standard library constexpr functions
    constexpr_functions_.insert("std::min");
    constexpr_functions_.insert("std::max");
    constexpr_functions_.insert("std::abs");
    constexpr_functions_.insert("std::size");
    constexpr_functions_.insert("sizeof");
    constexpr_functions_.insert("alignof");
}

bool ConstexprChecker::IsConstantExpression(Expression* expr) const {
    return IsConstexprExpr(expr);
}

bool ConstexprChecker::IsConstexprExpr(Expression* expr) const {
    if (!expr) return false;
    
    // Literals are always constant expressions
    if (dynamic_cast<Literal*>(expr)) {
        return true;
    }
    
    // Identifiers: need to check if they refer to constexpr variables
    // For now, assume they're constexpr if referenced
    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        // Special built-in expressions
        if (id->name == "true" || id->name == "false" || 
            id->name == "nullptr" || id->name == "NULL") {
            return true;
        }
        // Variable references may or may not be constexpr
        // A full implementation would look up the variable in a symbol table
        return false;
    }
    
    // Binary expressions: both operands must be constexpr
    if (auto* bin = dynamic_cast<BinaryExpression*>(expr)) {
        return CheckBinaryExpr(bin);
    }
    
    // Unary expressions: operand must be constexpr
    if (auto* unary = dynamic_cast<UnaryExpression*>(expr)) {
        return CheckUnaryExpr(unary);
    }
    
    // Conditional expressions: all parts must be constexpr
    if (auto* cond = dynamic_cast<ConditionalExpression*>(expr)) {
        return CheckConditionalExpr(cond);
    }
    
    // Call expressions: function must be constexpr and all args must be constexpr
    if (auto* call = dynamic_cast<CallExpression*>(expr)) {
        return CheckCallExpr(call);
    }
    
    // Static cast: operand must be constexpr
    if (auto* cast = dynamic_cast<StaticCastExpression*>(expr)) {
        return IsConstexprExpr(cast->operand.get());
    }
    
    // Initializer list: all elements must be constexpr
    if (auto* init_list = dynamic_cast<InitializerListExpression*>(expr)) {
        for (const auto& elem : init_list->elements) {
            if (!IsConstexprExpr(elem.get())) {
                return false;
            }
        }
        return true;
    }
    
    // Array subscript: both array and index must be constexpr
    if (auto* idx = dynamic_cast<IndexExpression*>(expr)) {
        return IsConstexprExpr(idx->object.get()) && IsConstexprExpr(idx->index.get());
    }
    
    // Member access: object must be constexpr
    if (auto* member = dynamic_cast<MemberExpression*>(expr)) {
        return IsConstexprExpr(member->object.get());
    }
    
    // Sizeof/alignof expressions are always constant
    if (dynamic_cast<SizeofExpression*>(expr)) {
        return true;
    }
    
    // Other expressions are not constexpr by default
    return false;
}

bool ConstexprChecker::CheckBinaryExpr(BinaryExpression* expr) const {
    if (!expr) return false;
    
    // Assignment operators are not allowed in constexpr context (C++11/14)
    // but some are allowed in C++14+ constexpr functions
    const std::string& op = expr->op;
    if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" ||
        op == "%=" || op == "&=" || op == "|=" || op == "^=" || 
        op == "<<=" || op == ">>=") {
        // Assignment is allowed in C++14+ constexpr functions
        // but the target must be a local variable
    }
    
    // Both operands must be constexpr
    return IsConstexprExpr(expr->left.get()) && IsConstexprExpr(expr->right.get());
}

bool ConstexprChecker::CheckUnaryExpr(UnaryExpression* expr) const {
    if (!expr) return false;
    
    const std::string& op = expr->op;
    
    // Increment/decrement are allowed in C++14+ constexpr
    // Address-of (&) and dereference (*) have restrictions
    if (op == "&") {
        // Address-of is allowed for static storage duration objects
        return true;  // Simplified
    }
    
    if (op == "*") {
        // Dereference: operand must be constexpr pointer
        return IsConstexprExpr(expr->operand.get());
    }
    
    return IsConstexprExpr(expr->operand.get());
}

bool ConstexprChecker::CheckConditionalExpr(ConditionalExpression* expr) const {
    if (!expr) return false;
    
    // All three parts must be constexpr
    return IsConstexprExpr(expr->condition.get()) &&
           IsConstexprExpr(expr->then_expr.get()) &&
           IsConstexprExpr(expr->else_expr.get());
}

bool ConstexprChecker::CheckCallExpr(CallExpression* expr) const {
    if (!expr) return false;
    
    // Check if the callee is a known constexpr function
    if (auto* id = dynamic_cast<Identifier*>(expr->callee.get())) {
        // Check if function is in our known constexpr functions list
        if (!IsKnownConstexprFunction(id->name)) {
            // Unknown function - might not be constexpr
            return false;
        }
    } else {
        // Complex callee expression - not typically constexpr
        return false;
    }
    
    // All arguments must be constexpr
    for (const auto& arg : expr->args) {
        if (!IsConstexprExpr(arg.get())) {
            return false;
        }
    }
    
    return true;
}

bool ConstexprChecker::CanBeConstexpr(VarDecl* func) const {
    return func && func->is_constexpr;
}

bool ConstexprChecker::CanBeConstexpr(FunctionDecl* func) const {
    if (!func || !func->is_constexpr) {
        return false;
    }
    
    // Check that all statements in the body are valid for constexpr
    for (const auto& stmt : func->body) {
        if (!IsConstexprStatement(stmt.get())) {
            return false;
        }
    }
    
    return true;
}

bool ConstexprChecker::IsConstexprStatement(Statement* stmt) const {
    if (!stmt) return true;  // Empty statement is OK
    
    // Variable declarations are allowed if constexpr or with constexpr initializer
    if (auto* decl = dynamic_cast<VarDecl*>(stmt)) {
        // Static and thread_local not allowed in constexpr functions
        if (decl->is_static) {
            return false;
        }
        // Initializer must be constexpr if present
        if (decl->init && !IsConstexprExpr(decl->init.get())) {
            return false;
        }
        return true;
    }
    
    // Expression statements
    if (auto* expr_stmt = dynamic_cast<ExprStatement*>(stmt)) {
        return IsConstexprExpr(expr_stmt->expr.get());
    }
    
    // Return statements
    if (auto* ret = dynamic_cast<ReturnStatement*>(stmt)) {
        if (ret->value) {
            return IsConstexprExpr(ret->value.get());
        }
        return true;  // void return is OK
    }
    
    // If statements (constexpr if in C++17+, regular if in C++14+)
    if (auto* if_stmt = dynamic_cast<IfStatement*>(stmt)) {
        if (!IsConstexprExpr(if_stmt->condition.get())) {
            return false;
        }
        for (const auto& s : if_stmt->then_body) {
            if (!IsConstexprStatement(s.get())) {
                return false;
            }
        }
        for (const auto& s : if_stmt->else_body) {
            if (!IsConstexprStatement(s.get())) {
                return false;
            }
        }
        return true;
    }
    
    // While loops (C++14+)
    if (auto* while_stmt = dynamic_cast<WhileStatement*>(stmt)) {
        if (!IsConstexprExpr(while_stmt->condition.get())) {
            return false;
        }
        for (const auto& s : while_stmt->body) {
            if (!IsConstexprStatement(s.get())) {
                return false;
            }
        }
        return true;
    }
    
    // For loops (C++14+)
    if (auto* for_stmt = dynamic_cast<ForStatement*>(stmt)) {
        if (for_stmt->init && !IsConstexprStatement(for_stmt->init.get())) {
            return false;
        }
        if (for_stmt->condition && !IsConstexprExpr(for_stmt->condition.get())) {
            return false;
        }
        if (for_stmt->increment && !IsConstexprExpr(for_stmt->increment.get())) {
            return false;
        }
        for (const auto& s : for_stmt->body) {
            if (!IsConstexprStatement(s.get())) {
                return false;
            }
        }
        return true;
    }
    
    // Compound statements
    if (auto* compound = dynamic_cast<CompoundStatement*>(stmt)) {
        for (const auto& s : compound->statements) {
            if (!IsConstexprStatement(s.get())) {
                return false;
            }
        }
        return true;
    }
    
    // Switch statements (C++14+)
    if (auto* switch_stmt = dynamic_cast<SwitchStatement*>(stmt)) {
        if (!IsConstexprExpr(switch_stmt->condition.get())) {
            return false;
        }
        for (const auto& case_ : switch_stmt->cases) {
            for (const auto& label : case_.labels) {
                if (!IsConstexprExpr(label.get())) {
                    return false;
                }
            }
            for (const auto& s : case_.body) {
                if (!IsConstexprStatement(s.get())) {
                    return false;
                }
            }
        }
        return true;
    }
    
    // Try-catch blocks are NOT allowed in constexpr (until C++20 with exceptions disabled)
    if (dynamic_cast<TryStatement*>(stmt)) {
        return false;
    }
    
    // Throw statements are NOT allowed in constexpr
    if (dynamic_cast<ThrowStatement*>(stmt)) {
        return false;
    }
    
    // Goto/label statements are NOT allowed
    // (Not defined in the AST, so skipped)
    
    // Inline ASM is NOT allowed
    // (Not defined in the AST, so skipped)
    
    // Function declarations nested inside are allowed
    if (dynamic_cast<FunctionDecl*>(stmt)) {
        return true;  // Nested functions are OK
    }
    
    // Unknown statement type - be conservative
    return false;
}

void ConstexprChecker::RegisterConstexprFunction(const std::string& name) {
    constexpr_functions_.insert(name);
}

bool ConstexprChecker::IsKnownConstexprFunction(const std::string& name) const {
    return constexpr_functions_.find(name) != constexpr_functions_.end();
}
}
