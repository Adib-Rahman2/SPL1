#pragma once

#include "lexer/Token.h"
#include "source/Language.h"

#include <cstddef>
#include <string>
#include <vector>

namespace codecomplex {

struct LexError {
    std::string message;
    int line = 1;
    int column = 1;
};

struct LexResult {
    std::vector<Token> tokens;
    std::vector<LexError> errors;

    bool ok() const { return errors.empty(); }
};

class Lexer {
public:
    explicit Lexer(const std::string& text, Language language = Language::Cpp);

    LexResult run();

private:
    char peek(std::size_t offset = 0) const;
    char advance();
    bool atEnd() const { return pos_ >= text_.size(); }

    void addToken(TokenKind kind, const std::string& text, int line, int column);
    std::string textFrom(std::size_t start) const { return text_.substr(start, pos_ - start); }
    void addError(const std::string& message, int line, int column);

    void skipLineComment();
    bool skipBlockComment();
    void lexPreprocessor();
    void copyQuotedInDirective(std::string& directive);
    void lexNumber();
    void lexWord();
    void lexQuoted(std::size_t start, int line, int column);
    void lexRawString(std::size_t start, int line, int column);
    void skipLiteralSuffix();
    void lexOperatorOrPunctuation();
    void skipAnnotation();

    std::string text_;
    Language language_;
    std::size_t pos_ = 0;
    int line_ = 1;
    int column_ = 1;
    bool atLineStart_ = true;
    LexResult result_;
};

LexResult tokenize(const std::string& text, Language language = Language::Cpp);

}
