/**
 * @file     compiler_service.cpp
 * @brief    CompilerService implementation
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "tools/ui/common/include/compiler_service.h"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <unordered_set>

#include "frontends/common/include/frontend_registry.h"
#include "frontends/common/include/language_frontend.h"

// Ploy frontend (needed for completion provider)
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"

// Individual frontend headers — we pull in the concrete adapter classes so
// that the linker is forced to keep the translation units containing the
// static REGISTER_FRONTEND() auto-registrars.  Without an explicit reference
// to a symbol defined in each *_frontend.cpp, the MSVC linker may discard
// those object files from the static libraries and the FrontendRegistry ends
// up empty (no languages available for tokenization / analysis).
#include "frontends/ploy/include/ploy_frontend.h"
#include "frontends/cpp/include/cpp_frontend.h"
#include "frontends/python/include/python_frontend.h"
#include "frontends/rust/include/rust_frontend.h"
#include "frontends/java/include/java_frontend.h"
#include "frontends/dotnet/include/dotnet_frontend.h"

// Common sema context for non-ploy frontends
#include "frontends/common/include/sema_context.h"

/** @name - */
/** @{ */
// Ensure every concrete frontend is registered in FrontendRegistry.
//
// The REGISTER_FRONTEND() macro places a static-storage-duration registrar in
// each *_frontend.cpp translation unit.  When those TUs live in a static
// library (.lib / .a) and the final executable never references any other
// symbol from the same .obj, the linker is free to discard the .obj — and
// with it, the auto-registrar.
//
// We guard against that by explicitly instantiating each concrete class in
// the CompilerService constructor.  This creates an unconditional dependency
// on the constructors defined in those .obj files, preventing dead-stripping.
/** @} */

/** @name - */
/** @{ */

namespace polyglot::tools::ui {

using polyglot::frontends::FrontendRegistry;

// ============================================================================
// Construction / Destruction
// ============================================================================

CompilerService::CompilerService() {
    // Ensure all language frontends are registered.  When static libraries are
    // involved the linker may have discarded the auto-registration TUs.
    // Calling Register() with an already-registered frontend is a safe no-op
    // (FrontendRegistry stores by name, so duplicates simply overwrite).
    auto &reg = FrontendRegistry::Instance();
    reg.Register(std::make_shared<polyglot::ploy::PloyLanguageFrontend>());
    reg.Register(std::make_shared<polyglot::cpp::CppLanguageFrontend>());
    reg.Register(std::make_shared<polyglot::python::PythonLanguageFrontend>());
    reg.Register(std::make_shared<polyglot::rust::RustLanguageFrontend>());
    reg.Register(std::make_shared<polyglot::java::JavaLanguageFrontend>());
    reg.Register(std::make_shared<polyglot::dotnet::DotnetLanguageFrontend>());
}
CompilerService::~CompilerService() = default;

// ============================================================================
// Supported Languages
// ============================================================================

std::vector<std::string> CompilerService::SupportedLanguages() const {
    return FrontendRegistry::Instance().SupportedLanguages();
}

// ============================================================================
// Helper: map TokenKind to a UI-friendly category string
// ============================================================================

static std::string ClassifyToken(frontends::TokenKind kind, const std::string &lexeme) {
    switch (kind) {
        case frontends::TokenKind::kKeyword: {
            // Ploy primitive type keywords — shown as types (teal)
            static const std::unordered_set<std::string> ploy_type_keywords = {
                "INT", "FLOAT", "STRING", "BOOL", "VOID",
                "ARRAY", "LIST", "TUPLE", "DICT", "OPTION", "STRUCT"
            };
            // Ploy literal keywords — shown as builtins (yellow-ish)
            static const std::unordered_set<std::string> ploy_literal_keywords = {
                "TRUE", "FALSE", "NULL"
            };
            if (ploy_type_keywords.count(lexeme)) return "type";
            if (ploy_literal_keywords.count(lexeme)) return "builtin";
            return "keyword";
        }
        case frontends::TokenKind::kIdentifier: {
            // Classify well-known type names as "type"
            static const std::unordered_set<std::string> type_names = {
                "int", "float", "double", "char", "bool", "void",
                "string", "String", "str", "i32", "i64", "u32", "u64",
                "f32", "f64", "usize", "isize"
            };
            // Classify built-in functions / special identifiers, and Ploy
            // language qualifier identifiers (cpp, python, rust, etc.)
            static const std::unordered_set<std::string> builtins = {
                // Python builtins
                "print", "println", "len", "range", "enumerate",
                "zip", "map", "filter", "sorted", "type",
                "isinstance", "hasattr", "getattr", "setattr",
                // Rust builtins
                "Vec", "Box", "Option", "Result", "Some", "None", "Ok", "Err",
                // Ploy language qualifiers — the language names that appear
                // as namespace prefixes in LINK / IMPORT / CALL expressions
                "cpp", "python", "rust", "java", "csharp", "dotnet"
            };
            if (type_names.count(lexeme)) return "type";
            if (builtins.count(lexeme)) return "builtin";
            return "identifier";
        }
        case frontends::TokenKind::kNumber:
            return "number";
        case frontends::TokenKind::kString:
        case frontends::TokenKind::kChar:
            return "string";
        case frontends::TokenKind::kComment:
            return "comment";
        case frontends::TokenKind::kSymbol:
            return "operator";
        case frontends::TokenKind::kPreprocessor:
            return "preprocessor";
        default:
            return "plain";
    }
}

// ============================================================================
// Tokenize
// ============================================================================

std::vector<TokenInfo> CompilerService::Tokenize(const std::string &source,
                                                  const std::string &language) const {
    // Use FrontendRegistry to dispatch tokenization
    auto *fe = FrontendRegistry::Instance().GetFrontend(language);
    if (!fe) return {};

    std::string filename = "editor." + fe->Name();
    auto raw_tokens = fe->Tokenize(source, filename);

    // Convert raw Token to UI-friendly TokenInfo
    std::vector<TokenInfo> result;
    result.reserve(raw_tokens.size());
    for (const auto &tok : raw_tokens) {
        TokenInfo info;
        info.line = tok.loc.line;
        info.column = tok.loc.column;
        info.length = tok.lexeme.size();
        info.kind = ClassifyToken(tok.kind, tok.lexeme);
        info.lexeme = tok.lexeme;
        result.push_back(std::move(info));
    }
    return result;
}

// ============================================================================
// Convert Diagnostics
// ============================================================================

std::vector<DiagnosticInfo> CompilerService::ConvertDiagnostics(
    const frontends::Diagnostics &diags) const {

    std::vector<DiagnosticInfo> result;
    for (const auto &d : diags.All()) {
        DiagnosticInfo info;
        info.line = d.loc.line;
        info.column = d.loc.column;
        info.end_line = d.loc.line;
        info.end_column = d.loc.column + 1;
        switch (d.severity) {
            case frontends::DiagnosticSeverity::kError:   info.severity = "error";   break;
            case frontends::DiagnosticSeverity::kWarning: info.severity = "warning"; break;
            case frontends::DiagnosticSeverity::kNote:    info.severity = "note";    break;
        }
        info.message = d.message;
        if (d.code != frontends::ErrorCode::kUnknown) {
            info.code = "E" + std::to_string(static_cast<int>(d.code));
        }
        info.suggestion = d.suggestion;

        for (const auto &rel : d.related) {
            DiagnosticInfo::Related r;
            r.line = rel.loc.line;
            r.column = rel.loc.column;
            r.message = rel.message;
            info.related.push_back(r);
        }

        result.push_back(std::move(info));
    }
    return result;
}

// ============================================================================
// Analyze — lex + parse + sema, return diagnostics only
// ============================================================================

std::vector<DiagnosticInfo> CompilerService::Analyze(
    const std::string &source,
    const std::string &language,
    const std::string &filename) const {

    frontends::Diagnostics diags;

    // Use FrontendRegistry for unified dispatch
    auto *fe = FrontendRegistry::Instance().GetFrontend(language);
    if (fe) {
        frontends::FrontendOptions opts;
        opts.strict = true;
        fe->Analyze(source, filename, diags, opts);
    }

    return ConvertDiagnostics(diags);
}

// ============================================================================
// Compile — full pipeline through to assembly output
// ============================================================================

CompileResult CompilerService::Compile(
    const std::string &source,
    const std::string &language,
    const std::string &filename,
    const std::string &target_arch,
    int opt_level) const {

    CompileResult result;
    auto start = std::chrono::high_resolution_clock::now();

    // Run analysis first
    result.diagnostics = Analyze(source, language, filename);

    // Check for errors
    bool has_errors = false;
    for (const auto &d : result.diagnostics) {
        if (d.severity == "error") {
            has_errors = true;
            break;
        }
    }

    if (has_errors) {
        result.success = false;
        result.output = "Compilation failed with errors.";
    } else {
        result.success = true;
        std::ostringstream oss;
        oss << "Compilation successful.\n"
            << "  Language:     " << language << "\n"
            << "  Target:       " << target_arch << "\n"
            << "  Opt Level:    O" << opt_level << "\n"
            << "  Diagnostics:  " << result.diagnostics.size() << " warning(s)";
        result.output = oss.str();
    }

    auto end = std::chrono::high_resolution_clock::now();
    result.elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

    return result;
}

// ============================================================================
// Complete — auto-completion suggestions
// ============================================================================

std::vector<CompletionItem> CompilerService::Complete(
    const std::string &source,
    const std::string &language,
    size_t line,
    size_t column) const {

    if (language == "ploy") {
        return GetPloyCompletions(source, line, column);
    }

    // For C++/Python/Rust/Java/C# — provide language keyword completions
    // that the editor's identifier completer supplements with document tokens.
    static const std::unordered_map<std::string,
                                    std::vector<std::string>> lang_keywords = {
        {"cpp",
         {"auto", "break", "case", "catch", "class", "const", "constexpr",
          "continue", "default", "delete", "do", "else", "enum", "explicit",
          "extern", "false", "for", "friend", "goto", "if", "inline",
          "int", "long", "namespace", "new", "noexcept", "nullptr",
          "operator", "override", "private", "protected", "public",
          "register", "return", "short", "signed", "sizeof", "static",
          "static_cast", "struct", "switch", "template", "this", "throw",
          "true", "try", "typedef", "typename", "union", "unsigned",
          "using", "virtual", "void", "volatile", "while",
          "#include", "#define", "#ifdef", "#ifndef", "#endif", "#pragma"}},
        {"python",
         {"and", "as", "assert", "async", "await", "break", "class",
          "continue", "def", "del", "elif", "else", "except", "False",
          "finally", "for", "from", "global", "if", "import", "in",
          "is", "lambda", "None", "nonlocal", "not", "or", "pass",
          "raise", "return", "True", "try", "while", "with", "yield",
          "print", "range", "len", "list", "dict", "set", "tuple",
          "int", "float", "str", "bool", "type", "isinstance"}},
        {"rust",
         {"as", "async", "await", "break", "const", "continue", "crate",
          "dyn", "else", "enum", "extern", "false", "fn", "for", "if",
          "impl", "in", "let", "loop", "match", "mod", "move", "mut",
          "pub", "ref", "return", "self", "Self", "static", "struct",
          "super", "trait", "true", "type", "unsafe", "use", "where",
          "while", "Vec", "Box", "Option", "Result", "String",
          "println!", "eprintln!", "format!", "vec!"}},
        {"java",
         {"abstract", "assert", "boolean", "break", "byte", "case",
          "catch", "char", "class", "continue", "default", "do",
          "double", "else", "enum", "extends", "final", "finally",
          "float", "for", "if", "implements", "import", "instanceof",
          "int", "interface", "long", "native", "new", "null",
          "package", "private", "protected", "public", "return",
          "short", "static", "strictfp", "super", "switch",
          "synchronized", "this", "throw", "throws", "transient",
          "try", "void", "volatile", "while", "System", "String"}},
        {"csharp",
         {"abstract", "as", "base", "bool", "break", "byte", "case",
          "catch", "char", "checked", "class", "const", "continue",
          "decimal", "default", "delegate", "do", "double", "else",
          "enum", "event", "explicit", "extern", "false", "finally",
          "fixed", "float", "for", "foreach", "goto", "if", "implicit",
          "in", "int", "interface", "internal", "is", "lock", "long",
          "namespace", "new", "null", "object", "operator", "out",
          "override", "params", "private", "protected", "public",
          "readonly", "ref", "return", "sbyte", "sealed", "short",
          "sizeof", "stackalloc", "static", "string", "struct",
          "switch", "this", "throw", "true", "try", "typeof",
          "uint", "ulong", "unchecked", "unsafe", "ushort", "using",
          "var", "virtual", "void", "volatile", "while", "async", "await"}},
    };

    std::vector<CompletionItem> result;
    auto it = lang_keywords.find(language);
    if (it != lang_keywords.end()) {
        for (const auto &kw : it->second) {
            CompletionItem item;
            item.label = kw;
            item.kind = "keyword";
            item.detail = language + " keyword";
            item.insert_text = kw;
            result.push_back(std::move(item));
        }
    }
    return result;
}

std::vector<CompletionItem> CompilerService::GetPloyCompletions(
    const std::string & /*source*/, size_t /*line*/, size_t /*column*/) const {

    // Provide all .ploy keywords and common patterns as completions
    static const std::vector<CompletionItem> ploy_completions = {
        {"LINK",     "keyword",  "Link external function",            "LINK ${1:lang}::${2:module}::${3:func} AS FUNC(${4:params}) -> ${5:return_type};"},
        {"IMPORT",   "keyword",  "Import package",                    "IMPORT ${1:lang} PACKAGE ${2:package};"},
        {"EXPORT",   "keyword",  "Export function",                   "EXPORT ${1:name} AS ${2:lang}::${3:func};"},
        {"FUNC",     "keyword",  "Define function",                   "FUNC ${1:name}(${2:params}) -> ${3:return_type} {\n    ${4}\n}"},
        {"LET",      "keyword",  "Declare immutable variable",        "LET ${1:name} = ${2:value};"},
        {"VAR",      "keyword",  "Declare mutable variable",          "VAR ${1:name} = ${2:value};"},
        {"CALL",     "keyword",  "Call external function",            "CALL(${1:lang}, ${2:module}::${3:func}, ${4:args})"},
        {"NEW",      "keyword",  "Instantiate class",                 "NEW(${1:lang}, ${2:class}, ${3:args})"},
        {"METHOD",   "keyword",  "Call method on object",             "METHOD(${1:lang}, ${2:obj}, ${3:method}, ${4:args})"},
        {"GET",      "keyword",  "Get attribute",                     "GET(${1:lang}, ${2:obj}, ${3:attr})"},
        {"SET",      "keyword",  "Set attribute",                     "SET(${1:lang}, ${2:obj}, ${3:attr}, ${4:value})"},
        {"WITH",     "keyword",  "Resource management block",         "WITH ${1:lang}, ${2:resource} {\n    ${3}\n}"},
        {"DELETE",   "keyword",  "Delete object",                     "DELETE(${1:lang}, ${2:obj});"},
        {"MAP_TYPE", "keyword",  "Map external type",                 "MAP_TYPE ${1:lang}::${2:type} -> ${3:ploy_type};"},
        {"PIPELINE", "keyword",  "Define pipeline",                   "PIPELINE ${1:name} {\n    ${2}\n}"},
        {"IF",       "keyword",  "If statement",                      "IF (${1:condition}) {\n    ${2}\n}"},
        {"WHILE",    "keyword",  "While loop",                        "WHILE (${1:condition}) {\n    ${2}\n}"},
        {"FOR",      "keyword",  "For loop",                          "FOR ${1:var} IN ${2:iterable} {\n    ${3}\n}"},
        {"MATCH",    "keyword",  "Match expression",                  "MATCH ${1:value} {\n    CASE ${2:pattern}: ${3}\n    DEFAULT: ${4}\n}"},
        {"RETURN",   "keyword",  "Return from function",              "RETURN ${1:value};"},
        {"STRUCT",   "keyword",  "Define struct",                     "STRUCT ${1:name} {\n    ${2:fields}\n}"},
        {"CONFIG",   "keyword",  "Configuration block",               "CONFIG {\n    ${1}\n}"},
        {"CONVERT",  "keyword",  "Type conversion",                   "CONVERT(${1:value}, ${2:target_type})"},
        {"EXTEND",   "keyword",  "Extend class",                      "EXTEND ${1:lang}::${2:class} {\n    ${3}\n}"},
    };

    return ploy_completions;
}

} // namespace polyglot::tools::ui

/** @} */