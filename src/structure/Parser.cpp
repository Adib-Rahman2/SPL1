#include "structure/Parser.h"

#include <set>
#include <string>

namespace codecomplex {

namespace {

bool isOpener(const std::string& text) { return text == "(" || text == "[" || text == "{"; }
bool isCloser(const std::string& text) { return text == ")" || text == "]" || text == "}"; }

int angleChange(const Token& token) {
    if (token.text == "<") return 1;
    if (token.text == ">") return -1;
    if (token.text == ">>") return -2;
    return 0;
}

std::string joinTexts(const std::vector<Token>& tokens, std::size_t begin, std::size_t end) {
    std::string out;
    for (std::size_t i = begin; i < end; ++i) {
        if (!out.empty()) out += ' ';
        out += tokens[i].text;
    }
    return out;
}

bool keywordStartsExpression(const std::string& word) {
    static const std::set<std::string> words = {
        "return", "throw", "else", "do", "case", "and", "or", "not", "xor",
        "bitand", "bitor", "compl", "not_eq", "and_eq", "or_eq", "xor_eq"};
    return words.count(word) > 0;
}

bool isDeclarationBefore(const std::vector<Token>& tokens, std::size_t start) {
    if (start == 0) return false;
    const Token& prev = tokens[start - 1];
    if (prev.kind == TokenKind::Identifier) return true;
    if (prev.kind == TokenKind::Keyword) return !keywordStartsExpression(prev.text);
    if (prev.text != ">" && prev.text != ">>") return false;

    int depth = 0;
    std::size_t i = start;
    while (i > 0) {
        --i;
        const std::string& text = tokens[i].text;
        if (text == ";" || text == "{" || text == "}") return false;
        depth -= angleChange(tokens[i]);
        if (depth == 0) break;
    }
    if (depth != 0 || tokens[i].text != "<") return false;
    while (i > 0) {
        const Token& t = tokens[i - 1];
        if (t.text == ";" || t.text == "{" || t.text == "}") return true;
        if (t.kind != TokenKind::Identifier && t.kind != TokenKind::Keyword && t.text != "::") {
            return false;
        }
        --i;
    }
    return true;
}

std::vector<std::vector<Token>> callArguments(const std::vector<Token>& tokens, std::size_t open) {
    std::vector<std::vector<Token>> args;
    std::vector<Token> current;
    int depth = 0;
    for (std::size_t i = open + 1; i < tokens.size(); ++i) {
        const std::string& text = tokens[i].text;
        if (depth == 0 && text == ")") break;
        if (depth == 0 && text == ",") {
            args.push_back(current);
            current.clear();
            continue;
        }
        if (isOpener(text)) ++depth;
        if (isCloser(text)) --depth;
        current.push_back(tokens[i]);
    }
    if (!current.empty() || !args.empty()) args.push_back(current);
    return args;
}

std::vector<Param> splitParams(const std::vector<Token>& tokens) {
    std::vector<Param> params;
    if (tokens.empty()) return params;
    if (tokens.size() == 1 && tokens[0].text == "void") return params;

    std::vector<std::vector<Token>> pieces(1);
    int depth = 0;
    int angles = 0;
    for (const Token& token : tokens) {
        if (depth == 0 && angles == 0 && token.text == ",") {
            pieces.emplace_back();
            continue;
        }
        if (isOpener(token.text)) ++depth;
        if (isCloser(token.text)) --depth;
        angles += angleChange(token);
        if (angles < 0) angles = 0;
        pieces.back().push_back(token);
    }

    for (const std::vector<Token>& piece : pieces) {
        std::size_t end = 0;
        while (end < piece.size() && piece[end].text != "=") ++end;

        std::size_t last = end;
        while (last > 0 && piece[last - 1].text == "]") {
            while (last > 0 && piece[last - 1].text != "[") --last;
            if (last > 0) --last;
        }

        Param param;
        const bool named = last >= 2 && piece[last - 1].kind == TokenKind::Identifier &&
                           piece[last - 2].text != "::";
        if (named) {
            param.name = piece[last - 1].text;
            std::vector<Token> type(piece.begin(), piece.begin() + (last - 1));
            type.insert(type.end(), piece.begin() + last, piece.begin() + end);
            param.type = joinTexts(type, 0, type.size());
        } else {
            param.type = joinTexts(piece, 0, end);
        }
        params.push_back(param);
    }
    return params;
}

bool containsText(const std::vector<Token>& tokens, const char* text) {
    for (const Token& token : tokens) {
        if (token.text == text) return true;
    }
    return false;
}

bool isJavaModifier(const std::string& word) {
    static const std::set<std::string> words = {
        "public", "private", "protected", "static", "final", "abstract",
        "synchronized", "native", "strictfp", "transient", "volatile"};
    return words.count(word) > 0;
}

bool startsLambda(const std::vector<Token>& before) {
    if (before.empty()) return true;
    const std::string& prev = before.back().text;
    return prev == "=" || prev == "(" || prev == "," || prev == "return";
}

bool looksLikeMacroStatement(const std::vector<Token>& tokens) {
    std::vector<std::size_t> opens;
    for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
        const std::string& text = tokens[i].text;
        if (text == "(") opens.push_back(i);
        if (text != ")" || opens.empty()) continue;
        const std::size_t open = opens.back();
        opens.pop_back();
        if (opens.empty() && open > 0 && tokens[open - 1].kind == TokenKind::Identifier &&
            tokens[i + 1].kind == TokenKind::Identifier) {
            return true;
        }
    }
    return false;
}

Node makeUnsupported(const std::string& reason, int line) {
    Node node;
    node.kind = NodeKind::Unsupported;
    node.reason = reason;
    node.line = line;
    return node;
}

}

std::vector<Call> findCalls(const std::vector<Token>& tokens) {
    std::vector<Call> calls;
    for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
        if (tokens[i].kind != TokenKind::Identifier || tokens[i + 1].text != "(") continue;

        std::string callee = tokens[i].text;
        std::size_t start = i;
        while (start >= 1) {
            const std::string& separator = tokens[start - 1].text;
            if (separator != "::" && separator != "." && separator != "->") break;
            if (start >= 2 && (tokens[start - 2].kind == TokenKind::Identifier ||
                               tokens[start - 2].text == "this")) {
                callee = tokens[start - 2].text + separator + callee;
                start -= 2;
            } else {
                callee = separator + callee;
                start -= 1;
                break;
            }
        }
        if (isDeclarationBefore(tokens, start)) continue;

        Call call;
        call.callee = callee;
        call.args = callArguments(tokens, i + 1);
        call.line = tokens[i].line;
        calls.push_back(call);
    }
    return calls;
}

Parser::Parser(const std::vector<Token>& tokens, Language language)
    : tokens_(tokens), language_(language) {}

const Token& Parser::peek(std::size_t offset) const {
    const std::size_t index = pos_ + offset;
    return index < tokens_.size() ? tokens_[index] : tokens_.back();
}

Token Parser::advance() {
    const Token token = peek();
    if (!atEnd()) ++pos_;
    return token;
}

bool Parser::check(const char* text, std::size_t offset) const {
    return peek(offset).text == text;
}

bool Parser::match(const char* text) {
    if (!check(text)) return false;
    advance();
    return true;
}

void Parser::addUnsupported(const std::string& reason, int line) {
    result_.unsupported.push_back(makeUnsupported(reason, line));
}

ParseResult Parser::run() {
    if (tokens_.empty()) return result_;
    while (!atEnd()) {
        parseScope("");
        if (check("}")) {
            addUnsupported("unexpected '}' outside any block", peek().line);
            advance();
        }
    }
    return result_;
}

void Parser::parseScope(const std::string& className) {
    while (!atEnd() && !check("}")) {
        if (peek().kind == TokenKind::Preprocessor || check(";")) {
            advance();
        } else if (check("namespace")) {
            const int line = advance().line;
            while (peek().kind == TokenKind::Identifier || check("::") || check("inline")) advance();
            if (match("{")) {
                parseScope("");
                if (!match("}")) addUnsupported("missing '}' for the namespace opened here", line);
            } else {
                skipDeclaration();
            }
        } else if (check("template")) {
            const int line = advance().line;
            if (check("<")) {
                int depth = 0;
                do {
                    depth += angleChange(advance());
                } while (!atEnd() && depth > 0);
            }
            skipDeclaration();
            addUnsupported("templates are not supported; this declaration was not analysed", line);
        } else if (check("enum")) {
            skipDeclaration();
        } else if (java() && isJavaModifier(peek().text)) {
            advance();
        } else if (check("class") || check("struct") || check("union") ||
                   (java() && check("interface"))) {
            parseClass();
        } else if ((check("public") || check("private") || check("protected")) && check(":", 1)) {
            advance();
            advance();
        } else {
            parseDeclaration(className);
        }
    }
}

void Parser::parseClass() {
    const int line = advance().line;
    std::string name;
    while (!atEnd() && !check("{") && !check(";") && !check("}")) {
        if (name.empty() && peek().kind == TokenKind::Identifier) name = peek().text;
        if (check("(")) {
            skipBalanced();
        } else {
            advance();
        }
    }
    if (match(";") || !check("{")) return;

    advance();
    parseScope(name);
    if (!match("}")) {
        addUnsupported("missing '}' for the class opened here", line);
        return;
    }
    if (java()) return;
    skipDeclaration();
}

void Parser::parseDeclaration(const std::string& className) {
    std::size_t j = pos_;
    std::size_t nameAt = 0;
    std::string name;
    while (true) {
        const Token& t = tokens_[j];
        if (t.kind == TokenKind::EndOfFile || t.text == ";" || t.text == "{" || t.text == "}" ||
            t.text == "=") {
            if (t.text == "=" && tokens_[j + 1].text == "[") {
                addUnsupported("lambda expressions are not supported", t.line);
            }
            skipDeclaration();
            return;
        }
        if (t.text == "operator") {
            nameAt = j;
            name = "operator";
            ++j;
            if (tokens_[j].text == "(" && tokens_[j + 1].text == ")") {
                name += "()";
                j += 2;
            }
            while (tokens_[j].kind != TokenKind::EndOfFile && tokens_[j].text != "(" &&
                   tokens_[j].text != ";" && tokens_[j].text != "{") {
                if (tokens_[j].kind == TokenKind::Identifier || tokens_[j].kind == TokenKind::Keyword) {
                    name += ' ';
                }
                name += tokens_[j].text;
                ++j;
            }
            if (tokens_[j].text != "(") {
                skipDeclaration();
                return;
            }
            break;
        }
        if (t.text == "(" && j > pos_ && tokens_[j - 1].kind == TokenKind::Identifier) {
            nameAt = j - 1;
            name = tokens_[nameAt].text;
            break;
        }
        if (t.text == "(" || t.text == "[") {
            int depth = 0;
            do {
                if (isOpener(tokens_[j].text)) ++depth;
                if (isCloser(tokens_[j].text)) --depth;
                ++j;
            } while (tokens_[j].kind != TokenKind::EndOfFile && depth > 0);
            continue;
        }
        ++j;
    }

    Function function;
    function.line = tokens_[nameAt].line;
    function.className = className;

    std::size_t first = nameAt;
    if (first > pos_ && tokens_[first - 1].text == "~") {
        name = "~" + name;
        --first;
    }
    std::string qualifier;
    while (first >= pos_ + 2 && tokens_[first - 1].text == "::" &&
           tokens_[first - 2].kind == TokenKind::Identifier) {
        qualifier = qualifier.empty() ? tokens_[first - 2].text
                                      : tokens_[first - 2].text + "::" + qualifier;
        first -= 2;
    }
    if (!qualifier.empty()) function.className = qualifier;
    function.name = name;

    while (pos_ < j) advance();
    std::vector<Token> paramTokens;
    if (!parenthesized(paramTokens)) {
        addUnsupported("the parameter list of '" + name + "' is never closed", function.line);
        return;
    }
    function.params = splitParams(paramTokens);

    if (!parseFunctionTail()) return;
    function.body = parseBlock();
    function.endLine = tokens_[pos_ - 1].line;
    result_.functions.push_back(function);
}

bool Parser::parseFunctionTail() {
    while (!atEnd() && !check("{")) {
        if (java() && match("throws")) {
            while (peek().kind == TokenKind::Identifier || check(".") || check(",")) advance();
            continue;
        }
        if (check(";") || check("=") || check("}") || check(",")) {
            skipDeclaration();
            return false;
        }
        if (match(":")) {
            while (!atEnd()) {
                while (!atEnd() && !check("(") && !check("{") && !check(";")) advance();
                if (!check("(") && !check("{")) break;
                skipBalanced();
                if (!match(",")) break;
            }
        } else if (check("(")) {
            skipBalanced();
        } else {
            advance();
        }
    }
    return check("{");
}

void Parser::skipDeclaration() {
    bool expectSemicolon = false;
    while (!atEnd()) {
        if (match(";")) return;
        if (check("}")) return;
        if (check("{")) {
            skipBalanced();
            if (!expectSemicolon) return;
            continue;
        }
        if (check("(") || check("[")) {
            skipBalanced();
            continue;
        }
        if (check("=") ||
            (!java() && (check("class") || check("struct") || check("enum") || check("union")))) {
            expectSemicolon = true;
        }
        advance();
    }
}

void Parser::skipBalanced() {
    if (!isOpener(peek().text)) {
        advance();
        return;
    }
    int depth = 0;
    do {
        const Token token = advance();
        if (isOpener(token.text)) ++depth;
        if (isCloser(token.text)) --depth;
    } while (!atEnd() && depth > 0);
}

bool Parser::parenthesized(std::vector<Token>& inside) {
    if (!check("(")) return false;
    advance();
    int depth = 0;
    while (!atEnd()) {
        if (check("(")) ++depth;
        if (check(")")) {
            if (depth == 0) {
                advance();
                return true;
            }
            --depth;
        }
        inside.push_back(advance());
    }
    return false;
}

Node Parser::parseBlock() {
    Node block;
    block.kind = NodeKind::Block;
    block.line = peek().line;
    advance();
    while (true) {
        if (atEnd()) {
            block.children.push_back(makeUnsupported(
                "missing '}' for the block opened at line " + std::to_string(block.line), block.line));
            break;
        }
        if (match("}")) break;
        block.children.push_back(parseStatement());
    }
    return block;
}

Node Parser::parseStatement() {
    const int line = peek().line;
    if (check("{")) return parseBlock();
    if (check(";")) {
        advance();
        Node empty;
        empty.line = line;
        return empty;
    }
    if (check("for")) return parseFor();
    if (check("while")) return parseWhile();
    if (check("do")) return parseDoWhile();
    if (check("if")) return parseIf();
    if (check("switch")) return parseSwitch();
    if (check("case") || check("default")) return parseCaseLabel();
    if (check("else")) {
        advance();
        return makeUnsupported("'else' without a matching 'if'", line);
    }
    if (check("try")) {
        advance();
        if (java() && check("(")) skipBalanced();
        if (!check("{")) return makeUnsupported("malformed 'try' block", line);
        Node block;
        block.kind = NodeKind::Block;
        block.line = line;
        block.children.push_back(parseBlock());
        while (match("catch")) {
            if (check("(")) skipBalanced();
            if (check("{")) block.children.push_back(parseBlock());
        }
        if (java() && match("finally") && check("{")) block.children.push_back(parseBlock());
        return block;
    }
    if (check("struct") || check("class") || check("union") || check("enum")) {
        skipDeclaration();
        return makeUnsupported("a type defined inside a function is not supported", line);
    }
    return parseSimpleStatement();
}

Node Parser::parseFor() {
    Node loop;
    loop.kind = NodeKind::Loop;
    loop.line = advance().line;
    std::vector<Token> header;
    if (!parenthesized(header)) return makeUnsupported("malformed 'for' header", loop.line);

    std::vector<std::size_t> semicolons;
    std::size_t colon = header.size();
    int depth = 0;
    int pendingQuestions = 0;
    for (std::size_t i = 0; i < header.size(); ++i) {
        const std::string& text = header[i].text;
        if (isOpener(text)) ++depth;
        if (isCloser(text)) --depth;
        if (depth != 0) continue;
        if (text == ";") semicolons.push_back(i);
        if (text == "?") ++pendingQuestions;
        if (text == ":") {
            if (pendingQuestions > 0) {
                --pendingQuestions;
            } else if (colon == header.size()) {
                colon = i;
            }
        }
    }

    if (semicolons.size() == 2) {
        loop.loopKind = LoopKind::For;
        loop.init.assign(header.begin(), header.begin() + semicolons[0]);
        loop.cond.assign(header.begin() + semicolons[0] + 1, header.begin() + semicolons[1]);
        loop.step.assign(header.begin() + semicolons[1] + 1, header.end());
    } else if (semicolons.empty() && colon < header.size()) {
        loop.loopKind = LoopKind::RangeFor;
        loop.init.assign(header.begin(), header.begin() + colon);
        loop.cond.assign(header.begin() + colon + 1, header.end());
    } else {
        parseStatement();
        Node bad = makeUnsupported("unrecognised 'for' header", loop.line);
        bad.tokens = header;
        return bad;
    }
    loop.calls = findCalls(header);
    loop.children.push_back(parseStatement());
    return loop;
}

Node Parser::parseWhile() {
    Node loop;
    loop.kind = NodeKind::Loop;
    loop.loopKind = LoopKind::While;
    loop.line = advance().line;
    if (!parenthesized(loop.cond)) return makeUnsupported("malformed 'while' condition", loop.line);
    loop.calls = findCalls(loop.cond);
    loop.children.push_back(parseStatement());
    return loop;
}

Node Parser::parseDoWhile() {
    Node loop;
    loop.kind = NodeKind::Loop;
    loop.loopKind = LoopKind::DoWhile;
    loop.line = advance().line;
    loop.children.push_back(parseStatement());
    if (!match("while") || !parenthesized(loop.cond)) {
        return makeUnsupported("'do' body not followed by 'while (...)'", loop.line);
    }
    match(";");
    loop.calls = findCalls(loop.cond);
    return loop;
}

Node Parser::parseIf() {
    Node node;
    node.kind = NodeKind::If;
    node.line = peek().line;
    while (true) {
        Node branch;
        branch.kind = NodeKind::Branch;
        branch.line = advance().line;
        match("constexpr");
        if (!parenthesized(branch.cond)) return makeUnsupported("malformed 'if' condition", branch.line);
        branch.calls = findCalls(branch.cond);
        branch.children.push_back(parseStatement());
        node.children.push_back(branch);

        if (!check("else")) break;
        const int elseLine = advance().line;
        if (check("if")) continue;

        Node last;
        last.kind = NodeKind::Branch;
        last.line = elseLine;
        last.children.push_back(parseStatement());
        node.children.push_back(last);
        break;
    }
    return node;
}

Node Parser::parseSwitch() {
    Node node;
    node.kind = NodeKind::Switch;
    node.line = advance().line;
    if (!parenthesized(node.cond)) return makeUnsupported("malformed 'switch' expression", node.line);
    node.calls = findCalls(node.cond);
    node.children.push_back(parseStatement());
    return node;
}

Node Parser::parseCaseLabel() {
    Node label;
    label.line = peek().line;
    label.tokens.push_back(advance());
    while (!atEnd() && !check("}") && !check(";") && !check("{")) {
        const bool colon = check(":");
        label.tokens.push_back(advance());
        if (colon) return label;
    }
    return makeUnsupported("'case' label without ':'", label.line);
}

Node Parser::parseSimpleStatement() {
    Node statement;
    statement.line = peek().line;
    int braceDepth = 0;
    int parenDepth = 0;
    bool lambda = false;
    bool anonymousClass = false;
    bool terminated = false;
    while (!atEnd()) {
        const std::string& text = peek().text;
        if (braceDepth == 0 && text == ";") {
            advance();
            terminated = true;
            break;
        }
        if (braceDepth == 0 && text == "}") break;
        if (text == "{" && braceDepth == 0 && parenDepth <= 1 && java() && !lambda &&
            !statement.tokens.empty() && statement.tokens.back().text == ")" &&
            containsText(statement.tokens, "new")) {
            anonymousClass = true;
        }
        if (text == "->" && java()) lambda = true;
        if (text == "{" && braceDepth == 0 && parenDepth == 0 && !lambda && !anonymousClass &&
            !statement.tokens.empty() && statement.tokens.back().text == ")") {
            skipBalanced();
            Node macro = makeUnsupported(
                "'" + statement.tokens[0].text + "(...)' looks like a macro used as code; it was not analysed",
                statement.line);
            macro.tokens = statement.tokens;
            return macro;
        }
        if (text == "[" && startsLambda(statement.tokens)) lambda = true;
        if (text == "(" || text == "[") ++parenDepth;
        if ((text == ")" || text == "]") && parenDepth > 0) --parenDepth;
        if (text == "{") ++braceDepth;
        if (text == "}") --braceDepth;
        statement.tokens.push_back(advance());
    }

    std::string reason;
    if (lambda) {
        reason = "lambda expressions are not supported";
    } else if (anonymousClass) {
        reason = "anonymous classes are not supported";
    } else if (looksLikeMacroStatement(statement.tokens)) {
        reason = "'" + statement.tokens[0].text + "(...)' looks like a macro used as code; it was not analysed";
    } else if (!terminated) {
        reason = "statement is not terminated by ';'";
    }
    if (!reason.empty()) {
        Node bad = makeUnsupported(reason, statement.line);
        bad.tokens = statement.tokens;
        return bad;
    }
    statement.calls = findCalls(statement.tokens);
    return statement;
}

ParseResult parse(const std::vector<Token>& tokens, Language language) {
    return Parser(tokens, language).run();
}

}
