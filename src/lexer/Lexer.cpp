#include "lexer/Lexer.h"

#include <cstdio>
#include <set>

namespace codecomplex {

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isIdentStart(char c) { return isLetter(c) || c == '_'; }
bool isIdentChar(char c) { return isIdentStart(c) || isDigit(c); }
bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f'; }

bool isKeyword(const std::string& word) {
    static const std::set<std::string> keywords = {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
        "bool", "break", "case", "catch", "char", "char16_t", "char32_t", "class",
        "compl", "const", "const_cast", "constexpr", "continue", "decltype",
        "default", "delete", "do", "double", "dynamic_cast", "else", "enum",
        "explicit", "export", "extern", "false", "float", "for", "friend", "goto",
        "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept",
        "not", "not_eq", "nullptr", "operator", "or", "or_eq", "private",
        "protected", "public", "register", "reinterpret_cast", "return", "short",
        "signed", "sizeof", "static", "static_assert", "static_cast", "struct",
        "switch", "template", "this", "thread_local", "throw", "true", "try",
        "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual",
        "void", "volatile", "wchar_t", "while", "xor", "xor_eq"};
    return keywords.count(word) > 0;
}

bool isEncodingPrefix(const std::string& word) {
    return word == "L" || word == "u" || word == "U" || word == "u8";
}

bool isRawPrefix(const std::string& word) {
    return word == "R" || word == "LR" || word == "uR" || word == "UR" || word == "u8R";
}

std::string describeChar(char c) {
    if (c >= 33 && c <= 126) return std::string("'") + c + "'";
    char code[8];
    std::snprintf(code, sizeof code, "0x%02X", static_cast<unsigned char>(c));
    return code;
}

std::string withoutCarriageReturns(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') continue;
        out += text[i];
    }
    return out;
}

}

Lexer::Lexer(const std::string& text, Language language)
    : text_(withoutCarriageReturns(text)), language_(language) {}

char Lexer::peek(std::size_t offset) const {
    return pos_ + offset < text_.size() ? text_[pos_ + offset] : '\0';
}

char Lexer::advance() {
    const char c = text_[pos_++];
    if (c == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return c;
}

void Lexer::addToken(TokenKind kind, const std::string& text, int line, int column) {
    result_.tokens.push_back({kind, text, line, column});
}

void Lexer::addError(const std::string& message, int line, int column) {
    result_.errors.push_back({message, line, column});
}

LexResult Lexer::run() {
    while (!atEnd()) {
        const char c = peek();
        if (c == '\n') {
            advance();
            atLineStart_ = true;
        } else if (isSpace(c)) {
            advance();
        } else if (c == '\\' && peek(1) == '\n') {
            advance();
            advance();
        } else if (c == '/' && peek(1) == '/') {
            skipLineComment();
        } else if (c == '/' && peek(1) == '*') {
            skipBlockComment();
        } else if (c == '#' && atLineStart_) {
            lexPreprocessor();
        } else if (c == '@' && language_ == Language::Java && isIdentStart(peek(1))) {
            skipAnnotation();
        } else {
            atLineStart_ = false;
            if (isDigit(c) || (c == '.' && isDigit(peek(1)))) {
                lexNumber();
            } else if (isIdentStart(c)) {
                lexWord();
            } else if (c == '"' || c == '\'') {
                lexQuoted(pos_, line_, column_);
            } else {
                lexOperatorOrPunctuation();
            }
        }
    }
    addToken(TokenKind::EndOfFile, "", line_, column_);
    return result_;
}

void Lexer::skipLineComment() {
    while (!atEnd() && peek() != '\n') {
        if (peek() == '\\' && peek(1) == '\n') advance();
        advance();
    }
}

bool Lexer::skipBlockComment() {
    const int line = line_;
    const int column = column_;
    advance();
    advance();
    while (!atEnd()) {
        if (peek() == '*' && peek(1) == '/') {
            advance();
            advance();
            return true;
        }
        advance();
    }
    addError("unterminated block comment", line, column);
    return false;
}

void Lexer::lexPreprocessor() {
    const int line = line_;
    const int column = column_;
    std::string directive;
    while (!atEnd() && peek() != '\n') {
        const char c = peek();
        const char previous = directive.empty() ? ' ' : directive.back();
        if (c == '\\' && peek(1) == '\n') {
            advance();
            advance();
        } else if (c == '/' && peek(1) == '/') {
            skipLineComment();
        } else if (c == '/' && peek(1) == '*') {
            if (!skipBlockComment()) break;
            directive += ' ';
        } else if (c == '"' || (c == '\'' && !isDigit(previous))) {
            copyQuotedInDirective(directive);
        } else {
            directive += advance();
        }
    }
    while (!directive.empty() && isSpace(directive.back())) directive.pop_back();
    addToken(TokenKind::Preprocessor, directive, line, column);
}

void Lexer::copyQuotedInDirective(std::string& directive) {
    const char quote = advance();
    directive += quote;
    while (!atEnd() && peek() != '\n') {
        if (peek() == '\\' && peek(1) == '\n') {
            advance();
            advance();
            continue;
        }
        const char c = advance();
        directive += c;
        if (c == '\\' && !atEnd() && peek() != '\n') {
            directive += advance();
        } else if (c == quote) {
            return;
        }
    }
}

void Lexer::lexNumber() {
    const std::size_t start = pos_;
    const int line = line_;
    const int column = column_;
    const bool hex = peek() == '0' && (peek(1) == 'x' || peek(1) == 'X');
    while (true) {
        const char c = peek();
        if (isIdentChar(c) || c == '.') {
            advance();
            const bool exponent = hex ? (c == 'p' || c == 'P') : (c == 'e' || c == 'E');
            if (exponent && (peek() == '+' || peek() == '-')) advance();
        } else if (c == '\'' && isIdentChar(peek(1))) {
            advance();
        } else {
            break;
        }
    }
    addToken(TokenKind::Number, textFrom(start), line, column);
}

void Lexer::lexWord() {
    const std::size_t start = pos_;
    const int line = line_;
    const int column = column_;
    while (isIdentChar(peek())) advance();
    const std::string word = textFrom(start);

    if (peek() == '"' && isRawPrefix(word)) {
        lexRawString(start, line, column);
    } else if ((peek() == '"' || peek() == '\'') && isEncodingPrefix(word)) {
        lexQuoted(start, line, column);
    } else {
        addToken(isKeyword(word) ? TokenKind::Keyword : TokenKind::Identifier, word, line, column);
    }
}

void Lexer::lexQuoted(std::size_t start, int line, int column) {
    const char quote = advance();
    const bool isString = quote == '"';
    while (true) {
        if (atEnd() || peek() == '\n') {
            addError(isString ? "unterminated string literal" : "unterminated character literal",
                     line, column);
            return;
        }
        const char c = advance();
        if (c == '\\' && !atEnd()) {
            advance();
        } else if (c == quote) {
            break;
        }
    }
    skipLiteralSuffix();
    addToken(isString ? TokenKind::String : TokenKind::Char, textFrom(start), line, column);
}

void Lexer::lexRawString(std::size_t start, int line, int column) {
    advance();
    std::string delimiter;
    while (!atEnd() && peek() != '(') {
        const char c = peek();
        if (c == ' ' || c == ')' || c == '\\' || c == '"' || c == '\t' || c == '\n' ||
            delimiter.size() == 16) {
            addError("invalid raw string delimiter", line, column);
            return;
        }
        delimiter += advance();
    }
    if (atEnd()) {
        addError("unterminated raw string literal", line, column);
        return;
    }
    advance();

    const std::string closing = ")" + delimiter + "\"";
    const std::size_t end = text_.find(closing, pos_);
    if (end == std::string::npos) {
        addError("unterminated raw string literal", line, column);
        while (!atEnd()) advance();
        return;
    }
    while (pos_ < end + closing.size()) advance();
    skipLiteralSuffix();
    addToken(TokenKind::String, textFrom(start), line, column);
}

void Lexer::skipLiteralSuffix() {
    if (isIdentStart(peek())) {
        while (isIdentChar(peek())) advance();
    }
}

void Lexer::lexOperatorOrPunctuation() {
    static const char* const operators[] = {
        "<=>", "<<=", ">>=", "->*", "...",
        "::", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
        "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", ".*", "##",
        "+", "-", "*", "/", "%", "=", "<", ">", "!", "&", "|", "^", "~",
        "?", ":", ".", "#"};
    const int line = line_;
    const int column = column_;

    if (language_ == Language::Java) {
        for (const char* op : {">>>=", ">>>"}) {
            const std::string text = op;
            if (text_.compare(pos_, text.size(), text) == 0) {
                for (std::size_t i = 0; i < text.size(); ++i) advance();
                addToken(TokenKind::Operator, text, line, column);
                return;
            }
        }
    }
    for (const char* op : operators) {
        const std::string text = op;
        if (text_.compare(pos_, text.size(), text) == 0) {
            for (std::size_t i = 0; i < text.size(); ++i) advance();
            addToken(TokenKind::Operator, text, line, column);
            return;
        }
    }

    const char c = peek();
    if (std::string("(){}[];,").find(c) != std::string::npos) {
        advance();
        addToken(TokenKind::Punctuation, std::string(1, c), line, column);
        return;
    }

    advance();
    addError("unexpected character " + describeChar(c), line, column);
}

void Lexer::skipAnnotation() {
    advance();
    while (isIdentChar(peek()) || (peek() == '.' && isIdentStart(peek(1)))) advance();
    std::size_t look = pos_;
    while (look < text_.size() && (isSpace(text_[look]) || text_[look] == '\n')) ++look;
    if (look >= text_.size() || text_[look] != '(') return;
    while (pos_ < look) advance();
    int depth = 0;
    while (!atEnd()) {
        const char c = advance();
        if (c == '"' || c == '\'') {
            while (!atEnd() && peek() != c && peek() != '\n') {
                if (peek() == '\\') advance();
                if (!atEnd()) advance();
            }
            if (!atEnd() && peek() == c) advance();
        } else if (c == '(') {
            ++depth;
        } else if (c == ')' && --depth == 0) {
            return;
        }
    }
}

LexResult tokenize(const std::string& text, Language language) {
    return Lexer(text, language).run();
}

}
