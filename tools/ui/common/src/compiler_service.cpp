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
#include <fstream>
#include <regex>
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
                "cpp", "python", "rust", "java", "csharp", "dotnet",
                "javascript", "ruby", "go"
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
        {"javascript",
         {"async", "await", "break", "case", "catch", "class", "const",
          "continue", "debugger", "default", "delete", "do", "else",
          "export", "extends", "false", "finally", "for", "function",
          "if", "import", "in", "instanceof", "let", "new", "null",
          "of", "return", "static", "super", "switch", "this", "throw",
          "true", "try", "typeof", "undefined", "var", "void", "while",
          "with", "yield", "console", "Math", "JSON", "Promise"}},
        {"ruby",
         {"BEGIN", "END", "alias", "and", "begin", "break", "case",
          "class", "def", "defined?", "do", "else", "elsif", "end",
          "ensure", "false", "for", "if", "in", "module", "next",
          "nil", "not", "or", "redo", "rescue", "retry", "return",
          "self", "super", "then", "true", "undef", "unless", "until",
          "when", "while", "yield", "puts", "require", "attr_accessor",
          "attr_reader", "attr_writer"}},
        {"go",
         {"break", "case", "chan", "const", "continue", "default",
          "defer", "else", "fallthrough", "for", "func", "go", "goto",
          "if", "import", "interface", "map", "package", "range",
          "return", "select", "struct", "switch", "type", "var",
          "true", "false", "nil", "iota", "int", "int8", "int16",
          "int32", "int64", "uint", "uint8", "uint16", "uint32",
          "uint64", "float32", "float64", "string", "bool", "byte",
          "rune", "error", "make", "new", "len", "cap", "append",
          "copy", "delete", "panic", "recover", "fmt"}},
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
    const std::string &source, size_t /*line*/, size_t /*column*/) const {

    std::vector<CompletionItem> result;

    // Static keyword + snippet completions
    static const std::vector<CompletionItem> ploy_keywords = {
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
        // Built-in type completions
        {"INT",      "type",     "Integer type",                      "INT"},
        {"FLOAT",    "type",     "Floating-point type",               "FLOAT"},
        {"STRING",   "type",     "String type",                       "STRING"},
        {"BOOL",     "type",     "Boolean type",                      "BOOL"},
        {"VOID",     "type",     "Void type",                         "VOID"},
        {"ARRAY",    "type",     "Array type",                        "ARRAY"},
        {"LIST",     "type",     "List type",                         "LIST"},
        {"TUPLE",    "type",     "Tuple type",                        "TUPLE"},
        {"DICT",     "type",     "Dictionary type",                   "DICT"},
        {"OPTION",   "type",     "Option type",                       "OPTION"},
        // Constants
        {"TRUE",     "keyword",  "Boolean true",                      "TRUE"},
        {"FALSE",    "keyword",  "Boolean false",                     "FALSE"},
        {"NULL",     "keyword",  "Null value",                        "NULL"},
        // Resource management
        {"INFER",    "keyword",  "Infer type annotation",             "INFER"},
    };
    result.insert(result.end(), ploy_keywords.begin(), ploy_keywords.end());

    // Extract symbols from current source using lightweight parsing.
    // This scans for FUNC, PIPELINE, LINK, LET, VAR declarations and
    // collects the declared names as completion candidates.
    ExtractSourceSymbols(source, result);

    return result;
}

void CompilerService::ExtractSourceSymbols(
    const std::string &source,
    std::vector<CompletionItem> &out) const {

    std::unordered_set<std::string> seen;
    std::istringstream stream(source);
    std::string line_text;

    while (std::getline(stream, line_text)) {
        // Skip comment lines
        size_t first_non_space = line_text.find_first_not_of(" \t");
        if (first_non_space == std::string::npos) continue;
        if (line_text.size() > first_non_space + 1 &&
            line_text[first_non_space] == '/' && line_text[first_non_space + 1] == '/')
            continue;

        // Tokenize the line by whitespace and special characters
        std::istringstream ls(line_text);
        std::string tok;
        ls >> tok;

        // FUNC name(...) -> type
        if (tok == "FUNC") {
            std::string name;
            ls >> name;
            // Strip trailing '(' if attached
            auto paren = name.find('(');
            if (paren != std::string::npos) name = name.substr(0, paren);
            if (!name.empty() && seen.insert(name).second) {
                out.push_back({name, "function", "FUNC " + name, name});
            }
            continue;
        }

        // PIPELINE name {
        if (tok == "PIPELINE") {
            std::string name;
            ls >> name;
            auto brace = name.find('{');
            if (brace != std::string::npos) name = name.substr(0, brace);
            if (!name.empty() && seen.insert(name).second) {
                out.push_back({name, "function", "PIPELINE " + name, name});
            }
            continue;
        }

        // LINK lang::module::func AS FUNC(...) -> ...
        if (tok == "LINK") {
            std::string qualified;
            ls >> qualified;
            // Extract short name (last component after ::)
            auto last_sep = qualified.rfind("::");
            std::string short_name = (last_sep != std::string::npos)
                ? qualified.substr(last_sep + 2) : qualified;
            // Remove trailing dot-return syntax if present
            auto dot = short_name.find('.');
            if (dot != std::string::npos) short_name = short_name.substr(0, dot);
            if (!short_name.empty() && seen.insert(qualified).second) {
                out.push_back({qualified, "function", "LINK target: " + qualified, short_name});
                // Also add the short name as a separate completion
                if (seen.insert(short_name).second) {
                    out.push_back({short_name, "function", "LINK function: " + short_name, short_name});
                }
            }
            continue;
        }

        // LET name = ...  /  VAR name = ...
        if (tok == "LET" || tok == "VAR") {
            std::string name;
            ls >> name;
            // Strip trailing colon (type annotation) or '='
            auto colon = name.find(':');
            if (colon != std::string::npos) name = name.substr(0, colon);
            auto eq = name.find('=');
            if (eq != std::string::npos) name = name.substr(0, eq);
            if (!name.empty() && seen.insert(name).second) {
                out.push_back({name, "variable", (tok == "LET" ? "immutable" : "mutable"), name});
            }
            continue;
        }

        // STRUCT name {
        if (tok == "STRUCT") {
            std::string name;
            ls >> name;
            auto brace = name.find('{');
            if (brace != std::string::npos) name = name.substr(0, brace);
            if (!name.empty() && seen.insert(name).second) {
                out.push_back({name, "type", "STRUCT " + name, name});
            }
            continue;
        }

        // IMPORT lang PACKAGE pkg
        if (tok == "IMPORT") {
            std::string lang_name, kw_package, pkg;
            ls >> lang_name >> kw_package >> pkg;
            // Strip trailing semicolons/version constraints
            auto semi = pkg.find(';');
            if (semi != std::string::npos) pkg = pkg.substr(0, semi);
            auto selectors = pkg.find("::");
            if (selectors != std::string::npos) pkg = pkg.substr(0, selectors);
            auto ge = pkg.find(">=");
            if (ge != std::string::npos) pkg = pkg.substr(0, ge);
            if (!pkg.empty() && seen.insert(pkg).second) {
                out.push_back({pkg, "module", "IMPORT " + lang_name + " PACKAGE", pkg});
            }
            continue;
        }
    }
}

// ============================================================================
// Workspace Symbol Index
// ============================================================================

void CompilerService::IndexWorkspaceFile(const std::string &path,
                                          const std::string &source,
                                          const std::string &language) {
    std::lock_guard<std::mutex> lock(index_mutex_);

    // Remove existing entries for this file
    for (auto &[name, syms] : workspace_index_) {
        syms.erase(
            std::remove_if(syms.begin(), syms.end(),
                [&path](const IndexedSymbol &s) { return s.file == path; }),
            syms.end());
    }

    // Use lightweight regex-based extraction for each language
    std::istringstream stream(source);
    std::string line_text;
    size_t line_num = 0;

    while (std::getline(stream, line_text)) {
        ++line_num;
        size_t first = line_text.find_first_not_of(" \t");
        if (first == std::string::npos) continue;

        if (language == "ploy") {
            // FUNC name, PIPELINE name, LINK qual, STRUCT name
            std::istringstream ls(line_text);
            std::string tok;
            ls >> tok;
            std::string name;
            if (tok == "FUNC" || tok == "PIPELINE" || tok == "STRUCT") {
                ls >> name;
                auto paren = name.find('(');
                if (paren != std::string::npos) name = name.substr(0, paren);
                auto brace = name.find('{');
                if (brace != std::string::npos) name = name.substr(0, brace);
                if (!name.empty()) {
                    std::string kind = (tok == "FUNC") ? "function"
                                     : (tok == "PIPELINE") ? "function" : "type";
                    workspace_index_[name].push_back(
                        {name, path, line_num, kind, language, tok + " " + name});
                }
            } else if (tok == "LINK") {
                ls >> name;
                auto last_sep = name.rfind("::");
                std::string short_name = (last_sep != std::string::npos)
                    ? name.substr(last_sep + 2) : name;
                auto dot = short_name.find('.');
                if (dot != std::string::npos) short_name = short_name.substr(0, dot);
                if (!short_name.empty()) {
                    workspace_index_[short_name].push_back(
                        {short_name, path, line_num, "function", language, "LINK " + name});
                    workspace_index_[name].push_back(
                        {name, path, line_num, "function", language, "LINK " + name});
                }
            }
        } else if (language == "cpp") {
            // Match function/class/struct definitions
            // Patterns: returntype name(, class name, struct name, void name(
            std::string trimmed = line_text.substr(first);
            // Function: type name(
            static const std::regex func_re(
                R"((?:[\w:*&<>]+\s+)+(\w+)\s*\()");
            // Class/struct
            static const std::regex class_re(
                R"((?:class|struct)\s+(\w+))");
            std::smatch m;
            if (std::regex_search(trimmed, m, class_re)) {
                std::string name = m[1].str();
                workspace_index_[name].push_back(
                    {name, path, line_num, "type", language, "class/struct " + name});
            } else if (std::regex_search(trimmed, m, func_re)) {
                std::string name = m[1].str();
                // Exclude common non-function keywords
                static const std::unordered_set<std::string> skip = {
                    "if", "while", "for", "switch", "return", "catch"};
                if (!skip.count(name)) {
                    workspace_index_[name].push_back(
                        {name, path, line_num, "function", language, name + "(...)"});
                }
            }
        } else if (language == "python") {
            std::string trimmed = line_text.substr(first);
            // def name( or class name
            static const std::regex def_re(R"(def\s+(\w+)\s*\()");
            static const std::regex cls_re(R"(class\s+(\w+))");
            std::smatch m;
            if (std::regex_search(trimmed, m, def_re)) {
                workspace_index_[m[1].str()].push_back(
                    {m[1].str(), path, line_num, "function", language,
                     "def " + m[1].str()});
            } else if (std::regex_search(trimmed, m, cls_re)) {
                workspace_index_[m[1].str()].push_back(
                    {m[1].str(), path, line_num, "type", language,
                     "class " + m[1].str()});
            }
        } else if (language == "rust") {
            std::string trimmed = line_text.substr(first);
            static const std::regex fn_re(R"((?:pub\s+)?fn\s+(\w+))");
            static const std::regex struct_re(R"((?:pub\s+)?struct\s+(\w+))");
            static const std::regex impl_re(R"(impl\s+(\w+))");
            std::smatch m;
            if (std::regex_search(trimmed, m, fn_re)) {
                workspace_index_[m[1].str()].push_back(
                    {m[1].str(), path, line_num, "function", language,
                     "fn " + m[1].str()});
            } else if (std::regex_search(trimmed, m, struct_re)) {
                workspace_index_[m[1].str()].push_back(
                    {m[1].str(), path, line_num, "type", language,
                     "struct " + m[1].str()});
            } else if (std::regex_search(trimmed, m, impl_re)) {
                workspace_index_[m[1].str()].push_back(
                    {m[1].str(), path, line_num, "type", language,
                     "impl " + m[1].str()});
            }
        } else if (language == "java") {
            std::string trimmed = line_text.substr(first);
            static const std::regex cls_re(
                R"((?:public|private|protected)?\s*(?:static\s+)?class\s+(\w+))");
            static const std::regex method_re(
                R"((?:public|private|protected)?\s*(?:static\s+)?[\w<>\[\]]+\s+(\w+)\s*\()");
            std::smatch m;
            if (std::regex_search(trimmed, m, cls_re)) {
                workspace_index_[m[1].str()].push_back(
                    {m[1].str(), path, line_num, "type", language,
                     "class " + m[1].str()});
            } else if (std::regex_search(trimmed, m, method_re)) {
                static const std::unordered_set<std::string> skip = {
                    "if", "while", "for", "switch", "return", "catch"};
                if (!skip.count(m[1].str())) {
                    workspace_index_[m[1].str()].push_back(
                        {m[1].str(), path, line_num, "function", language,
                         m[1].str() + "(...)"});
                }
            }
        } else if (language == "csharp") {
            std::string trimmed = line_text.substr(first);
            static const std::regex cls_re(
                R"((?:public|private|internal|protected)?\s*(?:static\s+)?(?:partial\s+)?class\s+(\w+))");
            static const std::regex method_re(
                R"((?:public|private|internal|protected)?\s*(?:static\s+)?(?:async\s+)?[\w<>\[\]]+\s+(\w+)\s*\()");
            std::smatch m;
            if (std::regex_search(trimmed, m, cls_re)) {
                workspace_index_[m[1].str()].push_back(
                    {m[1].str(), path, line_num, "type", language,
                     "class " + m[1].str()});
            } else if (std::regex_search(trimmed, m, method_re)) {
                static const std::unordered_set<std::string> skip = {
                    "if", "while", "for", "switch", "return", "catch"};
                if (!skip.count(m[1].str())) {
                    workspace_index_[m[1].str()].push_back(
                        {m[1].str(), path, line_num, "function", language,
                         m[1].str() + "(...)"});
                }
            }
        } else if (language == "javascript") {
            std::string trimmed = line_text.substr(first);
            static const std::regex fn_re(
                R"((?:async\s+)?function\s+(\w+)\s*\()");
            static const std::regex cls_re(R"(class\s+(\w+))");
            static const std::regex var_re(
                R"((?:const|let|var)\s+(\w+)\s*=\s*(?:async\s+)?(?:function|\([^)]*\)\s*=>))");
            std::smatch m;
            if (std::regex_search(trimmed, m, fn_re)) {
                workspace_index_[m[1].str()].push_back(
                    {m[1].str(), path, line_num, "function", language,
                     "function " + m[1].str()});
            } else if (std::regex_search(trimmed, m, cls_re)) {
                workspace_index_[m[1].str()].push_back(
                    {m[1].str(), path, line_num, "type", language,
                     "class " + m[1].str()});
            } else if (std::regex_search(trimmed, m, var_re)) {
                workspace_index_[m[1].str()].push_back(
                    {m[1].str(), path, line_num, "function", language,
                     m[1].str() + "(...)"});
            }
        } else if (language == "ruby") {
            std::string trimmed = line_text.substr(first);
            static const std::regex def_re(R"(def\s+(?:self\.)?(\w+))");
            static const std::regex cls_re(R"(class\s+(\w+))");
            static const std::regex mod_re(R"(module\s+(\w+))");
            std::smatch m;
            if (std::regex_search(trimmed, m, def_re)) {
                workspace_index_[m[1].str()].push_back(
                    {m[1].str(), path, line_num, "function", language,
                     "def " + m[1].str()});
            } else if (std::regex_search(trimmed, m, cls_re)) {
                workspace_index_[m[1].str()].push_back(
                    {m[1].str(), path, line_num, "type", language,
                     "class " + m[1].str()});
            } else if (std::regex_search(trimmed, m, mod_re)) {
                workspace_index_[m[1].str()].push_back(
                    {m[1].str(), path, line_num, "type", language,
                     "module " + m[1].str()});
            }
        } else if (language == "go") {
            std::string trimmed = line_text.substr(first);
            static const std::regex fn_re(
                R"(func\s+(?:\([^)]*\)\s+)?(\w+)\s*\()");
            static const std::regex type_re(R"(type\s+(\w+)\s+)");
            std::smatch m;
            if (std::regex_search(trimmed, m, fn_re)) {
                workspace_index_[m[1].str()].push_back(
                    {m[1].str(), path, line_num, "function", language,
                     "func " + m[1].str()});
            } else if (std::regex_search(trimmed, m, type_re)) {
                workspace_index_[m[1].str()].push_back(
                    {m[1].str(), path, line_num, "type", language,
                     "type " + m[1].str()});
            }
        }
    }

    // Clean up empty entries
    for (auto it = workspace_index_.begin(); it != workspace_index_.end(); ) {
        if (it->second.empty()) it = workspace_index_.erase(it);
        else ++it;
    }
}

void CompilerService::ClearWorkspaceIndex() {
    std::lock_guard<std::mutex> lock(index_mutex_);
    workspace_index_.clear();
}

// ============================================================================
// FindDefinition — resolve a symbol to its definition location
// ============================================================================

CompilerService::DefinitionLocation CompilerService::FindDefinition(
    const std::string &symbol,
    const std::string &current_file,
    const std::string &source,
    const std::string &language) const {

    DefinitionLocation result;

    // First, search in the current file for a definition pattern
    std::istringstream stream(source);
    std::string line_text;
    size_t line_num = 0;

    // Patterns that indicate a definition (not just a usage)
    std::vector<std::string> def_patterns;
    if (language == "ploy") {
        def_patterns = {
            "FUNC " + symbol,
            "PIPELINE " + symbol,
            "LINK " + symbol,
            "STRUCT " + symbol,
            "LET " + symbol,
            "VAR " + symbol,
        };
    } else if (language == "cpp") {
        def_patterns = {
            "class " + symbol,
            "struct " + symbol,
        };
        // Also match function defs: type symbol(
    } else if (language == "python") {
        def_patterns = {"def " + symbol, "class " + symbol};
    } else if (language == "rust") {
        def_patterns = {"fn " + symbol, "struct " + symbol, "enum " + symbol};
    } else if (language == "java" || language == "csharp") {
        def_patterns = {"class " + symbol};
    } else if (language == "javascript") {
        def_patterns = {"function " + symbol, "class " + symbol,
                        "const " + symbol, "let " + symbol, "var " + symbol};
    } else if (language == "ruby") {
        def_patterns = {"def " + symbol, "class " + symbol, "module " + symbol};
    } else if (language == "go") {
        def_patterns = {"func " + symbol, "type " + symbol,
                        "var " + symbol, "const " + symbol};
    }

    while (std::getline(stream, line_text)) {
        ++line_num;
        for (const auto &pat : def_patterns) {
            if (line_text.find(pat) != std::string::npos) {
                result.file = current_file;
                result.line = line_num;
                result.column = line_text.find(symbol) + 1;
                result.found = true;
                return result;
            }
        }
        // For C++ function defs: match "symbol(" preceded by a type
        if (language == "cpp" || language == "java" || language == "csharp") {
            std::string pat = symbol + "(";
            auto pos = line_text.find(pat);
            if (pos != std::string::npos && pos > 0) {
                // Verify it's preceded by whitespace (likely a definition)
                char before = line_text[pos - 1];
                if (before == ' ' || before == '\t' || before == '*' ||
                    before == '&' || before == '>') {
                    result.file = current_file;
                    result.line = line_num;
                    result.column = pos + 1;
                    result.found = true;
                    return result;
                }
            }
        }
    }

    // Second, search in the workspace index
    {
        std::lock_guard<std::mutex> lock(index_mutex_);
        auto it = workspace_index_.find(symbol);
        if (it != workspace_index_.end() && !it->second.empty()) {
            // Prefer definitions from different files (cross-language navigation)
            for (const auto &sym : it->second) {
                if (sym.file != current_file) {
                    result.file = sym.file;
                    result.line = sym.line;
                    result.column = 1;
                    result.found = true;
                    return result;
                }
            }
            // Fallback: same file definition in workspace index
            const auto &sym = it->second.front();
            result.file = sym.file;
            result.line = sym.line;
            result.column = 1;
            result.found = true;
            return result;
        }
    }

    // For .ploy files: if the symbol is a qualified name (lang::module::func),
    // try to find the foreign source file by scanning the workspace for
    // matching function definitions.
    if (language == "ploy" && symbol.find("::") != std::string::npos) {
        auto last_sep = symbol.rfind("::");
        std::string short_name = symbol.substr(last_sep + 2);
        std::lock_guard<std::mutex> lock(index_mutex_);
        auto it = workspace_index_.find(short_name);
        if (it != workspace_index_.end() && !it->second.empty()) {
            const auto &sym = it->second.front();
            result.file = sym.file;
            result.line = sym.line;
            result.column = 1;
            result.found = true;
            return result;
        }
    }

    return result; // not found
}

} // namespace polyglot::tools::ui

/** @} */