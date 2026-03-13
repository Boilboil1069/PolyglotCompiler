#include "frontends/dotnet/include/dotnet_lexer.h"

#include <cctype>
#include <string>
#include <unordered_set>

namespace polyglot::dotnet {

static bool IsIdentStart(unsigned char c) {
    return std::isalpha(c) || c == '_' || c == '@';
}

static bool IsIdentContinue(unsigned char c) {
    return std::isalnum(c) || c == '_';
}

frontends::Token DotnetLexer::ReadIdentifierOrKeyword() {
    core::SourceLoc loc = CurrentLoc();
    std::string lexeme;
    bool verbatim = false;

    // Verbatim identifier: @keyword
    if (Peek() == '@' && IsIdentStart(static_cast<unsigned char>(PeekNext()))) {
        Get(); // '@'
        verbatim = true;
    }

    while (!Eof() && IsIdentContinue(static_cast<unsigned char>(Peek()))) {
        lexeme.push_back(Get());
    }

    if (verbatim) {
        return frontends::Token{frontends::TokenKind::kIdentifier, lexeme, loc};
    }

    // C# keywords covering .NET 6-9 features
    static const std::unordered_set<std::string> keywords = {
        // Value types
        "bool",        "byte",        "sbyte",       "char",
        "decimal",     "double",      "float",       "int",
        "uint",        "long",        "ulong",       "short",
        "ushort",      "nint",        "nuint",
        // Reference types
        "object",      "string",      "dynamic",
        // Modifiers
        "abstract",    "async",       "const",       "event",
        "extern",      "new",         "override",    "partial",
        "readonly",    "sealed",      "static",      "unsafe",
        "virtual",     "volatile",    "required",
        // Access modifiers
        "public",      "private",     "protected",   "internal",
        "file",
        // Statements
        "if",          "else",        "switch",      "case",
        "default",     "for",         "foreach",     "do",
        "while",       "break",       "continue",    "goto",
        "return",      "throw",       "try",         "catch",
        "finally",     "using",       "lock",        "yield",
        "fixed",       "checked",     "unchecked",
        // Type keywords
        "class",       "struct",      "interface",   "enum",
        "delegate",    "record",      "namespace",
        // Operators and special
        "is",          "as",          "typeof",      "sizeof",
        "nameof",      "stackalloc",  "await",
        "in",          "out",         "ref",         "params",
        "this",        "base",        "null",        "true",
        "false",       "void",        "var",
        // LINQ
        "from",        "where",       "select",      "group",
        "into",        "orderby",     "join",        "let",
        "ascending",   "descending",  "on",          "equals",
        "by",
        // Pattern matching
        "when",        "and",         "or",          "not",
        // Other
        "global",      "with",        "init",        "value",
        "get",         "set",         "add",         "remove",
        "managed",     "unmanaged",   "notnull",
        "scoped",      "allows",
    };

    frontends::TokenKind kind = keywords.count(lexeme)
                                    ? frontends::TokenKind::kKeyword
                                    : frontends::TokenKind::kIdentifier;
    return frontends::Token{kind, lexeme, loc};
}

frontends::Token DotnetLexer::ReadNumber() {
    core::SourceLoc loc = CurrentLoc();
    std::string lexeme;

    // Handle hex / binary prefixes
    if (Peek() == '0') {
        lexeme.push_back(Get());
        if (Peek() == 'x' || Peek() == 'X') {
            lexeme.push_back(Get());
            while (std::isxdigit(static_cast<unsigned char>(Peek())) || Peek() == '_') {
                if (Peek() != '_') lexeme.push_back(Get());
                else Get();
            }
            return frontends::Token{frontends::TokenKind::kNumber, lexeme, loc};
        }
        if (Peek() == 'b' || Peek() == 'B') {
            lexeme.push_back(Get());
            while (Peek() == '0' || Peek() == '1' || Peek() == '_') {
                if (Peek() != '_') lexeme.push_back(Get());
                else Get();
            }
            return frontends::Token{frontends::TokenKind::kNumber, lexeme, loc};
        }
    }

    // Integer or floating-point
    while (std::isdigit(static_cast<unsigned char>(Peek())) || Peek() == '_') {
        if (Peek() != '_') lexeme.push_back(Get());
        else Get();
    }

    // Decimal point
    if (Peek() == '.' && std::isdigit(static_cast<unsigned char>(PeekNext()))) {
        lexeme.push_back(Get());
        while (std::isdigit(static_cast<unsigned char>(Peek())) || Peek() == '_') {
            if (Peek() != '_') lexeme.push_back(Get());
            else Get();
        }
    }

    // Exponent
    if (Peek() == 'e' || Peek() == 'E') {
        lexeme.push_back(Get());
        if (Peek() == '+' || Peek() == '-') lexeme.push_back(Get());
        while (std::isdigit(static_cast<unsigned char>(Peek()))) {
            lexeme.push_back(Get());
        }
    }

    // Type suffixes: f F d D m M l L u U ul UL lu LU
    if (Peek() == 'f' || Peek() == 'F' || Peek() == 'd' || Peek() == 'D' ||
        Peek() == 'm' || Peek() == 'M') {
        lexeme.push_back(Get());
    } else if (Peek() == 'u' || Peek() == 'U') {
        lexeme.push_back(Get());
        if (Peek() == 'l' || Peek() == 'L') lexeme.push_back(Get());
    } else if (Peek() == 'l' || Peek() == 'L') {
        lexeme.push_back(Get());
        if (Peek() == 'u' || Peek() == 'U') lexeme.push_back(Get());
    }

    return frontends::Token{frontends::TokenKind::kNumber, lexeme, loc};
}

frontends::Token DotnetLexer::ReadString() {
    core::SourceLoc loc = CurrentLoc();
    std::string lexeme;
    lexeme.push_back(Get()); // opening '"'
    while (!Eof() && Peek() != '"') {
        if (Peek() == '\\') {
            lexeme.push_back(Get());
            if (!Eof()) lexeme.push_back(Get());
        } else {
            lexeme.push_back(Get());
        }
    }
    if (!Eof()) lexeme.push_back(Get()); // closing '"'
    return frontends::Token{frontends::TokenKind::kString, lexeme, loc};
}

frontends::Token DotnetLexer::ReadVerbatimString() {
    core::SourceLoc loc = CurrentLoc();
    std::string lexeme;
    lexeme.push_back(Get()); // '@'
    lexeme.push_back(Get()); // '"'
    while (!Eof()) {
        if (Peek() == '"') {
            lexeme.push_back(Get());
            if (Peek() == '"') {
                lexeme.push_back(Get()); // escaped double quote
            } else {
                break;
            }
        } else {
            lexeme.push_back(Get());
        }
    }
    return frontends::Token{frontends::TokenKind::kString, lexeme, loc};
}

frontends::Token DotnetLexer::ReadInterpolatedString() {
    core::SourceLoc loc = CurrentLoc();
    std::string lexeme;
    lexeme.push_back(Get()); // '$'
    if (Peek() == '@') {
        lexeme.push_back(Get()); // '$@' verbatim interpolated
    }
    lexeme.push_back(Get()); // '"'
    int brace_depth = 0;
    while (!Eof()) {
        if (Peek() == '{') {
            lexeme.push_back(Get());
            if (Peek() == '{') {
                lexeme.push_back(Get()); // escaped brace
            } else {
                brace_depth++;
            }
        } else if (Peek() == '}' && brace_depth > 0) {
            lexeme.push_back(Get());
            brace_depth--;
        } else if (Peek() == '"' && brace_depth == 0) {
            lexeme.push_back(Get());
            break;
        } else if (Peek() == '\\') {
            lexeme.push_back(Get());
            if (!Eof()) lexeme.push_back(Get());
        } else {
            lexeme.push_back(Get());
        }
    }
    return frontends::Token{frontends::TokenKind::kString, lexeme, loc};
}

frontends::Token DotnetLexer::ReadChar() {
    core::SourceLoc loc = CurrentLoc();
    std::string lexeme;
    lexeme.push_back(Get()); // opening '\''
    while (!Eof() && Peek() != '\'') {
        if (Peek() == '\\') {
            lexeme.push_back(Get());
            if (!Eof()) lexeme.push_back(Get());
        } else {
            lexeme.push_back(Get());
        }
    }
    if (!Eof()) lexeme.push_back(Get()); // closing '\''
    return frontends::Token{frontends::TokenKind::kChar, lexeme, loc};
}

frontends::Token DotnetLexer::NextToken() {
    // Skip whitespace
    while (!Eof()) {
        char c = Peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            Get();
        } else {
            break;
        }
    }

    if (Eof()) {
        return frontends::Token{frontends::TokenKind::kEndOfFile, "", CurrentLoc()};
    }

    char c = Peek();

    // Line comments
    if (c == '/' && PeekNext() == '/') {
        core::SourceLoc loc = CurrentLoc();
        std::string text;
        Get(); Get();
        while (!Eof() && Peek() != '\n') {
            text.push_back(Get());
        }
        return frontends::Token{frontends::TokenKind::kComment, text, loc};
    }

    // Block comments
    if (c == '/' && PeekNext() == '*') {
        core::SourceLoc loc = CurrentLoc();
        std::string text;
        Get(); Get();
        while (!Eof()) {
            if (Peek() == '*' && PeekNext() == '/') {
                Get(); Get();
                break;
            }
            text.push_back(Get());
        }
        return frontends::Token{frontends::TokenKind::kComment, text, loc};
    }

    // Preprocessor directives
    if (c == '#') {
        core::SourceLoc loc = CurrentLoc();
        std::string text;
        while (!Eof() && Peek() != '\n') {
            text.push_back(Get());
        }
        return frontends::Token{frontends::TokenKind::kComment, text, loc};
    }

    // Interpolated string
    if (c == '$' && (PeekNext() == '"' || PeekNext() == '@')) {
        return ReadInterpolatedString();
    }

    // Verbatim string or verbatim identifier
    if (c == '@') {
        if (PeekNext() == '"') {
            return ReadVerbatimString();
        }
        if (IsIdentStart(static_cast<unsigned char>(PeekNext())) || std::isalpha(static_cast<unsigned char>(PeekNext()))) {
            return ReadIdentifierOrKeyword();
        }
    }

    // String literal
    if (c == '"') {
        return ReadString();
    }

    // Character literal
    if (c == '\'') {
        return ReadChar();
    }

    // Number
    if (std::isdigit(static_cast<unsigned char>(c))) {
        return ReadNumber();
    }

    // Identifier or keyword
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        return ReadIdentifierOrKeyword();
    }

    // Attribute marker [
    // handled as symbol below

    // Multi-character operators
    core::SourceLoc loc = CurrentLoc();

    // Three-character operators
    if (c == '<' && PeekNext() == '<') {
        Get();
        if (Peek() == '=') {
            Get(); Get(); // <<=
            return frontends::Token{frontends::TokenKind::kSymbol, "<<=", loc};
        }
        // <<
        Get();
        return frontends::Token{frontends::TokenKind::kSymbol, "<<", loc};
    }
    if (c == '>' && PeekNext() == '>') {
        Get();
        if (Peek() == '>') {
            Get();
            if (Peek() == '=') {
                Get(); // >>>=
                return frontends::Token{frontends::TokenKind::kSymbol, ">>>=", loc};
            }
            // >>>
            return frontends::Token{frontends::TokenKind::kSymbol, ">>>", loc};
        }
        if (Peek() == '=') {
            Get(); // >>=
            return frontends::Token{frontends::TokenKind::kSymbol, ">>=", loc};
        }
        // >>
        return frontends::Token{frontends::TokenKind::kSymbol, ">>", loc};
    }

    // Two-character operators
    auto tryTwoChar = [&](char first, char second, const std::string &sym) -> bool {
        if (c == first && PeekNext() == second) {
            Get(); Get();
            return true;
        }
        return false;
    };
    (void)tryTwoChar;

    std::string two_char;
    if (!Eof()) {
        char next = PeekNext();
        if (c == '=' && next == '=') two_char = "==";
        else if (c == '!' && next == '=') two_char = "!=";
        else if (c == '<' && next == '=') two_char = "<=";
        else if (c == '>' && next == '=') two_char = ">=";
        else if (c == '&' && next == '&') two_char = "&&";
        else if (c == '|' && next == '|') two_char = "||";
        else if (c == '+' && next == '+') two_char = "++";
        else if (c == '-' && next == '-') two_char = "--";
        else if (c == '+' && next == '=') two_char = "+=";
        else if (c == '-' && next == '=') two_char = "-=";
        else if (c == '*' && next == '=') two_char = "*=";
        else if (c == '/' && next == '=') two_char = "/=";
        else if (c == '%' && next == '=') two_char = "%=";
        else if (c == '&' && next == '=') two_char = "&=";
        else if (c == '|' && next == '=') two_char = "|=";
        else if (c == '^' && next == '=') two_char = "^=";
        else if (c == '=' && next == '>') two_char = "=>";
        else if (c == '-' && next == '>') two_char = "->";
        else if (c == '?' && next == '?') two_char = "??";
        else if (c == '?' && next == '.') two_char = "?.";
        else if (c == '?' && next == '[') two_char = "?[";
        else if (c == '.' && next == '.') two_char = "..";
    }

    if (!two_char.empty()) {
        Get(); Get();
        // Check for ??= (null-coalescing assignment)
        if (two_char == "??" && !Eof() && Peek() == '=') {
            Get();
            return frontends::Token{frontends::TokenKind::kSymbol, "\?\?=", loc};
        }
        return frontends::Token{frontends::TokenKind::kSymbol, two_char, loc};
    }

    // Single-character symbols
    Get();
    return frontends::Token{frontends::TokenKind::kSymbol, std::string(1, c), loc};
}

} // namespace polyglot::dotnet
