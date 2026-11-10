#include "TestFramework.h"

#include "lexer/Lexer.h"

#include <cstddef>
#include <string>

using codecomplex::LexResult;
using codecomplex::Token;
using codecomplex::TokenKind;
using codecomplex::tokenize;

namespace {

std::string joinedTexts(const LexResult& result, const std::string& separator = "|") {
    std::string out;
    for (const Token& token : result.tokens) {
        if (token.kind == TokenKind::EndOfFile) break;
        if (!out.empty()) out += separator;
        out += token.text;
    }
    return out;
}

bool hasKeyword(const LexResult& result, const std::string& word) {
    for (const Token& token : result.tokens) {
        if (token.kind == TokenKind::Keyword && token.text == word) return true;
    }
    return false;
}

Token onlyToken(const std::string& source) {
    const LexResult result = tokenize(source);
    REQUIRE(result.ok());
    REQUIRE(result.tokens.size() == 2);
    return result.tokens[0];
}

}

TEST_CASE("lexer: empty input gives only EndOfFile") {
    const LexResult result = tokenize("");
    CHECK(result.ok());
    REQUIRE(result.tokens.size() == 1);
    CHECK_EQ(result.tokens[0].kind, TokenKind::EndOfFile);
}

TEST_CASE("lexer: whitespace and comments only give only EndOfFile") {
    const LexResult result = tokenize("  \n\t// comment\n/* block */\n");
    CHECK(result.ok());
    CHECK_EQ(result.tokens.size(), std::size_t{1});
}

TEST_CASE("lexer: simple statement kinds") {
    const LexResult result = tokenize("int count = 10;");
    REQUIRE(result.ok());
    REQUIRE(result.tokens.size() == 6);
    CHECK_EQ(result.tokens[0].kind, TokenKind::Keyword);
    CHECK_EQ(result.tokens[1].kind, TokenKind::Identifier);
    CHECK_EQ(result.tokens[2].kind, TokenKind::Operator);
    CHECK_EQ(result.tokens[3].kind, TokenKind::Number);
    CHECK_EQ(result.tokens[4].kind, TokenKind::Punctuation);
    CHECK_EQ(result.tokens[5].kind, TokenKind::EndOfFile);
    CHECK_EQ(joinedTexts(result), std::string("int|count|=|10|;"));
}

TEST_CASE("lexer: keywords versus identifiers") {
    const LexResult result = tokenize("for while return std vector forx size_t");
    REQUIRE(result.ok());
    CHECK_EQ(result.tokens[0].kind, TokenKind::Keyword);
    CHECK_EQ(result.tokens[1].kind, TokenKind::Keyword);
    CHECK_EQ(result.tokens[2].kind, TokenKind::Keyword);
    CHECK_EQ(result.tokens[3].kind, TokenKind::Identifier);
    CHECK_EQ(result.tokens[4].kind, TokenKind::Identifier);
    CHECK_EQ(result.tokens[5].kind, TokenKind::Identifier);
    CHECK_EQ(result.tokens[6].kind, TokenKind::Identifier);
}

TEST_CASE("lexer: line and column numbers") {
    const LexResult result = tokenize("int a;\n  for (i)\n\tx");
    REQUIRE(result.ok());
    REQUIRE(result.tokens.size() == 9);
    const Token& a = result.tokens[1];
    CHECK_EQ(a.line, 1);
    CHECK_EQ(a.column, 5);
    const Token& forKeyword = result.tokens[3];
    CHECK_EQ(forKeyword.text, std::string("for"));
    CHECK_EQ(forKeyword.line, 2);
    CHECK_EQ(forKeyword.column, 3);
    const Token& paren = result.tokens[4];
    CHECK_EQ(paren.line, 2);
    CHECK_EQ(paren.column, 7);
    const Token& x = result.tokens[7];
    CHECK_EQ(x.line, 3);
    CHECK_EQ(x.column, 2);
}

TEST_CASE("lexer: Windows line endings count lines correctly") {
    const LexResult result = tokenize("a\r\nb\r\n\r\nc");
    REQUIRE(result.ok());
    REQUIRE(result.tokens.size() == 4);
    CHECK_EQ(result.tokens[1].line, 2);
    CHECK_EQ(result.tokens[2].line, 4);
    CHECK_EQ(result.tokens[2].column, 1);
}

TEST_CASE("lexer: 'for' inside comments is not a keyword") {
    const LexResult result = tokenize("// for (int i = 0; i < n; ++i)\n/* while for */ x");
    REQUIRE(result.ok());
    CHECK(!hasKeyword(result, "for"));
    CHECK(!hasKeyword(result, "while"));
    CHECK_EQ(joinedTexts(result), std::string("x"));
}

TEST_CASE("lexer: multi-line block comment keeps line numbers right") {
    const LexResult result = tokenize("a /* one\ntwo\nthree */ b\nc");
    REQUIRE(result.ok());
    CHECK_EQ(joinedTexts(result), std::string("a|b|c"));
    CHECK_EQ(result.tokens[1].line, 3);
    CHECK_EQ(result.tokens[1].column, 10);
    CHECK_EQ(result.tokens[2].line, 4);
}

TEST_CASE("lexer: line comment continued with a backslash") {
    const LexResult result = tokenize("a // comment \\\nstill comment\nb");
    REQUIRE(result.ok());
    CHECK_EQ(joinedTexts(result), std::string("a|b"));
    CHECK_EQ(result.tokens[1].line, 3);
}

TEST_CASE("lexer: comment markers inside a string are not comments") {
    const LexResult result = tokenize("s = \"// not /* a comment\";");
    REQUIRE(result.ok());
    CHECK_EQ(joinedTexts(result), std::string("s|=|\"// not /* a comment\"|;"));
}

TEST_CASE("lexer: 'for' inside strings and chars is not a keyword") {
    const LexResult result = tokenize("f(\"for (;;)\", 'f');");
    REQUIRE(result.ok());
    CHECK(!hasKeyword(result, "for"));
    CHECK_EQ(result.tokens[2].kind, TokenKind::String);
    CHECK_EQ(result.tokens[4].kind, TokenKind::Char);
}

TEST_CASE("lexer: string with escapes is one token") {
    const Token token = onlyToken(R"("a\"b\\c\n")");
    CHECK_EQ(token.kind, TokenKind::String);
    CHECK_EQ(token.text, std::string(R"("a\"b\\c\n")"));
}

TEST_CASE("lexer: string ending in an escaped backslash") {
    const LexResult result = tokenize(R"(x = "\\"; y)");
    REQUIRE(result.ok());
    CHECK_EQ(joinedTexts(result), std::string(R"(x|=|"\\"|;|y)"));
}

TEST_CASE("lexer: string literal prefixes and suffixes") {
    const LexResult result = tokenize("u8\"a\" L\"b\" u\"c\" U\"d\" \"e\"s");
    REQUIRE(result.ok());
    CHECK_EQ(joinedTexts(result), std::string("u8\"a\"|L\"b\"|u\"c\"|U\"d\"|\"e\"s"));
    for (std::size_t i = 0; i + 1 < result.tokens.size(); ++i) {
        CHECK_EQ(result.tokens[i].kind, TokenKind::String);
    }
}

TEST_CASE("lexer: char literals") {
    const LexResult result = tokenize(R"('a' '\'' '\n' '\\' L'x' u'y')");
    REQUIRE(result.ok());
    REQUIRE(result.tokens.size() == 7);
    CHECK_EQ(joinedTexts(result), std::string(R"('a'|'\''|'\n'|'\\'|L'x'|u'y')"));
    for (std::size_t i = 0; i < 6; ++i) {
        CHECK_EQ(result.tokens[i].kind, TokenKind::Char);
    }
}

TEST_CASE("lexer: raw string without delimiter") {
    const Token token = onlyToken(R"__(R"(a "for" \n b)")__");
    CHECK_EQ(token.kind, TokenKind::String);
    CHECK_EQ(token.text, std::string(R"__(R"(a "for" \n b)")__"));
}

TEST_CASE("lexer: raw string with a delimiter containing )\"") {
    const Token token = onlyToken(R"__(R"xy(a )" b)xy")__");
    CHECK_EQ(token.kind, TokenKind::String);
    CHECK_EQ(token.text, std::string(R"__(R"xy(a )" b)xy")__"));
}

TEST_CASE("lexer: multi-line raw string and the lines after it") {
    const LexResult result = tokenize("s = u8R\"(line1\nfor\nline3)\";\nnext");
    REQUIRE(result.ok());
    CHECK(!hasKeyword(result, "for"));
    REQUIRE(result.tokens.size() == 6);
    CHECK_EQ(result.tokens[2].kind, TokenKind::String);
    CHECK_EQ(result.tokens[2].line, 1);
    CHECK_EQ(result.tokens[3].text, std::string(";"));
    CHECK_EQ(result.tokens[3].line, 3);
    CHECK_EQ(result.tokens[4].text, std::string("next"));
    CHECK_EQ(result.tokens[4].line, 4);
}

TEST_CASE("lexer: R alone is an identifier") {
    const LexResult result = tokenize("R + LR");
    REQUIRE(result.ok());
    CHECK_EQ(result.tokens[0].kind, TokenKind::Identifier);
    CHECK_EQ(result.tokens[2].kind, TokenKind::Identifier);
}

TEST_CASE("lexer: include directive is one token") {
    const LexResult result = tokenize("#include <vector>\nint x;");
    REQUIRE(result.ok());
    REQUIRE(result.tokens.size() == 5);
    CHECK_EQ(result.tokens[0].kind, TokenKind::Preprocessor);
    CHECK_EQ(result.tokens[0].text, std::string("#include <vector>"));
    CHECK_EQ(result.tokens[1].text, std::string("int"));
    CHECK_EQ(result.tokens[1].line, 2);
}

TEST_CASE("lexer: directive with line continuation") {
    const LexResult result = tokenize("#define MAX(a, b) \\\n    ((a) > (b) ? (a) : (b))\nfor");
    REQUIRE(result.ok());
    REQUIRE(result.tokens.size() == 3);
    CHECK_EQ(result.tokens[0].kind, TokenKind::Preprocessor);
    CHECK_EQ(result.tokens[0].text,
             std::string("#define MAX(a, b)     ((a) > (b) ? (a) : (b))"));
    CHECK_EQ(result.tokens[1].text, std::string("for"));
    CHECK_EQ(result.tokens[1].line, 3);
}

TEST_CASE("lexer: comments are removed from directives, strings are kept") {
    const LexResult result = tokenize(
        "#include \"a//b.h\" // trailing\n"
        "#define N /* ten */ 10\n"
        "#if 10'000 > N\n");
    REQUIRE(result.ok());
    REQUIRE(result.tokens.size() == 4);
    CHECK_EQ(result.tokens[0].text, std::string("#include \"a//b.h\""));
    CHECK_EQ(result.tokens[1].text, std::string("#define N   10"));
    CHECK_EQ(result.tokens[2].text, std::string("#if 10'000 > N"));
}

TEST_CASE("lexer: indented directive and directive with a lone quote") {
    const LexResult result = tokenize("  #pragma once\n#error don't do this\nx");
    REQUIRE(result.ok());
    REQUIRE(result.tokens.size() == 4);
    CHECK_EQ(result.tokens[0].text, std::string("#pragma once"));
    CHECK_EQ(result.tokens[0].column, 3);
    CHECK_EQ(result.tokens[1].text, std::string("#error don't do this"));
    CHECK_EQ(result.tokens[2].text, std::string("x"));
    CHECK_EQ(result.tokens[2].line, 3);
}

TEST_CASE("lexer: # in the middle of a line is an operator") {
    const LexResult result = tokenize("a # b");
    REQUIRE(result.ok());
    CHECK_EQ(result.tokens[1].kind, TokenKind::Operator);
}

TEST_CASE("lexer: number forms") {
    const LexResult result = tokenize(
        "42 0x1F 0b1010 1e9 1.5e-3 1E+5 .5 3.14f 10'000 10ULL 0x1p-3 077 1'000'000u");
    REQUIRE(result.ok());
    CHECK_EQ(joinedTexts(result),
             std::string("42|0x1F|0b1010|1e9|1.5e-3|1E+5|.5|3.14f|10'000|10ULL|0x1p-3|077|1'000'000u"));
    for (std::size_t i = 0; i + 1 < result.tokens.size(); ++i) {
        CHECK_EQ(result.tokens[i].kind, TokenKind::Number);
    }
}

TEST_CASE("lexer: minus after a number is a separate operator") {
    CHECK_EQ(joinedTexts(tokenize("n-1")), std::string("n|-|1"));
    CHECK_EQ(joinedTexts(tokenize("0x1E-1")), std::string("0x1E|-|1"));
    CHECK_EQ(joinedTexts(tokenize("i<n/2")), std::string("i|<|n|/|2"));
}

TEST_CASE("lexer: number followed by a char literal") {
    const LexResult result = tokenize("f(1,'a')");
    REQUIRE(result.ok());
    CHECK_EQ(joinedTexts(result), std::string("f|(|1|,|'a'|)"));
}

TEST_CASE("lexer: multi-character operators") {
    const LexResult result = tokenize(
        "<<= >>= <=> -> :: ++ -- << >> <= >= == != && || += -= *= /= %= &= |= ^= ... ->* .*");
    REQUIRE(result.ok());
    CHECK_EQ(joinedTexts(result, " "),
             std::string("<<= >>= <=> -> :: ++ -- << >> <= >= == != && || "
                         "+= -= *= /= %= &= |= ^= ... ->* .*"));
    for (std::size_t i = 0; i + 1 < result.tokens.size(); ++i) {
        CHECK_EQ(result.tokens[i].kind, TokenKind::Operator);
    }
}

TEST_CASE("lexer: longest match splits a+++b and i<<=1") {
    CHECK_EQ(joinedTexts(tokenize("a+++b")), std::string("a|++|+|b"));
    CHECK_EQ(joinedTexts(tokenize("i<<=1")), std::string("i|<<=|1"));
    CHECK_EQ(joinedTexts(tokenize("std::cout<<x")), std::string("std|::|cout|<<|x"));
    CHECK_EQ(joinedTexts(tokenize("p->next")), std::string("p|->|next"));
}

TEST_CASE("lexer: punctuation") {
    const LexResult result = tokenize("(){}[];,");
    REQUIRE(result.ok());
    REQUIRE(result.tokens.size() == 9);
    for (std::size_t i = 0; i < 8; ++i) {
        CHECK_EQ(result.tokens[i].kind, TokenKind::Punctuation);
    }
}

TEST_CASE("lexer: a for loop header") {
    const LexResult result = tokenize("for (int i = 0; i < n; i *= 2) {}");
    REQUIRE(result.ok());
    CHECK_EQ(joinedTexts(result), std::string("for|(|int|i|=|0|;|i|<|n|;|i|*=|2|)|{|}"));
}

TEST_CASE("lexer: unterminated block comment reports its start line") {
    const LexResult result = tokenize("int a;\n  /* never\nclosed");
    REQUIRE(result.errors.size() == 1);
    CHECK_EQ(result.errors[0].message, std::string("unterminated block comment"));
    CHECK_EQ(result.errors[0].line, 2);
    CHECK_EQ(result.errors[0].column, 3);
    CHECK_EQ(joinedTexts(result), std::string("int|a|;"));
    CHECK_EQ(result.tokens.back().kind, TokenKind::EndOfFile);
}

TEST_CASE("lexer: unterminated string reports its line and lexing continues") {
    const LexResult result = tokenize("a;\ns = \"oops;\nb;");
    REQUIRE(result.errors.size() == 1);
    CHECK_EQ(result.errors[0].message, std::string("unterminated string literal"));
    CHECK_EQ(result.errors[0].line, 2);
    CHECK_EQ(result.errors[0].column, 5);
    CHECK_EQ(joinedTexts(result), std::string("a|;|s|=|b|;"));
    CHECK_EQ(result.tokens[4].line, 3);
}

TEST_CASE("lexer: unterminated string at end of input") {
    const LexResult result = tokenize("x = \"abc\\");
    REQUIRE(result.errors.size() == 1);
    CHECK_EQ(result.errors[0].message, std::string("unterminated string literal"));
    CHECK_EQ(result.errors[0].line, 1);
}

TEST_CASE("lexer: unterminated char literal") {
    const LexResult result = tokenize("c = 'a;\n");
    REQUIRE(result.errors.size() == 1);
    CHECK_EQ(result.errors[0].message, std::string("unterminated character literal"));
    CHECK_EQ(result.errors[0].line, 1);
}

TEST_CASE("lexer: unterminated raw string reports its start line") {
    const LexResult result = tokenize("a\nR\"x(never closed )\"\n");
    REQUIRE(result.errors.size() == 1);
    CHECK_EQ(result.errors[0].message, std::string("unterminated raw string literal"));
    CHECK_EQ(result.errors[0].line, 2);
}

TEST_CASE("lexer: invalid raw string delimiter") {
    const LexResult result = tokenize("R\"a b(x)a b\"");
    REQUIRE(!result.errors.empty());
    CHECK_EQ(result.errors[0].message, std::string("invalid raw string delimiter"));
}

TEST_CASE("lexer: unexpected character is reported and skipped") {
    const LexResult result = tokenize("a @ b\n$");
    REQUIRE(result.errors.size() == 2);
    CHECK_EQ(result.errors[0].message, std::string("unexpected character '@'"));
    CHECK_EQ(result.errors[0].line, 1);
    CHECK_EQ(result.errors[0].column, 3);
    CHECK_EQ(result.errors[1].line, 2);
    CHECK_EQ(joinedTexts(result), std::string("a|b"));
}

TEST_CASE("lexer: Java annotations are dropped, C++ still rejects '@'") {
    using codecomplex::Language;
    const codecomplex::LexResult java = codecomplex::tokenize(
        "@Override\npublic int f() { return 1; }\n@SuppressWarnings(\"a)b\") int g;", Language::Java);
    REQUIRE(java.ok());
    CHECK_EQ(java.tokens[0].text, std::string("public"));
    CHECK_EQ(java.tokens[0].line, 2);
    bool sawG = false;
    for (const codecomplex::Token& token : java.tokens) {
        CHECK(token.text.find("Suppress") == std::string::npos);
        if (token.text == "g") sawG = true;
    }
    CHECK(sawG);

    CHECK(!codecomplex::tokenize("@Override int f();").ok());
}

TEST_CASE("lexer: Java >>> and >>>= are one operator") {
    const codecomplex::LexResult java =
        codecomplex::tokenize("x >>>= 1; y = x >>> 2;", codecomplex::Language::Java);
    REQUIRE(java.ok());
    CHECK_EQ(java.tokens[1].text, std::string(">>>="));
    CHECK_EQ(java.tokens[7].text, std::string(">>>"));
    const codecomplex::LexResult cpp = codecomplex::tokenize("x >>> 2");
    CHECK_EQ(cpp.tokens[1].text, std::string(">>"));
}
