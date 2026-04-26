/**
 * @file     lexer.cpp
 * @brief    Go lexical analyzer
 *
 * @ingroup  Frontend / Go
 * @author   Manning Cyrus
 * @date     2026-04-26
 *
 * Implements Go's tokenizer including the automatic semicolon insertion
 * rule: when a newline follows certain "terminator" tokens (identifier,
 * literal, certain keywords, or a closing bracket), the lexer emits an
 * implicit `;` token.  Doc comments preceding declarations are captured
 * so that the parser can attach them to the declaration AST node.
 */
#include "frontends/go/include/go_lexer.h"

#include <cctype>
#include <string>
#include <unordered_set>

namespace polyglot::go {

using frontends::Token;
using frontends::TokenKind;

namespace {

const std::unordered_set<std::string> &Keywords() {
    static const std::unordered_set<std::string> k{
        "break", "case", "chan", "const", "continue", "default", "defer", "else",
        "fallthrough", "for", "func", "go", "goto", "if", "import", "interface",
        "map", "package", "range", "return", "select", "struct", "switch", "type",
        "var", "true", "false", "nil", "iota"};
    return k;
}

bool IsTerminatorKind(TokenKind k, const std::string &lex) {
    if (k == TokenKind::kIdentifier) return true;
    if (k == TokenKind::kNumber || k == TokenKind::kNumber ||
        k == TokenKind::kString || k == TokenKind::kChar) return true;
    if (k == TokenKind::kKeyword) {
        return lex == "break" || lex == "continue" || lex == "fallthrough" ||
               lex == "return" || lex == "true" || lex == "false" ||
               lex == "nil" || lex == "iota";
    }
    if (k == TokenKind::kSymbol) {
        return lex == ")" || lex == "]" || lex == "}" || lex == "++" || lex == "--";
    }
    return false;
}

}  // namespace

void GoLexer::SkipWhitespaceAndComments() {
    while (!Eof()) {
        char c = Peek();
        if (c == ' ' || c == '\t' || c == '\r') { Get(); continue; }
        if (c == '\n') {
            if (prev_terminator_) { emit_semi_ = true; return; }
            Get();
            continue;
        }
        if (c == '/' && PeekNext() == '/') {
            // Line comment
            std::string line;
            Get(); Get();
            while (!Eof() && Peek() != '\n') { line.push_back(Peek()); Get(); }
            // Capture doc comment block
            if (!line.empty()) {
                if (!pending_doc_.empty()) pending_doc_.push_back('\n');
                pending_doc_ += line;
            }
            continue;
        }
        if (c == '/' && PeekNext() == '*') {
            Get(); Get();
            while (!Eof() && !(Peek() == '*' && PeekNext() == '/')) Get();
            if (!Eof()) { Get(); Get(); }
            continue;
        }
        break;
    }
}

Token GoLexer::LexIdentifierOrKeyword() {
    auto start = CurrentLoc();
    std::string s;
    while (!Eof() && (std::isalnum((unsigned char)Peek()) || Peek() == '_')) {
        s.push_back(Peek()); Get();
    }
    Token t;
    t.loc = start;
    t.lexeme = s;
    if (Keywords().count(s)) {
        t.kind = TokenKind::kKeyword;
    } else {
        t.kind = TokenKind::kIdentifier;
    }
    return t;
}

Token GoLexer::LexNumber() {
    auto start = CurrentLoc();
    std::string s;
    bool is_float = false;
    bool is_imag = false;
    if (Peek() == '0' && (PeekNext() == 'x' || PeekNext() == 'X')) {
        s.push_back(Peek()); Get();
        s.push_back(Peek()); Get();
        while (!Eof() && (std::isxdigit((unsigned char)Peek()) || Peek() == '_')) {
            s.push_back(Peek()); Get();
        }
    } else if (Peek() == '0' && (PeekNext() == 'b' || PeekNext() == 'B')) {
        s.push_back(Peek()); Get();
        s.push_back(Peek()); Get();
        while (!Eof() && (Peek() == '0' || Peek() == '1' || Peek() == '_')) {
            s.push_back(Peek()); Get();
        }
    } else if (Peek() == '0' && (PeekNext() == 'o' || PeekNext() == 'O')) {
        s.push_back(Peek()); Get();
        s.push_back(Peek()); Get();
        while (!Eof() && Peek() >= '0' && Peek() <= '7') {
            s.push_back(Peek()); Get();
        }
    } else {
        while (!Eof() && (std::isdigit((unsigned char)Peek()) || Peek() == '_')) {
            s.push_back(Peek()); Get();
        }
        if (Peek() == '.' && std::isdigit((unsigned char)PeekNext())) {
            is_float = true;
            s.push_back(Peek()); Get();
            while (!Eof() && (std::isdigit((unsigned char)Peek()) || Peek() == '_')) {
                s.push_back(Peek()); Get();
            }
        }
        if (Peek() == 'e' || Peek() == 'E') {
            is_float = true;
            s.push_back(Peek()); Get();
            if (Peek() == '+' || Peek() == '-') { s.push_back(Peek()); Get(); }
            while (!Eof() && std::isdigit((unsigned char)Peek())) {
                s.push_back(Peek()); Get();
            }
        }
    }
    if (Peek() == 'i') { is_imag = true; s.push_back(Peek()); Get(); }
    Token t;
    t.loc = start;
    t.lexeme = s;
    t.kind = (is_float || is_imag) ? TokenKind::kNumber : TokenKind::kNumber;
    return t;
}

Token GoLexer::LexString(char quote) {
    auto start = CurrentLoc();
    std::string s;
    Get();   // opening quote
    while (!Eof() && Peek() != quote) {
        if (Peek() == '\\') {
            s.push_back(Peek()); Get();
            if (!Eof()) { s.push_back(Peek()); Get(); }
            continue;
        }
        if (Peek() == '\n') break;
        s.push_back(Peek()); Get();
    }
    if (Peek() == quote) Get();
    Token t;
    t.loc = start;
    t.lexeme = s;
    t.kind = TokenKind::kString;
    return t;
}

Token GoLexer::LexRawString() {
    auto start = CurrentLoc();
    std::string s;
    Get();   // opening backtick
    while (!Eof() && Peek() != '`') { s.push_back(Peek()); Get(); }
    if (Peek() == '`') Get();
    Token t;
    t.loc = start;
    t.lexeme = s;
    t.kind = TokenKind::kString;
    return t;
}

Token GoLexer::LexRune() {
    auto start = CurrentLoc();
    std::string s;
    Get();
    while (!Eof() && Peek() != '\'') {
        if (Peek() == '\\') { s.push_back(Peek()); Get(); }
        if (!Eof()) { s.push_back(Peek()); Get(); }
    }
    if (Peek() == '\'') Get();
    Token t;
    t.loc = start;
    t.lexeme = s;
    t.kind = TokenKind::kChar;
    return t;
}

Token GoLexer::LexOperator() {
    auto start = CurrentLoc();
    Token t;
    t.loc = start;
    t.kind = TokenKind::kSymbol;
    char c = Peek();
    char d = PeekNext();
    char e = Eof() ? '\0' : (position_ + 2 < source_.size() ? source_[position_ + 2] : '\0');
    // Three-char operators: <<=, >>=, &^=, ...
    if ((c == '<' && d == '<' && e == '=') ||
        (c == '>' && d == '>' && e == '=') ||
        (c == '&' && d == '^' && e == '=') ||
        (c == '.' && d == '.' && e == '.')) {
        t.lexeme = std::string{c, d, e};
        Get(); Get(); Get();
        return t;
    }
    // Two-char operators
    static const char *two[] = {
        "==", "!=", "<=", ">=", "&&", "||", "<-", "++", "--",
        "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", ":=",
        "<<", ">>", "&^", nullptr};
    for (int i = 0; two[i]; ++i) {
        if (c == two[i][0] && d == two[i][1]) {
            t.lexeme = std::string(two[i]);
            Get(); Get();
            return t;
        }
    }
    t.lexeme = std::string(1, c);
    Get();
    return t;
}

Token GoLexer::NextToken() {
    if (emit_semi_) {
        emit_semi_ = false;
        prev_terminator_ = false;
        Token t;
        t.loc = CurrentLoc();
        t.lexeme = ";";
        t.kind = TokenKind::kSymbol;
        return t;
    }
    SkipWhitespaceAndComments();
    if (emit_semi_) {
        emit_semi_ = false;
        prev_terminator_ = false;
        Token t;
        t.loc = CurrentLoc();
        t.lexeme = ";";
        t.kind = TokenKind::kSymbol;
        return t;
    }
    if (Eof()) {
        Token t;
        t.loc = CurrentLoc();
        t.kind = TokenKind::kEndOfFile;
        return t;
    }

    Token tok;
    char c = Peek();
    if (std::isalpha((unsigned char)c) || c == '_') {
        tok = LexIdentifierOrKeyword();
    } else if (std::isdigit((unsigned char)c)) {
        tok = LexNumber();
    } else if (c == '.' && std::isdigit((unsigned char)PeekNext())) {
        tok = LexNumber();
    } else if (c == '"') {
        tok = LexString('"');
    } else if (c == '`') {
        tok = LexRawString();
    } else if (c == '\'') {
        tok = LexRune();
    } else {
        tok = LexOperator();
    }
    prev_terminator_ = IsTerminatorKind(tok.kind, tok.lexeme);
    return tok;
}

}  // namespace polyglot::go
