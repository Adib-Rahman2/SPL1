#include "advice/PatternDetector.h"

#include "analysis/LibraryCosts.h"
#include "analysis/Recurrence.h"
#include "features/FeatureExtractor.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace codecomplex {

namespace {

using Tokens = std::vector<Token>;

bool hasName(const Tokens& tokens, const std::string& name) {
    for (const Token& token : tokens) {
        if (token.kind == TokenKind::Identifier && token.text == name) return true;
    }
    return false;
}

bool hasText(const Tokens& tokens, const char* text) {
    for (const Token& token : tokens) {
        if (token.text == text) return true;
    }
    return false;
}

void splitName(const std::string& callee, std::string& qualifier, std::string& base) {
    const std::size_t cut = callee.find_last_of(".:>");
    if (cut == std::string::npos) {
        qualifier.clear();
        base = callee;
        return;
    }
    base = callee.substr(cut + 1);
    std::size_t end = cut;
    if (cut > 0 && (callee[cut - 1] == ':' || callee[cut - 1] == '-')) end = cut - 1;
    qualifier = callee.substr(0, end);
}

struct Access {
    std::string array;
    bool usesVar = false;
};

std::vector<Access> elementAccesses(const Tokens& tokens, const std::string& var) {
    std::vector<Access> accesses;
    for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
        std::size_t open = 0;
        std::string array;
        if (tokens[i].kind == TokenKind::Identifier && tokens[i + 1].text == "[") {
            array = tokens[i].text;
            open = i + 1;
        } else if (i + 3 < tokens.size() && tokens[i].kind == TokenKind::Identifier &&
                   tokens[i + 1].text == "." &&
                   (tokens[i + 2].text == "get" || tokens[i + 2].text == "charAt") &&
                   tokens[i + 3].text == "(") {
            array = tokens[i].text;
            open = i + 3;
        } else {
            continue;
        }
        Access access;
        access.array = array;
        int depth = 0;
        for (std::size_t j = open; j < tokens.size(); ++j) {
            const std::string& t = tokens[j].text;
            if (t == "[" || t == "(") ++depth;
            if (t == "]" || t == ")") --depth;
            if (depth == 0) break;
            if (tokens[j].kind == TokenKind::Identifier && t == var) access.usesVar = true;
        }
        accesses.push_back(access);
    }
    return accesses;
}

bool usesElementOf(const Tokens& cond, const std::string& var) {
    for (const Access& a : elementAccesses(cond, var)) {
        if (a.usesVar) return true;
    }
    return false;
}

std::vector<Tokens> clauses(const Tokens& cond) {
    std::vector<Tokens> parts(1);
    int depth = 0;
    for (const Token& t : cond) {
        if (t.text == "(" || t.text == "[") ++depth;
        if (t.text == ")" || t.text == "]") --depth;
        if (depth == 0 && (t.text == "&&" || t.text == "||")) {
            parts.emplace_back();
            continue;
        }
        parts.back().push_back(t);
    }
    for (Tokens& part : parts) {
        while (part.size() >= 2 && part.front().text == "(" && part.back().text == ")") {
            part = Tokens(part.begin() + 1, part.end() - 1);
        }
    }
    return parts;
}

bool isPlainElementExpression(const Tokens& side) {
    for (std::size_t i = 0; i < side.size(); ++i) {
        const std::string& t = side[i].text;
        if (t == "%" || t == "*" || t == "/" || t == "&" || t == "|" || t == "^" || t == "!") {
            return false;
        }
        if (t == "]" && i + 1 < side.size() && side[i + 1].text == "[") return false;
        if (t == "(" && i > 0 && side[i - 1].kind == TokenKind::Identifier &&
            side[i - 1].text != "get" && side[i - 1].text != "charAt") {
            return false;
        }
    }
    return true;
}

bool isLiteral(const Tokens& side) {
    if (side.size() != 1) return false;
    const Token& t = side[0];
    return t.kind == TokenKind::Number || t.kind == TokenKind::Char ||
           t.kind == TokenKind::String || t.text == "true" || t.text == "false" ||
           t.text == "null" || t.text == "nullptr";
}

bool looksUpElement(const Tokens& cond, const std::string& var) {
    for (const Tokens& clause : clauses(cond)) {
        std::size_t eq = clause.size();
        int depth = 0;
        int count = 0;
        for (std::size_t i = 0; i < clause.size(); ++i) {
            const std::string& t = clause[i].text;
            if (t == "(" || t == "[") ++depth;
            if (t == ")" || t == "]") --depth;
            if (depth == 0 && t == "==") {
                eq = i;
                ++count;
            }
        }
        if (count != 1) continue;
        const Tokens left(clause.begin(), clause.begin() + eq);
        const Tokens right(clause.begin() + eq + 1, clause.end());
        for (int side = 0; side < 2; ++side) {
            const Tokens& s = side == 0 ? left : right;
            const Tokens& t = side == 0 ? right : left;
            const bool sumOfElements = elementAccesses(s, var).size() >= 2;
            if (usesElementOf(s, var) && isPlainElementExpression(s) && !hasName(t, var) &&
                !t.empty() && (!isLiteral(t) || sumOfElements)) {
                return true;
            }
        }
    }
    return false;
}

bool fillsTable(const Node& node) {
    if (node.kind == NodeKind::Statement) {
        const Tokens& t = node.tokens;
        for (std::size_t i = 0; i + 1 < t.size(); ++i) {
            if (t[i].text == "]" && t[i + 1].text == "[") {
                for (std::size_t j = i + 1; j < t.size(); ++j) {
                    if (t[j].text == "=") return true;
                    if (t[j].text == "==") break;
                }
            }
        }
    }
    for (const Node& child : node.children) {
        if (fillsTable(child)) return true;
    }
    return false;
}

bool comparesTwoElements(const Tokens& cond, const std::string& var, std::string& array) {
    if (!hasText(cond, "<") && !hasText(cond, ">") && !hasText(cond, "<=") && !hasText(cond, ">=")) {
        return false;
    }
    if (clauses(cond).size() != 1 || !isPlainElementExpression(cond)) return false;
    const std::vector<Access> accesses = elementAccesses(cond, var);
    for (std::size_t i = 0; i < accesses.size(); ++i) {
        for (std::size_t j = i + 1; j < accesses.size(); ++j) {
            if (accesses[i].array == accesses[j].array &&
                (accesses[i].usesVar || accesses[j].usesVar)) {
                array = accesses[i].array;
                return true;
            }
        }
    }
    return false;
}

bool writesElements(const Node& node, const std::string& array) {
    if (node.kind == NodeKind::Statement) {
        const Tokens& t = node.tokens;
        for (std::size_t i = 0; i + 1 < t.size(); ++i) {
            if (t[i].text != array || t[i + 1].text != "[") continue;
            int depth = 0;
            std::size_t j = i + 1;
            for (; j < t.size(); ++j) {
                if (t[j].text == "[") ++depth;
                if (t[j].text == "]" && --depth == 0) break;
            }
            if (j + 1 < t.size() && t[j + 1].text == "=") return true;
        }
        for (const Call& call : node.calls) {
            std::string qualifier, base;
            splitName(call.callee, qualifier, base);
            if (base != "swap") continue;
            for (const Tokens& arg : call.args) {
                if (!arg.empty() && arg[0].text == array) return true;
            }
        }
    }
    for (const Node& child : node.children) {
        if (writesElements(child, array)) return true;
    }
    return false;
}

bool containsLoop(const Node& node) {
    if (node.kind == NodeKind::Loop) return true;
    for (const Node& child : node.children) {
        if (containsLoop(child)) return true;
    }
    return false;
}

bool readsElementAt(const Tokens& tokens, const std::string& variable) {
    int depth = 0;
    for (const Token& t : tokens) {
        if (t.text == "[") ++depth;
        if (t.text == "]" && depth > 0) --depth;
        if (depth > 0 && t.kind == TokenKind::Identifier && t.text == variable) return true;
    }
    return false;
}

bool onlyAccumulates(const Node& body, const std::string& variable) {
    std::vector<const Node*> statements;
    if (body.kind == NodeKind::Block) {
        for (const Node& child : body.children) statements.push_back(&child);
    } else {
        statements.push_back(&body);
    }
    if (statements.empty()) return false;
    bool readsElement = false;
    for (const Node* s : statements) {
        if (s->kind != NodeKind::Statement || s->tokens.empty()) return false;
        const bool compound = hasText(s->tokens, "+=") || hasText(s->tokens, "-=") ||
                              hasText(s->tokens, "*=");
        const bool selfAdd = s->tokens.size() >= 4 && s->tokens[1].text == "=" &&
                             s->tokens[2].text == s->tokens[0].text;
        if (!compound && !selfAdd) return false;
        if (readsElementAt(s->tokens, variable)) readsElement = true;
    }
    return readsElement;
}

bool assignsTo(const Tokens& tokens, const std::set<std::string>& names) {
    static const std::set<std::string> ops = {"=", "+=", "-=", "*=", "/=", "%=", "++", "--",
                                              "<<=", ">>=", "&=", "|=", "^="};
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].kind != TokenKind::Identifier || names.count(tokens[i].text) == 0) continue;
        if (i + 1 < tokens.size() && ops.count(tokens[i + 1].text)) return true;
        if (i > 0 && (tokens[i - 1].text == "++" || tokens[i - 1].text == "--")) return true;
    }
    return false;
}

std::string callText(const Call& call) {
    std::string text = call.callee + "(";
    for (std::size_t i = 0; i < call.args.size(); ++i) {
        if (i > 0) text += ", ";
        text += tokensToText(call.args[i]);
    }
    return text + ")";
}

bool isListType(const std::string& word) {
    static const std::set<std::string> types = {"List", "ArrayList", "LinkedList", "Vector",
                                                "String", "StringBuilder", "string", "vector"};
    return types.count(word) > 0;
}

std::set<std::string> listVariables(const Function& function) {
    std::set<std::string> names;
    for (const Param& param : function.params) {
        std::string word;
        for (std::size_t i = 0; i <= param.type.size(); ++i) {
            const char c = i < param.type.size() ? param.type[i] : ' ';
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                word += c;
            } else {
                if (isListType(word) && !param.name.empty()) names.insert(param.name);
                word.clear();
            }
        }
    }
    std::vector<const Node*> work = {&function.body};
    while (!work.empty()) {
        const Node* n = work.back();
        work.pop_back();
        for (const Node& child : n->children) work.push_back(&child);
        if (n->kind != NodeKind::Statement || n->tokens.empty()) continue;
        const Tokens& t = n->tokens;
        std::size_t i = 0;
        while (i < t.size() && (t[i].text == "final" || t[i].text == "const" || t[i].text == "std" ||
                                t[i].text == "::")) {
            ++i;
        }
        if (i >= t.size() || !isListType(t[i].text)) continue;
        ++i;
        int depth = 0;
        while (i < t.size() && (depth > 0 || t[i].text == "<" || t[i].text == "[" ||
                                t[i].text == "]" || t[i].text == "&")) {
            if (t[i].text == "<") ++depth;
            if (t[i].text == ">") --depth;
            if (t[i].text == ">>") depth -= 2;
            ++i;
        }
        if (i < t.size() && t[i].kind == TokenKind::Identifier) names.insert(t[i].text);
    }
    return names;
}

struct LoopContext {
    const Node* loop;
    LoopFacts facts;
    bool sized;
};

class Detector {
public:
    Detector(const Function& function, const RuleResult& rules)
        : function_(function), rules_(rules), inputs_(inputNames(function)),
          containers_(findContainers(function)), lists_(listVariables(function)) {}

    std::vector<DetectedPattern> run();

private:
    void visit(const Node& node);
    void checkCalls(const std::vector<Call>& calls);
    void checkLoop(const Node& loop, const LoopFacts& facts, bool sized);
    void checkCondition(const Tokens& cond, int line);
    void checkRecursion();
    bool repeatCandidate(const Call& call) const;
    void checkRepeats(const Node& node);
    void add(PatternKind kind, int line, const std::string& detail);
    int sizedLoopsAbove() const;

    const Function& function_;
    const RuleResult& rules_;
    std::vector<std::string> inputs_;
    std::map<std::string, ContainerKind> containers_;
    std::set<std::string> lists_;
    std::vector<LoopContext> loops_;
    std::set<const Node*> searchReported_;
    std::set<std::string> repeatReported_;
    std::vector<DetectedPattern> found_;
};

std::vector<DetectedPattern> Detector::run() {
    visit(function_.body);
    checkRecursion();
    std::stable_sort(found_.begin(), found_.end(),
                     [](const DetectedPattern& a, const DetectedPattern& b) { return a.line < b.line; });
    return found_;
}

void Detector::add(PatternKind kind, int line, const std::string& detail) {
    found_.push_back({kind, line, detail});
}

int Detector::sizedLoopsAbove() const {
    int count = 0;
    for (const LoopContext& c : loops_) {
        if (c.sized) ++count;
    }
    return count;
}

void Detector::visit(const Node& node) {
    checkCalls(node.calls);
    checkRepeats(node);
    if (node.kind == NodeKind::Branch && !node.cond.empty()) checkCondition(node.cond, node.line);

    if (node.kind != NodeKind::Loop) {
        for (const Node& child : node.children) visit(child);
        return;
    }

    const LoopFacts facts = analyzeLoop(node, inputs_);
    const bool sized = facts.step != StepKind::Other && facts.bound != BoundKind::Unknown &&
                       facts.bound != BoundKind::Constant;
    checkLoop(node, facts, sized);

    const bool indexGrows = node.loopKind != LoopKind::RangeFor && facts.step == StepKind::Additive &&
                            facts.bound == BoundKind::Input && !facts.variable.empty();
    if (indexGrows) inputs_.push_back(facts.variable);
    loops_.push_back({&node, facts, sized});
    for (const Node& child : node.children) visit(child);
    loops_.pop_back();
    if (indexGrows) inputs_.pop_back();
}

void Detector::checkCalls(const std::vector<Call>& calls) {
    for (const Call& call : calls) {
        if (isRecursiveCall(call, function_)) continue;
        std::string qualifier, base;
        splitName(call.callee, qualifier, base);
        const LibraryCost library = libraryCost(call, containers_, inputs_);

        if (!loops_.empty() && sizedLoopsAbove() > 0) {
            const bool stdSearch = library.found && library.cost == Complexity::linear() &&
                                   (base == "find" || base == "count");
            const bool javaSearch = (base == "indexOf" || base == "lastIndexOf" ||
                                     base == "contains") &&
                                    lists_.count(qualifier) > 0;
            if (stdSearch || javaSearch) {
                add(PatternKind::FindInLoop, call.line,
                    callText(call) + " searches element by element inside a loop" +
                        (javaSearch ? " (" + qualifier + " is a list or a string)" : ""));
            }
        }
    }
}

bool Detector::repeatCandidate(const Call& call) const {
    if (call.args.empty() || isRecursiveCall(call, function_)) return false;
    std::string qualifier, base;
    splitName(call.callee, qualifier, base);
    if (base == "get") return false;
    const LibraryCost library = libraryCost(call, containers_, inputs_);
    return !(library.found && (library.cost.isConstant() ||
                               library.what.find("operation") != std::string::npos));
}

void Detector::checkRepeats(const Node& node) {
    std::map<std::string, int> seen;
    for (const Call& call : node.calls) {
        if (!repeatCandidate(call)) continue;
        const std::string text = callText(call);
        if (++seen[text] == 2 && repeatReported_.insert(text).second) {
            add(PatternKind::RepeatedCall, call.line,
                text + " is computed twice in the same statement");
        }
    }
    if (node.kind != NodeKind::Branch || node.children.empty()) return;

    const Node& body = node.children[0];
    std::vector<const Node*> statements;
    if (body.kind == NodeKind::Block) {
        for (const Node& child : body.children) statements.push_back(&child);
    } else {
        statements.push_back(&body);
    }
    for (const Call& call : node.calls) {
        if (!repeatCandidate(call)) continue;
        const std::string text = callText(call);
        std::set<std::string> argNames;
        for (const Tokens& arg : call.args) {
            for (const Token& t : arg) {
                if (t.kind == TokenKind::Identifier) argNames.insert(t.text);
            }
        }
        for (const Node* statement : statements) {
            bool again = false;
            for (const Call& inner : statement->calls) {
                if (callText(inner) == text) again = true;
            }
            if (again && repeatReported_.insert(text).second) {
                add(PatternKind::RepeatedCall, statement->line,
                    text + " is computed in the condition on line " + std::to_string(node.line) +
                        " and again here");
                break;
            }
            if (statement->kind != NodeKind::Statement || assignsTo(statement->tokens, argNames)) break;
        }
    }
}

void Detector::checkLoop(const Node& loop, const LoopFacts& facts, bool sized) {
    for (const Call& call : loop.calls) {
        const LibraryCost library = libraryCost(call, containers_, inputs_);
        if (!library.found || library.cost != Complexity::linear()) continue;
        bool usesVariable = false;
        for (const Tokens& arg : call.args) {
            if (!facts.variable.empty() && hasName(arg, facts.variable)) usesVariable = true;
        }
        if (!usesVariable) {
            add(PatternKind::LoopInvariantCall, loop.line,
                callText(call) + " in the loop header is computed again on every iteration, "
                                 "but its arguments do not change with " +
                    (facts.variable.empty() ? std::string("the loop") : facts.variable));
        }
    }
    if (!sized || loops_.empty()) return;

    const LoopContext& outer = loops_.back();
    if (outer.sized && !outer.facts.variable.empty() && facts.step == StepKind::Additive &&
        !facts.start.empty() && std::isdigit(static_cast<unsigned char>(facts.start[0])) &&
        facts.limit == outer.facts.variable && !loop.children.empty() &&
        onlyAccumulates(loop.children[0], facts.variable)) {
        add(PatternKind::PrefixRecomputation, loop.line,
            "the loop on " + facts.variable + " sums from " + facts.start + " up to " +
                outer.facts.variable + " again for every " + outer.facts.variable);
    }

    if (sizedLoopsAbove() == 1 && !loop.children.empty() && !containsLoop(loop.children[0]) &&
        !facts.variable.empty()) {
        std::vector<const Node*> work = {&loop.children[0]};
        while (!work.empty()) {
            const Node* n = work.back();
            work.pop_back();
            std::string array;
            if (n->kind == NodeKind::Branch && comparesTwoElements(n->cond, facts.variable, array) &&
                writesElements(*outer.loop, array)) {
                add(PatternKind::QuadraticOrdering, n->line,
                    "two nested loops compare " + tokensToText(n->cond) + " and move elements of " +
                        array + ", the shape of a quadratic sort");
                break;
            }
            for (const Node& child : n->children) work.push_back(&child);
        }
    }
}

void Detector::checkCondition(const Tokens& cond, int line) {
    if (loops_.empty() || !hasText(cond, "==")) return;
    const LoopContext& inner = loops_.back();
    if (!inner.sized || inner.facts.step != StepKind::Additive || inner.facts.variable.empty() ||
        sizedLoopsAbove() < 2 || searchReported_.count(inner.loop) > 0) {
        return;
    }
    if (!looksUpElement(cond, inner.facts.variable)) return;
    if (!inner.loop->children.empty() && fillsTable(inner.loop->children[0])) return;
    searchReported_.insert(inner.loop);
    std::string outerVar;
    for (const LoopContext& c : loops_) {
        if (c.sized && &c != &inner) outerVar = c.facts.variable;
    }
    add(PatternKind::LinearSearchInLoop, line,
        "the loop on " + inner.facts.variable + " tests " + tokensToText(cond) +
            (outerVar.empty() ? std::string(" inside another loop")
                              : " again for every " + outerVar));
}

void Detector::checkRecursion() {
    if (rules_.complexity.kind != Complexity::Kind::Exponential) return;
    if (extractRecurrence(function_).memoized) return;

    std::vector<const Call*> selfCalls;
    std::vector<const Node*> work = {&function_.body};
    while (!work.empty()) {
        const Node* n = work.back();
        work.pop_back();
        for (const Call& call : n->calls) {
            if (isRecursiveCall(call, function_)) selfCalls.push_back(&call);
        }
        for (const Node& child : n->children) work.push_back(&child);
    }
    if (selfCalls.size() < 2) return;

    std::set<std::size_t> changing;
    for (const Call* call : selfCalls) {
        for (std::size_t p = 0; p < call->args.size(); ++p) {
            const std::string param = p < function_.params.size() ? function_.params[p].name : "";
            if (tokensToText(call->args[p]) != param) changing.insert(p);
        }
    }
    if (changing.size() != 1) return;

    std::string calls;
    std::set<std::string> seen;
    for (auto it = selfCalls.begin(); it != selfCalls.end(); ++it) {
        const std::string text = callText(**it);
        if (seen.insert(text).second) calls += (calls.empty() ? "" : ", ") + text;
    }
    add(PatternKind::OverlappingRecursion, function_.line,
        function_.name + "() calls itself as " + calls +
            " with nothing else changing, so the same values are computed many times");
}

}

const char* patternKindName(PatternKind kind) {
    switch (kind) {
    case PatternKind::LinearSearchInLoop:   return "Linear search inside a loop";
    case PatternKind::FindInLoop:           return "Repeated find/count inside a loop";
    case PatternKind::LoopInvariantCall:    return "Loop-invariant call in a loop header";
    case PatternKind::RepeatedCall:         return "Same call repeated with the same arguments";
    case PatternKind::OverlappingRecursion: return "Overlapping recursion without memoization";
    case PatternKind::QuadraticOrdering:    return "Quadratic sorting / ordering by nested loops";
    case PatternKind::PrefixRecomputation:  return "Prefix recomputed in a nested loop";
    }
    return "?";
}

std::vector<DetectedPattern> detectPatterns(const Function& function, const RuleResult& rules) {
    return Detector(function, rules).run();
}

}
