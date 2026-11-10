#pragma once

#include <ostream>
#include <string>

namespace codecomplex {

enum class TokenKind {
    Identifier,
    Keyword,
    Number,
    String,
    Char,
    Operator,
    Punctuation,
    Preprocessor,
    EndOfFile
};

struct Token {
    TokenKind kind = TokenKind::EndOfFile;
    std::string text;
    int line = 1;
    int column = 1;
};

inline const char* tokenKindName(TokenKind kind) {
    switch (kind) {
    case TokenKind::Identifier:   return "Identifier";
    case TokenKind::Keyword:      return "Keyword";
    case TokenKind::Number:       return "Number";
    case TokenKind::String:       return "String";
    case TokenKind::Char:         return "Char";
    case TokenKind::Operator:     return "Operator";
    case TokenKind::Punctuation:  return "Punctuation";
    case TokenKind::Preprocessor: return "Preprocessor";
    case TokenKind::EndOfFile:    return "EndOfFile";
    }
    return "?";
}

inline std::ostream& operator<<(std::ostream& out, TokenKind kind) {
    return out << tokenKindName(kind);
}

}
