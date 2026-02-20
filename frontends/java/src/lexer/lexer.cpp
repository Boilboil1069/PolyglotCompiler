#include "frontends/java/include/java_lexer.h"

#include <cctype>
#include <string>
#include <unordered_set>

namespace polyglot::java {

static bool IsIdentStart(unsigned char c) {
    return std::isalpha(c) || c == '_' || c == '$';
}

static bool IsIdentContinue(unsigned char c) {
    return std::isalnum(c) || c == '_' || c == '$';
}

void JavaLexer::SkipWhitespace() {
    while (!Eof()) {
        char c = Peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            Get();
        } else {
            break;
        }
    }
}

void JavaLexer::SkipLineComment() {
    while (!Eof() && Peek() != '\n') {
        Get();
    }
}

void JavaLexer::SkipBlockComment() {
    while (!Eof()) {
        if (Peek() == '*' && PeekNext() == '/') {
            Get();
            Get();
            return;
        }
        Get();
    }
}

frontends::Token JavaLexer::LexIdentifierOrKeyword() {
    core::SourceLoc loc = CurrentLoc();
    std::string lexeme;
    while (IsIdentContinue(static_cast<unsigned char>(Peek()))) {
        lexeme.push_back(Get());
    }

    // Java keywords across all supported versions (8, 17, 21, 23)
    static const std::unordered_set<std::string> keywords = {
        // Core keywords (Java 8+)
        "abstract",    "assert",      "boolean",     "break",
        "byte",        "case",        "catch",       "char",
        "class",       "const",       "continue",    "default",
        "do",          "double",      "else",        "enum",
        "extends",     "final",       "finally",     "float",
        "for",         "goto",        "if",          "implements",
        "import",      "instanceof",  "int",         "interface",
        "long",        "native",      "new",         "package",
        "private",     "protected",   "public",      "return",
        "short",       "static",      "strictfp",    "super",
        "switch",      "synchronized","this",        "throw",
        "throws",      "transient",   "try",         "void",
        "volatile",    "while",
        // Literals
        "true",        "false",       "null",
        // Contextual keywords (Java 9+)
        "var",         "module",      "requires",    "exports",
        "opens",       "uses",        "provides",    "with",
        "to",          "transitive",  "open",
        // Contextual keywords (Java 14+)
        "yield",       "record",
        // Contextual keywords (Java 17+)
        "sealed",      "non-sealed",  "permits",
        // Contextual keywords (Java 21+)
        "when",
        // Annotation-related
        "@interface"
    };

    frontends::TokenKind kind = keywords.count(lexeme)
                                    ? frontends::TokenKind::kKeyword
                                    : frontends::TokenKind::kIdentifier;
    return frontends::Token{kind, lexeme, loc};
}

frontends::Token JavaLexer::LexNumber() {
    core::SourceLoc loc = CurrentLoc();
    std::string lexeme;

    // Handle hex, octal, binary prefixes
    if (Peek() == '0') {
        lexeme.push_back(Get());
        if (Peek() == 'x' || Peek() == 'X') {
            lexeme.push_back(Get());
            while (std::isxdigit(static_cast<unsigned char>(Peek())) || Peek() == '_') {
                if (Peek() != '_') lexeme.push_back(Get());
                else Get();
            }
        } else if (Peek() == 'b' || Peek() == 'B') {
            lexeme.push_back(Get());
            while (Peek() == '0' || Peek() == '1' || Peek() == '_') {
                if (Peek() != '_') lexeme.push_back(Get());
                else Get();
            }
        } else {
            // Octal or decimal with leading zero
            while (std::isdigit(static_cast<unsigned char>(Peek())) || Peek() == '_') {
                if (Peek() != '_') lexeme.push_back(Get());
                else Get();
            }
        }
    } else {
        while (std::isdigit(static_cast<unsigned char>(Peek())) || Peek() == '_') {
            if (Peek() != '_') lexeme.push_back(Get());
            else Get();
        }
    }

    // Fractional part
    if (Peek() == '.' && std::isdigit(static_cast<unsigned char>(PeekNext()))) {
        lexeme.push_back(Get()); // '.'
        while (std::isdigit(static_cast<unsigned char>(Peek())) || Peek() == '_') {
            if (Peek() != '_') lexeme.push_back(Get());
            else Get();
        }
    }

    // Exponent
    if (Peek() == 'e' || Peek() == 'E') {
        lexeme.push_back(Get());
        if (Peek() == '+' || Peek() == '-') {
            lexeme.push_back(Get());
        }
        while (std::isdigit(static_cast<unsigned char>(Peek()))) {
            lexeme.push_back(Get());
        }
    }

    // Type suffix: L, l, F, f, D, d
    if (Peek() == 'L' || Peek() == 'l' || Peek() == 'F' || Peek() == 'f' ||
        Peek() == 'D' || Peek() == 'd') {
        lexeme.push_back(Get());
    }

    return frontends::Token{frontends::TokenKind::kNumber, lexeme, loc};
}

frontends::Token JavaLexer::LexString() {
    core::SourceLoc loc = CurrentLoc();
    std::string lexeme;
    lexeme.push_back(Get()); // opening quote

    while (!Eof() && Peek() != '"') {
        if (Peek() == '\\') {
            lexeme.push_back(Get()); // backslash
            if (!Eof()) lexeme.push_back(Get()); // escaped char
        } else {
            lexeme.push_back(Get());
        }
    }
    if (!Eof()) lexeme.push_back(Get()); // closing quote

    return frontends::Token{frontends::TokenKind::kString, lexeme, loc};
}

frontends::Token JavaLexer::LexChar() {
    core::SourceLoc loc = CurrentLoc();
    std::string lexeme;
    lexeme.push_back(Get()); // opening single quote

    while (!Eof() && Peek() != '\'') {
        if (Peek() == '\\') {
            lexeme.push_back(Get());
            if (!Eof()) lexeme.push_back(Get());
        } else {
            lexeme.push_back(Get());
        }
    }
    if (!Eof()) lexeme.push_back(Get()); // closing single quote

    return frontends::Token{frontends::TokenKind::kChar, lexeme, loc};
}

frontends::Token JavaLexer::LexTextBlock() {
    // Java 13+ text blocks: """..."""
    core::SourceLoc loc = CurrentLoc();
    std::string lexeme;
    // Consume the opening """
    lexeme.push_back(Get());
    lexeme.push_back(Get());
    lexeme.push_back(Get());

    while (!Eof()) {
        if (Peek() == '"' && PeekNext() == '"') {
            size_t pos = position_;
            char c3 = (pos + 2 < source_.size()) ? source_[pos + 2] : '\0';
            if (c3 == '"') {
                lexeme.push_back(Get());
                lexeme.push_back(Get());
                lexeme.push_back(Get());
                return frontends::Token{frontends::TokenKind::kString, lexeme, loc};
            }
        }
        if (Peek() == '\\') {
            lexeme.push_back(Get());
            if (!Eof()) lexeme.push_back(Get());
        } else {
            lexeme.push_back(Get());
        }
    }

    return frontends::Token{frontends::TokenKind::kString, lexeme, loc};
}

frontends::Token JavaLexer::LexAnnotation() {
    // Consume '@' and following identifier
    core::SourceLoc loc = CurrentLoc();
    std::string lexeme;
    lexeme.push_back(Get()); // '@'
    while (IsIdentContinue(static_cast<unsigned char>(Peek())) || Peek() == '.') {
        lexeme.push_back(Get());
    }
    return frontends::Token{frontends::TokenKind::kKeyword, lexeme, loc};
}

frontends::Token JavaLexer::LexOperator() {
    core::SourceLoc loc = CurrentLoc();

    // Multi-character operators (longest match first)
    static const char *multi_ops[] = {
        ">>>=", "<<=", ">>=",
        ">>>",  "&&",  "||",  "==",  "!=",  "<=",  ">=",
        "<<",   ">>",  "++",  "--",  "+=",  "-=",  "*=",
        "/=",   "%=",  "&=",  "|=",  "^=",  "->",  "::",
        nullptr
    };

    for (int i = 0; multi_ops[i]; ++i) {
        std::string op(multi_ops[i]);
        if (source_.compare(position_, op.size(), op) == 0) {
            for (size_t j = 0; j < op.size(); ++j) Get();
            return frontends::Token{frontends::TokenKind::kSymbol, op, loc};
        }
    }

    char c = Get();
    return frontends::Token{frontends::TokenKind::kSymbol, std::string(1, c), loc};
}

frontends::Token JavaLexer::NextToken() {
    if (!pending_.empty()) {
        auto tok = pending_.front();
        pending_.erase(pending_.begin());
        return tok;
    }

    SkipWhitespace();
    core::SourceLoc loc = CurrentLoc();
    char c = Peek();

    if (c == '\0') {
        return frontends::Token{frontends::TokenKind::kEndOfFile, "", loc};
    }

    // Line comment
    if (c == '/' && PeekNext() == '/') {
        Get();
        Get();
        SkipLineComment();
        return NextToken();
    }

    // Block comment
    if (c == '/' && PeekNext() == '*') {
        Get();
        Get();
        SkipBlockComment();
        return NextToken();
    }

    // Text block (Java 13+): """
    if (c == '"' && PeekNext() == '"') {
        size_t pos = position_;
        char c3 = (pos + 2 < source_.size()) ? source_[pos + 2] : '\0';
        if (c3 == '"') {
            return LexTextBlock();
        }
    }

    // String literal
    if (c == '"') {
        return LexString();
    }

    // Char literal
    if (c == '\'') {
        return LexChar();
    }

    // Annotation
    if (c == '@') {
        return LexAnnotation();
    }

    // Identifier or keyword
    if (IsIdentStart(static_cast<unsigned char>(c))) {
        return LexIdentifierOrKeyword();
    }

    // Number literal
    if (std::isdigit(static_cast<unsigned char>(c))) {
        return LexNumber();
    }

    // Operator / symbol
    (void)loc;
    return LexOperator();
}

} // namespace polyglot::java
