/**
 * @file     lexer.cpp
 * @brief    JavaScript lexer implementation (ES2020+)
 *
 * @ingroup  Frontend / JavaScript
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#include "frontends/javascript/include/javascript_lexer.h"

#include <cctype>
#include <string>
#include <unordered_set>

namespace polyglot::javascript {

namespace {

bool IsIdentStart(unsigned char c) {
    return std::isalpha(c) || c == '_' || c == '$';
}

bool IsIdentContinue(unsigned char c) {
    return std::isalnum(c) || c == '_' || c == '$';
}

const std::unordered_set<std::string> &Keywords() {
    static const std::unordered_set<std::string> kw = {
        // Reserved keywords (ES2020+)
        "break",      "case",       "catch",      "class",      "const",
        "continue",   "debugger",   "default",    "delete",     "do",
        "else",       "export",     "extends",    "finally",    "for",
        "function",   "if",         "import",     "in",         "instanceof",
        "new",        "of",         "return",     "super",      "switch",
        "this",       "throw",      "try",        "typeof",     "var",
        "void",       "while",      "with",       "yield",
        // Future / contextual keywords
        "async",      "await",      "let",        "static",
        // Literals (treated as keywords for parser convenience)
        "true",       "false",      "null",       "undefined",
        // Strict-mode-only / JSDoc adjacent
        "enum",       "implements", "interface",  "package",
        "private",    "protected",  "public",     "as",         "from",
    };
    return kw;
}

}  // namespace

void JsLexer::SkipWhitespace() {
    while (!Eof()) {
        char c = Peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            Get();
        } else {
            break;
        }
    }
}

void JsLexer::SkipLineComment() {
    while (!Eof() && Peek() != '\n') Get();
}

void JsLexer::SkipBlockComment(bool *had_doc) {
    // We are positioned just after "/*". Detect a leading "*" (excluding "*/")
    // to recognise JSDoc comments; capture the body for the parser to consume
    // when it encounters the next declaration.
    bool is_doc = (Peek() == '*' && PeekNext() != '/');
    std::string body;
    while (!Eof()) {
        if (Peek() == '*' && PeekNext() == '/') {
            Get();
            Get();
            break;
        }
        body.push_back(Get());
    }
    if (is_doc) {
        pending_doc_ = body;
        if (had_doc) *had_doc = true;
    }
}

frontends::Token JsLexer::LexIdentifierOrKeyword() {
    core::SourceLoc loc = CurrentLoc();
    std::string lexeme;
    if (Peek() == '#') {
        // Private name (e.g. `#field`).
        lexeme.push_back(Get());
    }
    while (IsIdentContinue(static_cast<unsigned char>(Peek()))) {
        lexeme.push_back(Get());
    }

    // Don't treat `#name` as keyword.
    bool is_private = !lexeme.empty() && lexeme[0] == '#';
    auto kind = (!is_private && Keywords().count(lexeme))
                    ? frontends::TokenKind::kKeyword
                    : frontends::TokenKind::kIdentifier;
    return frontends::Token{kind, lexeme, loc};
}

frontends::Token JsLexer::LexNumber() {
    core::SourceLoc loc = CurrentLoc();
    std::string lexeme;

    // Handle prefixes: 0x, 0o, 0b
    if (Peek() == '0' && (PeekNext() == 'x' || PeekNext() == 'X' ||
                            PeekNext() == 'o' || PeekNext() == 'O' ||
                            PeekNext() == 'b' || PeekNext() == 'B')) {
        lexeme.push_back(Get());
        lexeme.push_back(Get());
        while (std::isalnum(static_cast<unsigned char>(Peek())) || Peek() == '_') {
            if (Peek() != '_') lexeme.push_back(Get());
            else Get();
        }
    } else {
        while (std::isdigit(static_cast<unsigned char>(Peek())) || Peek() == '_') {
            if (Peek() != '_') lexeme.push_back(Get());
            else Get();
        }
        // Fractional
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
    }
    // BigInt suffix
    if (Peek() == 'n') lexeme.push_back(Get());

    return frontends::Token{frontends::TokenKind::kNumber, lexeme, loc};
}

frontends::Token JsLexer::LexString(char quote) {
    core::SourceLoc loc = CurrentLoc();
    std::string lexeme;
    lexeme.push_back(Get()); // opening quote
    while (!Eof() && Peek() != quote) {
        if (Peek() == '\\') {
            lexeme.push_back(Get());
            if (!Eof()) lexeme.push_back(Get());
        } else if (Peek() == '\n') {
            // Unterminated single-quoted string; bail out.
            break;
        } else {
            lexeme.push_back(Get());
        }
    }
    if (!Eof()) lexeme.push_back(Get()); // closing quote
    return frontends::Token{frontends::TokenKind::kString, lexeme, loc};
}

frontends::Token JsLexer::LexTemplateString() {
    core::SourceLoc loc = CurrentLoc();
    std::string lexeme;
    lexeme.push_back(Get()); // opening backtick
    int brace_depth = 0;
    while (!Eof()) {
        char c = Peek();
        if (c == '`' && brace_depth == 0) {
            lexeme.push_back(Get());
            break;
        }
        if (c == '\\') {
            lexeme.push_back(Get());
            if (!Eof()) lexeme.push_back(Get());
            continue;
        }
        if (c == '$' && PeekNext() == '{') {
            lexeme.push_back(Get());
            lexeme.push_back(Get());
            ++brace_depth;
            continue;
        }
        if (brace_depth > 0) {
            if (c == '{') ++brace_depth;
            else if (c == '}') --brace_depth;
        }
        lexeme.push_back(Get());
    }
    return frontends::Token{frontends::TokenKind::kString, lexeme, loc};
}

frontends::Token JsLexer::LexRegex() {
    core::SourceLoc loc = CurrentLoc();
    std::string lexeme;
    lexeme.push_back(Get()); // '/'
    bool in_class = false;
    while (!Eof()) {
        char c = Peek();
        if (c == '\\') {
            lexeme.push_back(Get());
            if (!Eof()) lexeme.push_back(Get());
            continue;
        }
        if (c == '[') in_class = true;
        else if (c == ']') in_class = false;
        else if (c == '/' && !in_class) {
            lexeme.push_back(Get());
            break;
        } else if (c == '\n') {
            break;
        }
        lexeme.push_back(Get());
    }
    // Flags
    while (IsIdentContinue(static_cast<unsigned char>(Peek()))) {
        lexeme.push_back(Get());
    }
    return frontends::Token{frontends::TokenKind::kString, lexeme, loc};
}

frontends::Token JsLexer::LexOperator() {
    core::SourceLoc loc = CurrentLoc();
    static const char *multi_ops[] = {
        ">>>=", "**=", "<<=", ">>=", "&&=", "||=", "??=", "...",
        ">>>",  "**",  "<<",  ">>",  "&&",  "||",  "??",  "==",  "!=",
        "===",  "!==", "<=",  ">=",  "+=",  "-=",  "*=",  "/=",  "%=",
        "&=",   "|=",  "^=",  "++",  "--",  "=>",  "?.",
        nullptr
    };
    // ES has === / !== which are 3 chars; check 4-char first then 3 then 2.
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

frontends::Token JsLexer::NextToken() {
    while (true) {
        SkipWhitespace();
        if (Eof()) {
            return frontends::Token{frontends::TokenKind::kEndOfFile, "", CurrentLoc()};
        }
        char c = Peek();

        // Line comment
        if (c == '/' && PeekNext() == '/') {
            Get();
            Get();
            SkipLineComment();
            continue;
        }
        // Block / JSDoc comment
        if (c == '/' && PeekNext() == '*') {
            Get();
            Get();
            bool had_doc = false;
            SkipBlockComment(&had_doc);
            continue;
        }

        core::SourceLoc loc = CurrentLoc();

        if (c == '"' || c == '\'') {
            auto tok = LexString(c);
            prev_allows_regex_ = false;
            return tok;
        }
        if (c == '`') {
            auto tok = LexTemplateString();
            prev_allows_regex_ = false;
            return tok;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            auto tok = LexNumber();
            prev_allows_regex_ = false;
            return tok;
        }
        if (IsIdentStart(static_cast<unsigned char>(c)) || c == '#') {
            auto tok = LexIdentifierOrKeyword();
            // After most identifiers a regex isn't valid; after these
            // keywords it is.
            static const std::unordered_set<std::string> regex_after = {
                "return", "typeof", "instanceof", "in", "of", "new", "delete",
                "void",   "throw",  "yield",      "await", "case"
            };
            prev_allows_regex_ = regex_after.count(tok.lexeme) > 0;
            return tok;
        }

        if (c == '/') {
            if (prev_allows_regex_) {
                auto tok = LexRegex();
                prev_allows_regex_ = false;
                return tok;
            }
        }

        auto tok = LexOperator();
        // After a closing bracket / identifier / literal, a slash means
        // division.  After other operators, a slash starts a regex.
        static const std::unordered_set<std::string> div_after = {
            ")", "]", "}", "++", "--"
        };
        prev_allows_regex_ = div_after.count(tok.lexeme) == 0;
        // Track contextual loc usage (silence unused warning in some toolchains).
        (void)loc;
        return tok;
    }
}

}  // namespace polyglot::javascript
