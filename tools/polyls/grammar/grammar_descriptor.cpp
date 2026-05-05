/**
 * @file     grammar_descriptor.cpp
 * @brief    Definitions of the bundled tree-sitter-shaped grammars.
 *
 * The descriptors here are the only piece of code that needs to be
 * touched when a new editor language is added: declare its keyword /
 * type / builtin sets and the `kind` → semantic-type mapping, and the
 * runtime + LSP layer pick it up automatically.
 *
 * @ingroup  Tool / polyls
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/polyls/grammar/grammar_descriptor.h"

namespace polyglot::polyls::grammar {

namespace {

/// Default mapping table — every grammar starts from this baseline and
/// then layers language-specific overrides on top.
std::unordered_map<std::string, SemanticMapping> BaselineKindMap() {
  return {
      {"keyword", {6, kModDeclaration}},
      {"type", {1, 0}},
      {"struct", {2, 0}},
      {"function", {3, 0}},
      {"builtin", {3, 0}},
      {"identifier", {4, 0}},
      {"parameter", {5, 0}},
      {"comment", {7, 0}},
      {"string", {8, 0}},
      {"number", {9, kModReadonly}},
      {"operator", {10, 0}},
      {"link", {6, kModDeclaration | kModDefinition}},
      {"preprocessor", {6, kModStatic}},
  };
}

GrammarDescriptor MakePloy() {
  GrammarDescriptor g;
  g.name = "ploy";
  g.display_name = "Ploy";
  g.file_extensions = {".ploy", ".poly"};
  g.keywords = {"FUNC",     "PIPELINE", "STRUCT", "LET",      "VAR",
                "RETURN",   "IF",       "ELSE",   "WHILE",    "FOR",
                "BREAK",    "CONTINUE", "IMPORT", "EXPORT",   "LINK",
                "CONFIG",   "PACKAGE",  "AS",     "NEW",      "METHOD",
                "GET",      "SET",      "WITH",   "DELETE",   "EXTEND",
                "MAP_TYPE", "CONVERT",  "AND",    "OR",       "NOT",
                "TRUE",     "FALSE",    "NULL",   "MAP_FUNC", "CALL"};
  g.primitive_types = {"INT",   "FLOAT", "BOOL", "STRING",
                       "VOID",  "BYTE",  "LONG", "DOUBLE"};
  g.builtins = {"PRINT", "PRINTLN", "ASSERT", "LEN", "PUSH"};
  g.kind_map = BaselineKindMap();
  return g;
}

GrammarDescriptor MakeCpp() {
  GrammarDescriptor g;
  g.name = "cpp";
  g.display_name = "C++";
  g.file_extensions = {".cpp", ".cc", ".cxx", ".hpp", ".h", ".hh",
                       ".hxx"};
  g.keywords = {"alignas",  "alignof", "and",      "auto",       "break",
                "case",     "catch",   "class",    "co_await",   "co_return",
                "co_yield", "concept", "const",    "constexpr",  "consteval",
                "continue", "default", "delete",   "do",         "else",
                "enum",     "explicit","export",   "extern",     "for",
                "friend",   "goto",    "if",       "inline",     "namespace",
                "new",      "noexcept","nullptr",  "operator",   "or",
                "private",  "protected","public",  "register",   "return",
                "sizeof",   "static",  "static_cast","struct",   "switch",
                "template", "this",    "throw",    "try",        "typedef",
                "typeid",   "typename","union",    "using",      "virtual",
                "void",     "volatile","while"};
  g.primitive_types = {"bool",  "char",   "char8_t",  "char16_t",
                       "char32_t","double","float",  "int",
                       "long",  "short",  "signed",   "unsigned",
                       "wchar_t","size_t","int8_t",   "int16_t",
                       "int32_t","int64_t","uint8_t", "uint16_t",
                       "uint32_t","uint64_t"};
  g.builtins = {"std", "printf", "scanf", "malloc", "free"};
  g.kind_map = BaselineKindMap();
  return g;
}

GrammarDescriptor MakePython() {
  GrammarDescriptor g;
  g.name = "python";
  g.display_name = "Python";
  g.file_extensions = {".py", ".pyi"};
  g.keywords = {"and",     "as",     "assert", "async",  "await",
                "break",   "class",  "continue","def",   "del",
                "elif",    "else",   "except", "finally","for",
                "from",    "global", "if",     "import", "in",
                "is",      "lambda", "nonlocal","not",  "or",
                "pass",    "raise",  "return", "try",    "while",
                "with",    "yield",  "True",   "False",  "None"};
  g.primitive_types = {"int",   "float", "str",  "bool",  "bytes",
                       "list",  "tuple", "dict", "set",   "frozenset",
                       "object","type",  "None"};
  g.builtins = {"print", "len",  "range", "input", "open",
                "abs",   "min",  "max",   "sum",   "map",
                "filter","zip",  "enumerate"};
  g.kind_map = BaselineKindMap();
  return g;
}

GrammarDescriptor MakeRust() {
  GrammarDescriptor g;
  g.name = "rust";
  g.display_name = "Rust";
  g.file_extensions = {".rs"};
  g.keywords = {"as",      "async",  "await",  "break",   "const",
                "continue","crate",  "dyn",    "else",    "enum",
                "extern",  "false",  "fn",     "for",     "if",
                "impl",    "in",     "let",    "loop",    "match",
                "mod",     "move",   "mut",    "pub",     "ref",
                "return",  "self",   "Self",   "static",  "struct",
                "super",   "trait",  "true",   "type",    "unsafe",
                "use",     "where",  "while"};
  g.primitive_types = {"bool", "char", "f32",   "f64",   "i8",
                       "i16",  "i32",  "i64",   "i128",  "isize",
                       "u8",   "u16",  "u32",   "u64",   "u128",
                       "usize","str",  "String"};
  g.builtins = {"println", "print", "eprintln", "vec", "format",
                "panic",   "assert"};
  g.kind_map = BaselineKindMap();
  return g;
}

GrammarDescriptor MakeJava() {
  GrammarDescriptor g;
  g.name = "java";
  g.display_name = "Java";
  g.file_extensions = {".java"};
  g.keywords = {"abstract","assert", "break",   "case",    "catch",
                "class",   "const",  "continue","default", "do",
                "else",    "enum",   "extends", "final",   "finally",
                "for",     "goto",   "if",      "implements","import",
                "instanceof","interface","native","new",   "package",
                "private", "protected","public","return",  "static",
                "strictfp","super",  "switch",  "synchronized","this",
                "throw",   "throws", "transient","try",    "void",
                "volatile","while", "true",    "false",   "null"};
  g.primitive_types = {"boolean","byte","char","short","int","long",
                       "float","double","void","String","Object",
                       "Integer","Boolean","Long","Double","Float"};
  g.builtins = {"System", "Math", "println", "print"};
  g.kind_map = BaselineKindMap();
  return g;
}

GrammarDescriptor MakeCSharp() {
  GrammarDescriptor g;
  g.name = "csharp";
  g.display_name = "C#";
  g.file_extensions = {".cs"};
  g.keywords = {"abstract","as",     "base",    "break",   "case",
                "catch",   "checked","class",   "const",   "continue",
                "default", "delegate","do",     "else",    "enum",
                "event",   "explicit","extern", "false",   "finally",
                "fixed",   "for",    "foreach", "goto",    "if",
                "implicit","in",     "interface","internal","is",
                "lock",    "namespace","new",  "null",    "operator",
                "out",     "override","params","private", "protected",
                "public",  "readonly","ref",   "return",  "sealed",
                "sizeof",  "stackalloc","static","struct","switch",
                "this",    "throw",  "true",    "try",     "typeof",
                "unchecked","unsafe","using",  "virtual","void",
                "volatile","while", "yield",  "var",     "async",
                "await"};
  g.primitive_types = {"bool",   "byte",   "sbyte",  "char",   "decimal",
                       "double", "float",  "int",    "uint",   "long",
                       "ulong",  "short",  "ushort", "string", "object",
                       "void"};
  g.builtins = {"Console", "Math", "WriteLine"};
  g.kind_map = BaselineKindMap();
  return g;
}

}  // namespace

const std::unordered_map<std::string, GrammarDescriptor> &KnownGrammars() {
  static const std::unordered_map<std::string, GrammarDescriptor> kTable = [] {
    std::unordered_map<std::string, GrammarDescriptor> m;
    auto add = [&m](GrammarDescriptor g) {
      const std::string key = g.name;
      m.emplace(key, std::move(g));
    };
    add(MakePloy());
    add(MakeCpp());
    add(MakePython());
    add(MakeRust());
    add(MakeJava());
    add(MakeCSharp());
    // Aliases so `language_id` strings sent by various editors all
    // route to the same descriptor.
    m["c++"] = m["cpp"];
    m["poly"] = m["ploy"];
    m["dotnet"] = m["csharp"];
    m["c#"] = m["csharp"];
    return m;
  }();
  return kTable;
}

const GrammarDescriptor *FindGrammar(const std::string &language_id) {
  const auto &t = KnownGrammars();
  auto it = t.find(language_id);
  if (it == t.end()) return nullptr;
  return &it->second;
}

}  // namespace polyglot::polyls::grammar
