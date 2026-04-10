// language_frontend.h — Unified language frontend interface.
//
// Defines the ILanguageFrontend abstract base class that every language
// frontend (ploy, cpp, python, rust, java, dotnet) implements.  This
// decouples the compilation driver and UI from concrete frontend types,
// enabling a registry-based dispatch model instead of long if/else chains.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/include/core/types.h"
#include "frontends/common/include/diagnostics.h"
#include "frontends/common/include/lexer_base.h"
#include "middle/include/ir/ir_context.h"

namespace polyglot::frontends {

// ============================================================================
// FrontendOptions — common options passed to frontend pipelines
// ============================================================================

struct FrontendOptions {
    bool verbose{false};              // emit progress diagnostics
    bool strict{false};               // reject placeholders / Any fallback
    bool force{false};                // continue past errors
    bool enable_preprocessing{true};  // run preprocessor before parsing
    std::vector<std::string> include_paths{"."};
};

// ============================================================================
// FrontendResult — outcome of a full frontend pipeline run
// ============================================================================

struct FrontendResult {
    bool success{false};
    bool lowered{false};  // true if IR was produced
};

// ============================================================================
// ForeignFunctionSignature — extracted signature from a foreign source file
// ============================================================================
// Represents a function/method signature extracted by parsing a source file
// in its native language.  Used for cross-language type inference in the
// topology graph and .ploy sema.

struct ForeignFunctionSignature {
    std::string name;                            // Function name (unqualified)
    std::string qualified_name;                  // Module-qualified name (e.g. "math_ops::add")
    std::vector<core::Type> param_types;         // Parameter types (may contain Any for unresolved)
    std::vector<std::string> param_names;        // Parameter names
    core::Type return_type{core::Type::Any()};   // Return type (Any if not determinable)
    bool is_method{false};                       // True if it's a class method
    std::string class_name;                      // Owning class (if is_method)
    bool has_type_annotations{true};             // False if types were inferred rather than declared
};

// ============================================================================
// ILanguageFrontend — abstract interface for a language frontend
// ============================================================================

class ILanguageFrontend {
  public:
    virtual ~ILanguageFrontend() = default;

    // ---- Identity ----

    // Canonical language name (e.g. "ploy", "cpp", "python")
    virtual std::string Name() const = 0;

    // Display name for UI (e.g. "C++", "Python", ".NET/C#")
    virtual std::string DisplayName() const = 0;

    // File extensions this frontend handles (e.g. {".ploy", ".poly"})
    virtual std::vector<std::string> Extensions() const = 0;

    // Alternative language identifiers that resolve to this frontend
    // (e.g. "csharp" -> dotnet, "c" -> cpp)
    virtual std::vector<std::string> Aliases() const { return {}; }

    // ---- Pipeline stages ----

    // Tokenize source code and return a flat token list.
    // Used by the UI for syntax highlighting.
    virtual std::vector<Token> Tokenize(const std::string &source,
                                        const std::string &filename) const = 0;

    // Run the full analysis pipeline (lex + parse + sema) and populate
    // `diagnostics` with any errors/warnings.  Returns true if analysis
    // succeeded without fatal errors.
    virtual bool Analyze(const std::string &source,
                         const std::string &filename,
                         Diagnostics &diagnostics,
                         const FrontendOptions &options) const = 0;

    // Run the full frontend pipeline (lex + parse + sema + lower) and
    // populate `ir_ctx` with the lowered IR.  Returns a FrontendResult
    // describing the outcome.
    virtual FrontendResult Lower(const std::string &source,
                                 const std::string &filename,
                                 ir::IRContext &ir_ctx,
                                 Diagnostics &diagnostics,
                                 const FrontendOptions &options) const = 0;

    // Whether this frontend requires preprocessing (e.g. C++ does, Python does not)
    virtual bool NeedsPreprocessing() const { return false; }

    // ---- Cross-language type extraction ----

    // Parse source code and extract function/method signatures with their
    // parameter types and return types.  For languages with optional type
    // annotations (e.g. Python), the frontend should:
    //   1. Read explicit annotations when present
    //   2. Attempt basic type inference from function bodies when absent
    //   3. Fall back to core::Type::Any() for unresolvable types
    //
    // `module_name` is the module qualifier (e.g. "math_ops" from IMPORT cpp::math_ops).
    // The default implementation returns an empty vector (no extraction support).
    virtual std::vector<ForeignFunctionSignature> ExtractSignatures(
        const std::string &source,
        const std::string &filename,
        const std::string &module_name) const { return {}; }
};

}  // namespace polyglot::frontends
