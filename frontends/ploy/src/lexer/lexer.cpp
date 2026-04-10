#include "frontends/ploy/include/ploy_lexer.h"

#include <cctype>
#include <unordered_set>

namespace polyglot::ploy {

static bool IsIdentStart(unsigned char c) {
    return std::isalpha(c) || c == '_';
}

static bool IsIdentContinue(unsigned char c) {
    return std::isalnum(c) || c == '_';
}

void PloyLexer::SkipWhitespace() {
    while (!Eof()) {
        unsigned char c = static_cast<unsigned char>(Peek());
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            Get();
        } else {
            break;
        }
    }
}

void PloyLexer::SkipLineComment() {
    // Skip until end of line
    while (!Eof() && Peek() != '\n') {
        Get();
    }
}

void PloyLexer::SkipBlockComment() {
    // Already consumed '/*', now find '*/'
    while (!Eof()) {
        if (Peek() == '*' && PeekNext() == '/') {
            Get(); // consume '*'
            Get(); // consume '/'
            return;
        }
        Get();
    }
    // Unterminated block comment — the parser/diagnostics will handle this
}

frontends::Token PloyLexer::LexIdentifierOrKeyword() {
    core::SourceLoc loc = CurrentLoc();
    std::string lexeme;
    while (!Eof() && IsIdentContinue(static_cast<unsigned char>(Peek()))) {
        lexeme.push_back(Get());
    }

    static const std::unordered_set<std::string> keywords = {
        "LINK",     "IMPORT",   "EXPORT",   "MAP_TYPE", "PIPELINE",
        "FUNC",     "LET",      "VAR",      "RETURN",   "RETURNS",
        "IF",       "ELSE",     "WHILE",    "FOR",      "IN",
        "MATCH",    "CASE",     "DEFAULT",  "BREAK",    "CONTINUE",
        "AS",       "TRUE",     "FALSE",    "NULL",     "AND",
        "OR",       "NOT",      "CALL",     "VOID",     "INT",
        "FLOAT",    "STRING",   "BOOL",     "ARRAY",    "STRUCT",
        "PACKAGE",  "LIST",     "TUPLE",    "DICT",     "OPTION",
        "MAP_FUNC", "CONVERT",  "CONFIG",   "VENV",
        "CONDA",    "UV",       "PIPENV",   "POETRY",
        "NEW",      "METHOD",   "GET",      "SET",
        "WITH",     "DELETE",   "EXTEND"
    };

    frontends::TokenKind kind = keywords.count(lexeme) ? frontends::TokenKind::kKeyword
                                                       : frontends::TokenKind::kIdentifier;
    return frontends::Token{kind, lexeme, loc};
}

frontends::Token PloyLexer::LexNumber() {
    core::SourceLoc loc = CurrentLoc();
    std::string lexeme;
    bool is_float = false;

    // Handle hex, binary, octal prefixes
    if (Peek() == '0' && !Eof()) {
        lexeme.push_back(Get());
        if (Peek() == 'x' || Peek() == 'X') {
            lexeme.push_back(Get());
            while (!Eof() && std::isxdigit(static_cast<unsigned char>(Peek()))) {
                lexeme.push_back(Get());
            }
            return frontends::Token{frontends::TokenKind::kNumber, lexeme, loc};
        }
        if (Peek() == 'b' || Peek() == 'B') {
            lexeme.push_back(Get());
            while (!Eof() && (Peek() == '0' || Peek() == '1')) {
                lexeme.push_back(Get());
            }
            return frontends::Token{frontends::TokenKind::kNumber, lexeme, loc};
        }
        if (Peek() == 'o' || Peek() == 'O') {
            lexeme.push_back(Get());
            while (!Eof() && Peek() >= '0' && Peek() <= '7') {
                lexeme.push_back(Get());
            }
            return frontends::Token{frontends::TokenKind::kNumber, lexeme, loc};
        }
    }

    // Decimal digits
    while (!Eof() && std::isdigit(static_cast<unsigned char>(Peek()))) {
        lexeme.push_back(Get());
    }

    // Fractional part
    if (Peek() == '.' && std::isdigit(static_cast<unsigned char>(PeekNext()))) {
        is_float = true;
        lexeme.push_back(Get()); // consume '.'
        while (!Eof() && std::isdigit(static_cast<unsigned char>(Peek()))) {
            lexeme.push_back(Get());
        }
    }

    // Exponent
    if (Peek() == 'e' || Peek() == 'E') {
        is_float = true;
        lexeme.push_back(Get());
        if (Peek() == '+' || Peek() == '-') {
            lexeme.push_back(Get());
        }
        while (!Eof() && std::isdigit(static_cast<unsigned char>(Peek()))) {
            lexeme.push_back(Get());
        }
    }

    (void)is_float; // Token kind is always kNumber; consumers differentiate
    return frontends::Token{frontends::TokenKind::kNumber, lexeme, loc};
}

frontends::Token PloyLexer::LexString() {
    core::SourceLoc loc = CurrentLoc();
    char quote = Get(); // consume opening '"'
    std::string lexeme;
    lexeme.push_back(quote);

    while (!Eof() && Peek() != quote) {
        if (Peek() == '\\') {
            lexeme.push_back(Get()); // consume backslash
            if (!Eof()) {
                lexeme.push_back(Get()); // consume escaped character
            }
        } else {
            lexeme.push_back(Get());
        }
    }

    if (!Eof()) {
        lexeme.push_back(Get()); // consume closing quote
    }

    return frontends::Token{frontends::TokenKind::kString, lexeme, loc};
}

frontends::Token PloyLexer::LexOperator() {
    core::SourceLoc loc = CurrentLoc();
    char c = Get();
    std::string lexeme(1, c);

    switch (c) {
        case '(': case ')': case '{': case '}': case '[': case ']':
        case ',': case ';': case '%':
            break;

        case ':':
            if (Peek() == ':') {
                lexeme.push_back(Get());
            }
            break;

        case '-':
            if (Peek() == '>') {
                lexeme.push_back(Get());
            }
            break;

        case '=':
            if (Peek() == '=') {
                lexeme.push_back(Get());
            }
            break;

        case '!':
            if (Peek() == '=') {
                lexeme.push_back(Get());
            }
            break;

        case '<':
            if (Peek() == '=') {
                lexeme.push_back(Get());
            }
            break;

        case '>':
            if (Peek() == '=') {
                lexeme.push_back(Get());
            }
            break;

        case '&':
            if (Peek() == '&') {
                lexeme.push_back(Get());
            }
            break;

        case '|':
            if (Peek() == '|') {
                lexeme.push_back(Get());
            }
            break;

        case '+': case '*':
            break;

        case '~':
            if (Peek() == '=') {
                lexeme.push_back(Get());
            }
            break;

        case '.':
            if (Peek() == '.') {
                lexeme.push_back(Get());
            }
            break;

        default:
            break;
    }

    return frontends::Token{frontends::TokenKind::kSymbol, lexeme, loc};
}

frontends::Token PloyLexer::NextToken() {
    // Skip whitespace and comments
    while (!Eof()) {
        SkipWhitespace();
        if (Eof()) break;

        // Handle comments
        if (Peek() == '/' && PeekNext() == '/') {
            Get(); Get(); // consume '//'
            SkipLineComment();
            continue;
        }
        if (Peek() == '/' && PeekNext() == '*') {
            Get(); Get(); // consume '/*'
            SkipBlockComment();
            continue;
        }

        break;
    }

    if (Eof()) {
        return frontends::Token{frontends::TokenKind::kEndOfFile, "", CurrentLoc()};
    }

    unsigned char c = static_cast<unsigned char>(Peek());

    // Identifiers and keywords
    if (IsIdentStart(c)) {
        return LexIdentifierOrKeyword();
    }

    // Numbers
    if (std::isdigit(c)) {
        return LexNumber();
    }

    // Strings
    if (c == '"') {
        return LexString();
    }

    // Division operator (not a comment — already handled above)
    if (c == '/') {
        core::SourceLoc loc = CurrentLoc();
        Get();
        return frontends::Token{frontends::TokenKind::kSymbol, "/", loc};
    }

    // Operators and punctuation
    return LexOperator();
}

} // namespace polyglot::ploy
