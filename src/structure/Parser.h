#pragma once

#include "lexer/Token.h"
#include "source/Language.h"
#include "structure/Ast.h"

#include <cstddef>
#include <string>
#include <vector>

namespace codecomplex {

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens, Language language = Language::Cpp);

    ParseResult run();

private:
    const Token& peek(std::size_t offset = 0) const;
    Token advance();
    bool atEnd() const { return peek().kind == TokenKind::EndOfFile; }
    bool check(const char* text, std::size_t offset = 0) const;
    bool match(const char* text);

    void parseScope(const std::string& className);
    void parseClass();
    void parseDeclaration(const std::string& className);
    bool parseFunctionTail();
    void skipDeclaration();
    void skipBalanced();

    Node parseBlock();
    Node parseStatement();
    Node parseFor();
    Node parseWhile();
    Node parseDoWhile();
    Node parseIf();
    Node parseSwitch();
    Node parseCaseLabel();
    Node parseSimpleStatement();

    bool parenthesized(std::vector<Token>& inside);

    void addUnsupported(const std::string& reason, int line);

    bool java() const { return language_ == Language::Java; }

    const std::vector<Token>& tokens_;
    Language language_;
    std::size_t pos_ = 0;
    ParseResult result_;
};

ParseResult parse(const std::vector<Token>& tokens, Language language = Language::Cpp);

std::vector<Call> findCalls(const std::vector<Token>& tokens);

}
