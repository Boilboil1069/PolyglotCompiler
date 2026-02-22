#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "common/include/ir/ir_builder.h"
#include "frontends/common/include/diagnostics.h"
#include "frontends/ploy/include/ploy_ast.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "middle/include/ir/ir_context.h"

namespace polyglot::ploy {

// ============================================================================
// Cross-Language Call Descriptor (emitted into IR metadata)
// ============================================================================

struct CrossLangCallDescriptor {
    std::string stub_name;          // Generated stub function name
    std::string source_language;
    std::string target_language;
    std::string source_function;
    std::string target_function;
    ir::IRType source_return_type{ir::IRType::Invalid()};
    ir::IRType target_return_type{ir::IRType::Invalid()};
    std::vector<ir::IRType> source_param_types;
    std::vector<ir::IRType> target_param_types;
    // Marshalling descriptors per parameter
    struct MarshalOp {
        enum class Kind { kDirect, kCast, kStringConvert, kArrayConvert, kStructConvert,
                          kListConvert, kTupleConvert, kDictConvert, kOptionConvert };
        Kind kind{Kind::kDirect};
        ir::IRType from{ir::IRType::Invalid()};
        ir::IRType to{ir::IRType::Invalid()};
    };
    std::vector<MarshalOp> param_marshal;
    MarshalOp return_marshal;
};

// ============================================================================
// Lowering Engine
// ============================================================================

class PloyLowering {
  public:
    PloyLowering(ir::IRContext &ir_context, frontends::Diagnostics &diagnostics,
                 const PloySema &sema)
        : ir_ctx_(ir_context), diagnostics_(diagnostics), sema_(sema),
          builder_(ir_context) {}

    // Lower the entire analyzed module to IR
    bool Lower(const std::shared_ptr<Module> &module);

    // Access generated cross-language call descriptors
    const std::vector<CrossLangCallDescriptor> &CallDescriptors() const {
        return call_descriptors_;
    }

  private:
    // Environment for name resolution during lowering
    struct EnvEntry {
        std::string ir_name;
        ir::IRType type{ir::IRType::Invalid()};
        bool is_mutable{false};  // true for VAR (uses alloca/load/store)
    };

    // Statement lowering
    void LowerStatement(const std::shared_ptr<Statement> &stmt);
    void LowerLinkDecl(const std::shared_ptr<LinkDecl> &link);
    void LowerImportDecl(const std::shared_ptr<ImportDecl> &import);
    void LowerExportDecl(const std::shared_ptr<ExportDecl> &export_decl);
    void LowerPipelineDecl(const std::shared_ptr<PipelineDecl> &pipeline);
    void LowerFuncDecl(const std::shared_ptr<FuncDecl> &func);
    void LowerVarDecl(const std::shared_ptr<VarDecl> &var);
    void LowerIfStatement(const std::shared_ptr<IfStatement> &if_stmt);
    void LowerWhileStatement(const std::shared_ptr<WhileStatement> &while_stmt);
    void LowerForStatement(const std::shared_ptr<ForStatement> &for_stmt);
    void LowerMatchStatement(const std::shared_ptr<MatchStatement> &match_stmt);
    void LowerReturnStatement(const std::shared_ptr<ReturnStatement> &ret);
    void LowerWithStatement(const std::shared_ptr<WithStatement> &with_stmt);
    void LowerBlockStatements(const std::vector<std::shared_ptr<Statement>> &stmts);
    void LowerStructDecl(const std::shared_ptr<StructDecl> &struct_decl);
    void LowerMapFuncDecl(const std::shared_ptr<MapFuncDecl> &map_func);
    void LowerExtendDecl(const std::shared_ptr<ExtendDecl> &extend);

    // Expression lowering — returns the IR value name and its type
    struct EvalResult {
        std::string value;
        ir::IRType type{ir::IRType::Invalid()};
    };

    EvalResult LowerExpression(const std::shared_ptr<Expression> &expr);
    EvalResult LowerCallExpression(const std::shared_ptr<CallExpression> &call);
    EvalResult LowerCrossLangCall(const std::shared_ptr<CrossLangCallExpression> &call);
    EvalResult LowerNewExpression(const std::shared_ptr<NewExpression> &new_expr);
    EvalResult LowerMethodCallExpression(const std::shared_ptr<MethodCallExpression> &method_call);
    EvalResult LowerGetAttrExpression(const std::shared_ptr<GetAttrExpression> &get_attr);
    EvalResult LowerSetAttrExpression(const std::shared_ptr<SetAttrExpression> &set_attr);
    EvalResult LowerBinaryExpression(const std::shared_ptr<BinaryExpression> &bin);
    EvalResult LowerUnaryExpression(const std::shared_ptr<UnaryExpression> &unary);
    EvalResult LowerIdentifier(const std::shared_ptr<Identifier> &id);
    EvalResult LowerLiteral(const std::shared_ptr<Literal> &lit);
    EvalResult LowerConvertExpression(const std::shared_ptr<ConvertExpression> &conv);
    EvalResult LowerListLiteral(const std::shared_ptr<ListLiteral> &list);
    EvalResult LowerTupleLiteral(const std::shared_ptr<TupleLiteral> &tuple);
    EvalResult LowerDictLiteral(const std::shared_ptr<DictLiteral> &dict);
    EvalResult LowerStructLiteral(const std::shared_ptr<StructLiteral> &struct_lit);
    EvalResult LowerDeleteExpression(const std::shared_ptr<DeleteExpression> &del_expr);

    // Type conversion
    ir::IRType PloyTypeToIR(const std::shared_ptr<TypeNode> &type_node);
    ir::IRType CoreTypeToIR(const core::Type &ct);

    // Glue code generation for LINK directives
    void GenerateLinkStub(const LinkEntry &link);
    void GenerateMarshalCode(const std::string &src_val, const ir::IRType &src_type,
                             const ir::IRType &dst_type, const std::string &dst_name);

    // Helper
    void Report(const core::SourceLoc &loc, const std::string &message);

    ir::IRContext &ir_ctx_;
    frontends::Diagnostics &diagnostics_;
    const PloySema &sema_;
    ir::IRBuilder builder_;

    std::unordered_map<std::string, EnvEntry> env_{};
    std::vector<CrossLangCallDescriptor> call_descriptors_{};
    // Map from SSA value name to named-argument label for argument reordering.
    std::unordered_map<std::string, std::string> named_arg_labels_{};
    std::shared_ptr<ir::Function> current_function_{};
    bool terminated_{false};
};

} // namespace polyglot::ploy
