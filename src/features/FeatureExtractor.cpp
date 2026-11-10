#include "features/FeatureExtractor.h"

#include "analysis/LibraryCosts.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <set>

namespace codecomplex {

const char* stepKindName(StepKind kind) {
    switch (kind) {
    case StepKind::Additive:       return "Additive";
    case StepKind::Multiplicative: return "Multiplicative";
    case StepKind::Other:          return "Other";
    }
    return "?";
}

const char* boundKindName(BoundKind kind) {
    switch (kind) {
    case BoundKind::Constant: return "Constant";
    case BoundKind::Input:    return "Input";
    case BoundKind::Variable: return "Variable";
    case BoundKind::Unknown:  return "Unknown";
    }
    return "?";
}

namespace {

using Tokens = std::vector<Token>;

bool isName(const Token& token) { return token.kind == TokenKind::Identifier; }

bool isIncDec(const Token& token) { return token.text == "++" || token.text == "--"; }

bool isAssignOp(const std::string& text) {
    static const char* const ops[] = {"=", "+=", "-=", "*=", "/=", "%=", "<<=", ">>=",
                                      "&=", "|=", "^="};
    for (const char* op : ops) {
        if (text == op) return true;
    }
    return false;
}

bool isComparison(const std::string& text) {
    return text == "<" || text == "<=" || text == ">" || text == ">=" || text == "!=";
}

bool numberValue(const Token& token, long long& value) {
    if (token.kind != TokenKind::Number) return false;
    const std::string& text = token.text;
    std::size_t i = 0;
    value = 0;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
        value = value * 10 + (text[i] - '0');
        if (value > 1000000000) return false;
        ++i;
    }
    if (i == 0 || (text[0] == '0' && i > 1)) return false;
    for (; i < text.size(); ++i) {
        const char c = text[i];
        if (c != 'u' && c != 'U' && c != 'l' && c != 'L') return false;
    }
    return true;
}

bool mentions(const Tokens& tokens, const std::string& name) {
    for (const Token& token : tokens) {
        if (isName(token) && token.text == name) return true;
    }
    return false;
}

bool isSingleName(const Tokens& tokens) { return tokens.size() == 1 && isName(tokens[0]); }

std::vector<Tokens> splitTopLevel(const Tokens& tokens, const char* separator) {
    std::vector<Tokens> parts(1);
    int depth = 0;
    for (const Token& token : tokens) {
        const std::string& t = token.text;
        if (t == "(" || t == "[" || t == "{") ++depth;
        if (t == ")" || t == "]" || t == "}") --depth;
        if (depth == 0 && t == separator) {
            parts.emplace_back();
            continue;
        }
        parts.back().push_back(token);
    }
    return parts;
}

bool splitComparison(const Tokens& part, Tokens& left, Tokens& right) {
    int depth = 0;
    for (std::size_t i = 0; i < part.size(); ++i) {
        const std::string& t = part[i].text;
        if (t == "(" || t == "[" || t == "{") ++depth;
        if (t == ")" || t == "]" || t == "}") --depth;
        if (depth == 0 && isComparison(t)) {
            left.assign(part.begin(), part.begin() + static_cast<std::ptrdiff_t>(i));
            right.assign(part.begin() + static_cast<std::ptrdiff_t>(i) + 1, part.end());
            return !left.empty() && !right.empty();
        }
    }
    return false;
}

bool updates(const Tokens& tokens, const std::string& name) {
    if (tokens.size() < 2) return false;
    if (isName(tokens[0]) && tokens[0].text == name) {
        return isAssignOp(tokens[1].text) || isIncDec(tokens[1]);
    }
    return isIncDec(tokens[0]) && isName(tokens[1]) && tokens[1].text == name;
}

struct Update {
    const Node* statement;
    bool conditional;
};

void findUpdates(const Node& node, const std::string& name, bool conditional,
                 std::vector<Update>& out) {
    if (node.kind == NodeKind::Statement && updates(node.tokens, name)) {
        out.push_back({&node, conditional});
    }
    const bool inner = conditional || node.kind == NodeKind::Branch ||
                       node.kind == NodeKind::Switch || node.kind == NodeKind::Loop;
    for (const Node& child : node.children) findUpdates(child, name, inner, out);
}

void collectStatements(const Node& node, std::vector<const Node*>& out) {
    if (node.kind == NodeKind::Statement) out.push_back(&node);
    for (const Node& child : node.children) {
        if (child.kind != NodeKind::Loop) collectStatements(child, out);
    }
}

StepKind stepForOperator(const std::string& op, long long value) {
    if (op == "+" || op == "-" || op == "+=" || op == "-=") {
        return value >= 1 ? StepKind::Additive : StepKind::Other;
    }
    if (op == "*" || op == "/" || op == "*=" || op == "/=") {
        return value >= 2 ? StepKind::Multiplicative : StepKind::Other;
    }
    if (op == "<<" || op == ">>" || op == "<<=" || op == ">>=") {
        return value >= 1 ? StepKind::Multiplicative : StepKind::Other;
    }
    return StepKind::Other;
}

struct Step {
    std::string variable;
    StepKind kind = StepKind::Other;
};

Step classifyStep(const Tokens& t) {
    Step step;
    if (t.empty()) return step;
    if (isName(t[0])) step.variable = t[0].text;
    else if (t.size() == 2 && isName(t[1])) step.variable = t[1].text;

    long long value = 0;
    if (t.size() == 2) {
        const bool postfix = isName(t[0]) && isIncDec(t[1]);
        const bool prefix = isIncDec(t[0]) && isName(t[1]);
        if (postfix || prefix) step.kind = StepKind::Additive;
    } else if (t.size() == 3 && isName(t[0]) && numberValue(t[2], value)) {
        step.kind = stepForOperator(t[1].text, value);
    } else if (t.size() == 5 && isName(t[0]) && t[1].text == "=") {
        if (t[2].text == t[0].text && numberValue(t[4], value)) {
            step.kind = stepForOperator(t[3].text, value);
        } else if (numberValue(t[2], value) && t[4].text == t[0].text &&
                   (t[3].text == "+" || t[3].text == "*")) {
            step.kind = stepForOperator(t[3].text, value);
        }
    }
    return step;
}

BoundKind classifyEnd(const Tokens& tokens, const std::vector<std::string>& inputs) {
    if (tokens.empty()) return BoundKind::Unknown;
    if (tokens[0].text == "{") return BoundKind::Constant;
    bool anyName = false;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const Token& token = tokens[i];
        if (!isName(token)) continue;
        anyName = true;
        if (std::find(inputs.begin(), inputs.end(), token.text) != inputs.end()) {
            return BoundKind::Input;
        }
        const bool member = i > 0 && (tokens[i - 1].text == "." || tokens[i - 1].text == "->");
        if (member && (token.text == "size" || token.text == "length")) return BoundKind::Input;
        if (token.text == "strlen") return BoundKind::Input;
    }
    return anyName ? BoundKind::Variable : BoundKind::Constant;
}

BoundKind classifyRange(const Tokens& start, const Tokens& limit,
                        const std::vector<std::string>& inputs) {
    if (limit.empty()) return BoundKind::Unknown;
    if (start.empty()) return classifyEnd(limit, inputs);
    for (const Token& token : start) {
        if (isName(token) && mentions(limit, token.text)) return BoundKind::Variable;
    }
    const BoundKind a = classifyEnd(start, inputs);
    const BoundKind b = classifyEnd(limit, inputs);
    if (a == BoundKind::Input || b == BoundKind::Input) return BoundKind::Input;
    if (a == BoundKind::Constant && b == BoundKind::Constant) return BoundKind::Constant;
    return BoundKind::Variable;
}

Tokens initialValue(const Tokens& init, const std::string& name) {
    for (const Tokens& part : splitTopLevel(init, ",")) {
        for (std::size_t i = 0; i + 1 < part.size(); ++i) {
            if (isName(part[i]) && part[i].text == name && part[i + 1].text == "=") {
                return Tokens(part.begin() + static_cast<std::ptrdiff_t>(i) + 2, part.end());
            }
        }
    }
    return {};
}

bool midpoint(const Tokens& tokens, std::string& target, Tokens& expression) {
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        if (tokens[i].text != "=") continue;
        if (!isName(tokens[i - 1])) return false;
        expression.assign(tokens.begin() + static_cast<std::ptrdiff_t>(i) + 1, tokens.end());
        const std::size_t n = expression.size();
        long long value = 0;
        if (n < 4 || !numberValue(expression[n - 1], value)) return false;
        const bool halves = (expression[n - 2].text == "/" && value == 2) ||
                            (expression[n - 2].text == ">>" && value == 1);
        bool hasPlus = false;
        for (const Token& token : expression) {
            if (token.text == "+") hasPlus = true;
        }
        target = tokens[i - 1].text;
        return halves && hasPlus;
    }
    return false;
}

bool dividesByConstant(const Tokens& tokens) {
    for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
        long long value = 0;
        if (!numberValue(tokens[i + 1], value)) continue;
        if (tokens[i].text == "/" && value >= 2) return true;
        if (tokens[i].text == ">>" && value >= 1) return true;
    }
    return false;
}

bool binarySearchFacts(const Node& body, const Tokens& cond,
                       const std::vector<std::string>& inputs, LoopFacts& facts) {
    std::vector<const Node*> statements;
    collectStatements(body, statements);

    for (const Tokens& part : splitTopLevel(cond, "&&")) {
        Tokens left, right;
        if (!splitComparison(part, left, right) || !isSingleName(left) || !isSingleName(right)) {
            continue;
        }
        const std::string& lo = left[0].text;
        const std::string& hi = right[0].text;
        for (const Node* mid : statements) {
            std::string target;
            Tokens expression;
            if (!midpoint(mid->tokens, target, expression) || !mentions(expression, lo) ||
                !mentions(expression, hi)) {
                continue;
            }
            std::vector<Update> moves;
            findUpdates(body, lo, false, moves);
            findUpdates(body, hi, false, moves);
            for (const Update& move : moves) {
                if (!mentions(move.statement->tokens, target)) continue;
                facts.variable = lo + ", " + hi;
                facts.step = StepKind::Multiplicative;
                facts.stepText = tokensToText(mid->tokens);
                facts.start = lo;
                facts.limit = hi;
                facts.bound = classifyRange(left, right, inputs);
                facts.note = "the range [" + lo + ", " + hi + "] is halved around " + target;
                return true;
            }
        }
    }
    return false;
}

void whileFacts(const Node& loop, const std::vector<std::string>& inputs, LoopFacts& facts) {
    const Tokens& cond = loop.cond;

    if (cond.size() == 2) {
        const Step step = classifyStep(cond);
        if (step.kind == StepKind::Additive) {
            facts.variable = step.variable;
            facts.step = step.kind;
            facts.stepText = tokensToText(cond);
            facts.start = step.variable;
            facts.limit = "0";
            const Token& name = isName(cond[0]) ? cond[0] : cond[1];
            facts.bound = classifyEnd(Tokens{name}, inputs);
            return;
        }
    }
    if (splitTopLevel(cond, "||").size() > 1) {
        facts.note = "the condition uses ||";
        return;
    }
    if (loop.children.empty()) {
        facts.note = "the loop has no body";
        return;
    }
    const Node& body = loop.children[0];
    if (binarySearchFacts(body, cond, inputs, facts)) return;

    for (const Tokens& part : splitTopLevel(cond, "&&")) {
        Tokens left, right;
        if (!splitComparison(part, left, right)) continue;
        for (int side = 0; side < 2; ++side) {
            const Tokens& mine = side == 0 ? left : right;
            const Tokens& other = side == 0 ? right : left;
            if (!isSingleName(mine)) continue;
            const std::string& name = mine[0].text;
            std::vector<Update> found;
            findUpdates(body, name, false, found);
            if (found.empty()) continue;

            facts.variable = name;
            facts.start = name;
            facts.limit = tokensToText(other);
            facts.stepText = tokensToText(found[0].statement->tokens);
            facts.bound = classifyRange(mine, other, inputs);
            if (found.size() > 1 || found[0].conditional) {
                facts.note = name + " is updated in more than one place or only under a condition";
            } else {
                facts.step = classifyStep(found[0].statement->tokens).kind;
            }
            return;
        }
    }
    facts.note = cond.empty() ? "the loop has no condition"
                              : "no variable of the condition is updated in the loop body";
}

void forFacts(const Node& loop, const std::vector<std::string>& inputs, LoopFacts& facts) {
    const std::vector<Tokens> parts = splitTopLevel(loop.step, ",");
    Tokens chosen = parts[0];
    for (const Tokens& part : parts) {
        if (mentions(loop.cond, classifyStep(part).variable)) {
            chosen = part;
            break;
        }
    }
    const Step step = classifyStep(chosen);
    facts.variable = step.variable;
    facts.step = step.kind;
    facts.stepText = tokensToText(chosen);
    if (step.kind == StepKind::Other) {
        facts.note = "the step " + facts.stepText + " does not add or multiply by a constant";
    }
    if (!loop.children.empty() && !step.variable.empty()) {
        std::vector<Update> found;
        findUpdates(loop.children[0], step.variable, false, found);
        facts.modifiedInBody = !found.empty();
    }

    if (splitTopLevel(loop.cond, "||").size() > 1) {
        facts.note = "the condition uses ||";
        return;
    }
    std::string expressionText;
    for (const Tokens& part : splitTopLevel(loop.cond, "&&")) {
        Tokens left, right;
        if (!splitComparison(part, left, right)) continue;
        const bool onLeft = isSingleName(left) && left[0].text == step.variable;
        const bool onRight = isSingleName(right) && right[0].text == step.variable;
        if (onLeft || onRight) {
            const Tokens& limit = onLeft ? right : left;
            const Tokens start = initialValue(loop.init, step.variable);
            facts.start = tokensToText(start);
            facts.limit = tokensToText(limit);
            facts.bound = classifyRange(start, limit, inputs);
            return;
        }
        if (mentions(part, step.variable)) expressionText = tokensToText(part);
    }
    if (facts.note.empty()) {
        facts.note = expressionText.empty()
            ? "the condition does not compare the loop variable"
            : "the loop variable is part of an expression in the condition (" + expressionText + ")";
    }
}

}

std::string tokensToText(const Tokens& tokens) {
    std::string out;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const std::string& t = tokens[i].text;
        if (i > 0) {
            const Token& prev = tokens[i - 1];
            const bool afterOperand = isName(prev) || prev.text == ")" || prev.text == "]";
            bool space = true;
            if (t == "." || t == "->" || t == "::" || t == ")" || t == "]" || t == ",") space = false;
            if (prev.text == "." || prev.text == "->" || prev.text == "::" || prev.text == "(" ||
                prev.text == "[" || prev.text == "!") {
                space = false;
            }
            if ((t == "(" || t == "[" || isIncDec(tokens[i])) && afterOperand) space = false;
            const bool prefix = isIncDec(prev) &&
                                (i == 1 || !(isName(tokens[i - 2]) || tokens[i - 2].text == ")" ||
                                             tokens[i - 2].text == "]"));
            if (prefix && isName(tokens[i])) space = false;
            if (space) out += ' ';
        }
        out += t;
    }
    return out;
}

std::vector<std::string> inputNames(const Function& function) {
    std::vector<std::string> names;
    for (const Param& param : function.params) {
        if (!param.name.empty()) names.push_back(param.name);
    }
    names.push_back("n");
    names.push_back("N");
    return names;
}

LoopFacts analyzeLoop(const Node& loop, const std::vector<std::string>& inputs) {
    LoopFacts facts;
    facts.line = loop.line;

    if (loop.loopKind == LoopKind::RangeFor) {
        for (const Token& token : loop.init) {
            if (isName(token)) facts.variable = token.text;
        }
        facts.step = StepKind::Additive;
        facts.stepText = "one element per iteration";
        facts.limit = tokensToText(loop.cond);
        facts.bound = classifyEnd(loop.cond, inputs);
        return facts;
    }
    if (loop.loopKind == LoopKind::For && !loop.step.empty()) {
        forFacts(loop, inputs, facts);
    } else {
        whileFacts(loop, inputs, facts);
    }
    return facts;
}

bool isRecursiveCall(const Call& call, const Function& function) {
    if (function.name.empty()) return false;
    if (call.callee == function.name || call.callee == "this->" + function.name) return true;
    return !function.className.empty() &&
           call.callee == function.className + "::" + function.name;
}

bool isMidpointStatement(const Tokens& tokens) {
    std::string target;
    Tokens expression;
    return midpoint(tokens, target, expression);
}

namespace {

struct FeatureCounter {
    const Function& function;
    std::vector<std::string> inputs;
    FeatureVector features;

    void visit(const Node& node, int depth) {
        features.functionCallCount += static_cast<int>(node.calls.size());
        for (const Call& call : node.calls) {
            if (!isRecursiveCall(call, function) && isSortCall(call.callee)) {
                ++features.sortCallCount;
            }
            if (!isRecursiveCall(call, function)) continue;
            features.hasRecursion = true;
            for (const Tokens& arg : call.args) {
                if (dividesByConstant(arg)) features.hasLogPattern = true;
            }
        }
        if (node.kind == NodeKind::Statement && isMidpointStatement(node.tokens)) {
            features.hasLogPattern = true;
        }

        int childDepth = depth;
        if (node.kind == NodeKind::Loop) {
            ++features.loopCount;
            childDepth = depth + 1;
            features.maxNestingDepth = std::max(features.maxNestingDepth, childDepth);
            const LoopFacts facts = analyzeLoop(node, inputs);
            if (facts.step == StepKind::Additive) ++features.linearLoopCount;
            if (facts.step == StepKind::Multiplicative) {
                ++features.logLoopCount;
                features.hasLogPattern = true;
            }
        }
        for (const Node& child : node.children) visit(child, childDepth);
    }
};

}

FeatureVector extractFeatures(const Function& function, const Tokens& tokens) {
    FeatureCounter counter{function, inputNames(function), FeatureVector()};
    counter.visit(function.body, 0);

    std::set<int> lines;
    for (const Token& token : tokens) {
        if (token.kind == TokenKind::EndOfFile) continue;
        if (token.line >= function.line && token.line <= function.endLine) lines.insert(token.line);
    }
    counter.features.linesOfCode = static_cast<int>(lines.size());
    return counter.features;
}

}
