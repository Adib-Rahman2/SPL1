#include "analysis/Recurrence.h"

#include "features/FeatureExtractor.h"
#include "structure/Parser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>

namespace codecomplex {

namespace {

using Tokens = std::vector<Token>;

bool contains(const std::vector<std::string>& names, const std::string& name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

bool smallNumber(const Token& token, int& value) {
    if (token.kind != TokenKind::Number || token.text.empty() || token.text.size() > 6) return false;
    for (char c : token.text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    value = std::stoi(token.text);
    return true;
}

bool alwaysReturns(const Node& node) {
    switch (node.kind) {
    case NodeKind::Statement:
        return !node.tokens.empty() &&
               (node.tokens[0].text == "return" || node.tokens[0].text == "throw");
    case NodeKind::Block:
        for (const Node& child : node.children) {
            if (alwaysReturns(child)) return true;
        }
        return false;
    case NodeKind::Branch:
        return !node.children.empty() && alwaysReturns(node.children[0]);
    case NodeKind::If:
        if (node.children.empty() || !node.children.back().cond.empty()) return false;
        for (const Node& branch : node.children) {
            if (!alwaysReturns(branch)) return false;
        }
        return true;
    default:
        return false;
    }
}

class PathCounter {
public:
    explicit PathCounter(const Function& function) : function_(function) {}

    int calls(const std::vector<Call>& calls) const {
        int count = 0;
        for (const Call& call : calls) {
            if (isRecursiveCall(call, function_)) ++count;
        }
        return count;
    }

    int expression(const Tokens& tokens) const {
        std::size_t question = tokens.size(), colon = tokens.size(), end = tokens.size();
        int depth = 0, questionDepth = 0, nested = 0;
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            const std::string& t = tokens[i].text;
            if (t == "(" || t == "[" || t == "{") {
                ++depth;
            } else if (t == ")" || t == "]" || t == "}") {
                if (colon < tokens.size() && depth == questionDepth) {
                    end = i;
                    break;
                }
                --depth;
            } else if (t == "?") {
                if (question == tokens.size()) {
                    question = i;
                    questionDepth = depth;
                } else if (depth == questionDepth) {
                    ++nested;
                }
            } else if (t == ":" && question < tokens.size() && depth == questionDepth) {
                if (nested > 0) {
                    --nested;
                } else if (colon == tokens.size()) {
                    colon = i;
                }
            } else if (t == "," && colon < tokens.size() && depth == questionDepth) {
                end = i;
                break;
            }
        }
        if (colon == tokens.size()) return calls(findCalls(tokens));

        Tokens rest(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(question));
        rest.insert(rest.end(), tokens.begin() + static_cast<std::ptrdiff_t>(end), tokens.end());
        const Tokens yes(tokens.begin() + static_cast<std::ptrdiff_t>(question) + 1,
                         tokens.begin() + static_cast<std::ptrdiff_t>(colon));
        const Tokens no(tokens.begin() + static_cast<std::ptrdiff_t>(colon) + 1,
                        tokens.begin() + static_cast<std::ptrdiff_t>(end));
        return expression(rest) + std::max(expression(yes), expression(no));
    }

    int node(const Node& node) const {
        switch (node.kind) {
        case NodeKind::Statement:
            return expression(node.tokens);
        case NodeKind::Block:
            return block(node.children);
        case NodeKind::If:
            return ifNode(node, 0);
        case NodeKind::Branch:
        case NodeKind::Switch:
            return calls(node.calls) + (node.children.empty() ? 0 : this->node(node.children[0]));
        case NodeKind::Loop:
        case NodeKind::Unsupported:
            return 0;
        }
        return 0;
    }

private:
    int block(const std::vector<Node>& children) const {
        int rest = 0;
        for (std::size_t i = children.size(); i-- > 0;) {
            const Node& child = children[i];
            if (child.kind == NodeKind::If) {
                rest = ifNode(child, rest);
            } else if (alwaysReturns(child)) {
                rest = node(child);
            } else {
                rest = node(child) + rest;
            }
        }
        return rest;
    }

    int ifNode(const Node& node, int rest) const {
        int conditions = 0;
        int best = 0;
        for (const Node& branch : node.children) {
            conditions += calls(branch.calls);
            if (branch.children.empty()) continue;
            const Node& body = branch.children[0];
            best = std::max(best, this->node(body) + (alwaysReturns(body) ? 0 : rest));
        }
        const bool hasElse = !node.children.empty() && node.children.back().cond.empty();
        if (!hasElse) best = std::max(best, rest);
        return conditions + best;
    }

    const Function& function_;
};

struct SelfCall {
    const Call* call;
    int loopDepth;
    const Node* loop;
};

void collectSelfCalls(const Node& node, const Function& function, int depth, const Node* loop,
                      std::vector<SelfCall>& out) {
    if (node.kind == NodeKind::Loop) {
        ++depth;
        loop = &node;
    }
    for (const Call& call : node.calls) {
        if (isRecursiveCall(call, function)) out.push_back({&call, depth, loop});
    }
    for (const Node& child : node.children) collectSelfCalls(child, function, depth, loop, out);
}

void midpointNames(const Node& node, std::vector<std::string>& out) {
    if (node.kind == NodeKind::Statement && isMidpointStatement(node.tokens)) {
        for (std::size_t i = 1; i < node.tokens.size(); ++i) {
            if (node.tokens[i].text == "=") {
                out.push_back(node.tokens[i - 1].text);
                break;
            }
        }
    }
    for (const Node& child : node.children) midpointNames(child, out);
}

struct Shrink {
    ShrinkKind kind = ShrinkKind::Other;
    int amount = 0;
    bool unchanged = false;
    std::string text;
};

Shrink argumentShrink(const Tokens& arg, const std::string& param,
                      const std::vector<std::string>& midpoints) {
    Shrink shrink;
    const std::string text = tokensToText(arg);
    if (arg.size() == 1 && arg[0].text == param) {
        shrink.unchanged = true;
        return shrink;
    }
    for (const Token& token : arg) {
        if (token.kind == TokenKind::Identifier && contains(midpoints, token.text)) {
            shrink.kind = ShrinkKind::Divide;
            shrink.amount = 2;
            shrink.text = text + " uses the midpoint " + token.text + ", so the range is halved";
            return shrink;
        }
    }
    int value = 0;
    const std::size_t n = arg.size();
    if (n >= 4 && smallNumber(arg[n - 1], value) &&
        ((arg[n - 2].text == "/" && value == 2) || (arg[n - 2].text == ">>" && value == 1))) {
        for (const Token& token : arg) {
            if (token.text != "+") continue;
            shrink.kind = ShrinkKind::Divide;
            shrink.amount = 2;
            shrink.text = text + " is a midpoint, so the range is halved";
            return shrink;
        }
    }
    if (n == 3 && !param.empty() && arg[0].text == param && smallNumber(arg[2], value)) {
        const std::string& op = arg[1].text;
        const std::string number = std::to_string(value);
        if (op == "-" && value >= 1) {
            shrink.kind = ShrinkKind::Subtract;
            shrink.amount = value;
            shrink.text = text + " subtracts " + number + " from " + param;
        } else if (op == "+" && value >= 1) {
            shrink.kind = ShrinkKind::Subtract;
            shrink.amount = value;
            shrink.text = text + " moves " + param + " " + number +
                          " towards the end, so the remaining size shrinks by " + number;
        } else if (op == "/" && value >= 2) {
            shrink.kind = ShrinkKind::Divide;
            shrink.amount = value;
            shrink.text = text + " divides " + param + " by " + number;
        } else if (op == ">>" && value >= 1 && value <= 4) {
            shrink.kind = ShrinkKind::Divide;
            shrink.amount = 1 << value;
            shrink.text = text + " divides " + param + " by " + std::to_string(1 << value);
        }
    }
    return shrink;
}

std::string callText(const Call& call) {
    std::string text = call.callee + "(";
    for (std::size_t i = 0; i < call.args.size(); ++i) {
        if (i > 0) text += ", ";
        text += tokensToText(call.args[i]);
    }
    return text + ")";
}

Shrink callShrink(const Call& call, const Function& function,
                  const std::vector<std::string>& midpoints) {
    Shrink best;
    bool found = false;
    for (std::size_t i = 0; i < call.args.size(); ++i) {
        const std::string param = i < function.params.size() ? function.params[i].name : "";
        const Shrink shrink = argumentShrink(call.args[i], param, midpoints);
        if (shrink.kind == ShrinkKind::Divide) return shrink;
        if (shrink.kind == ShrinkKind::Subtract && (!found || shrink.amount < best.amount)) {
            best = shrink;
            found = true;
        }
    }
    if (found) return best;
    best.kind = ShrinkKind::Other;
    best.text = callText(call) + ": no argument shrinks in a recognised way "
                                 "(n - c, n / b or a midpoint)";
    return best;
}

void storedTables(const Node& node, std::vector<std::string>& out) {
    if (node.kind == NodeKind::Statement) {
        const Tokens& t = node.tokens;
        for (std::size_t i = 0; i + 1 < t.size(); ++i) {
            if (t[i].kind != TokenKind::Identifier || t[i + 1].text != "[") continue;
            int depth = 0;
            std::size_t j = i + 1;
            for (; j < t.size(); ++j) {
                if (t[j].text == "[") ++depth;
                if (t[j].text == "]" && --depth == 0) break;
            }
            if (j + 1 < t.size() && t[j + 1].text == "=") out.push_back(t[i].text);
        }
    }
    for (const Node& child : node.children) storedTables(child, out);
}

const Node* tableCheck(const Node& node, const std::vector<std::string>& tables) {
    if (node.kind == NodeKind::Branch && !node.cond.empty() && alwaysReturns(node)) {
        for (std::size_t i = 0; i + 1 < node.cond.size(); ++i) {
            const std::string& next = node.cond[i + 1].text;
            if (contains(tables, node.cond[i].text) && (next == "[" || next == ".")) return &node;
        }
    }
    for (const Node& child : node.children) {
        if (const Node* found = tableCheck(child, tables)) return found;
    }
    return nullptr;
}

void collectCalls(const Node& node, std::vector<const Call*>& out) {
    for (const Call& call : node.calls) out.push_back(&call);
    for (const Node& child : node.children) collectCalls(child, out);
}

bool sameFunction(const Function& a, const Function& b) {
    return a.name == b.name && a.className == b.className && a.line == b.line;
}

using FunctionsByName = std::map<std::string, std::vector<std::size_t>>;

FunctionsByName indexByName(const std::vector<Function>& program) {
    FunctionsByName index;
    for (std::size_t i = 0; i < program.size(); ++i) index[program[i].name].push_back(i);
    return index;
}

std::string calledName(const std::string& callee) {
    const std::size_t cut = callee.find_last_of(".:>");
    return cut == std::string::npos ? callee : callee.substr(cut + 1);
}

bool findPathBack(const Function& current, const Function& target,
                  const std::vector<Function>& program, const FunctionsByName& byName,
                  std::vector<bool>& visited, std::vector<std::string>& path) {
    std::vector<const Call*> calls;
    collectCalls(current.body, calls);
    for (const Call* call : calls) {
        const auto named = byName.find(calledName(call->callee));
        if (named == byName.end()) continue;
        for (std::size_t i : named->second) {
            const Function& callee = program[i];
            if (!isRecursiveCall(*call, callee) || sameFunction(callee, current)) continue;
            if (sameFunction(callee, target)) {
                path.push_back(target.name);
                return true;
            }
            if (visited[i]) continue;
            visited[i] = true;
            path.push_back(callee.name);
            if (findPathBack(callee, target, program, byName, visited, path)) return true;
            path.pop_back();
        }
    }
    return false;
}

std::string formatExponent(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.2f", value);
    return buffer;
}

}

Recurrence extractRecurrence(const Function& function) {
    Recurrence r;
    std::vector<SelfCall> sites;
    collectSelfCalls(function.body, function, 0, nullptr, sites);
    if (sites.empty()) {
        r.unsupported = "no call to " + function.name + "() was found";
        return r;
    }

    std::vector<std::string> midpoints;
    midpointNames(function.body, midpoints);
    bool subtract = false;
    int smallestSubtract = 0;
    std::vector<int> divisors;
    for (const SelfCall& site : sites) {
        const Shrink shrink = callShrink(*site.call, function, midpoints);
        if (shrink.kind == ShrinkKind::Other) {
            r.unsupported = shrink.text;
            return r;
        }
        r.reasons.push_back({site.call->line, "recursive call " + callText(*site.call) + ": " +
                                                  shrink.text});
        if (shrink.kind == ShrinkKind::Subtract) {
            r.amounts.push_back(shrink.amount);
            if (!subtract || shrink.amount < smallestSubtract) smallestSubtract = shrink.amount;
            subtract = true;
        } else if (std::find(divisors.begin(), divisors.end(), shrink.amount) == divisors.end()) {
            divisors.push_back(shrink.amount);
        }
    }
    if (subtract && !divisors.empty()) {
        r.unsupported = "the calls mix n - c and n / b shrinks";
        return r;
    }
    if (divisors.size() > 1) {
        r.unsupported = "uneven split: the calls divide the size by " +
                        std::to_string(divisors[0]) + " and by " + std::to_string(divisors[1]);
        return r;
    }
    r.shrink = subtract ? ShrinkKind::Subtract : ShrinkKind::Divide;
    r.amount = subtract ? smallestSubtract : divisors[0];

    const PathCounter counter(function);
    std::vector<const SelfCall*> inLoops;
    for (const SelfCall& site : sites) {
        if (site.loopDepth > 0) inLoops.push_back(&site);
    }
    if (!inLoops.empty()) {
        const std::string shape = "a recursive call inside a loop that is not the n*T(n - 1) "
                                  "permutation shape";
        const Node* loop = inLoops[0]->loop;
        bool oneLoop = inLoops.size() == sites.size();
        for (const SelfCall* site : inLoops) {
            if (site->loop != loop || site->loopDepth != 1) oneLoop = false;
        }
        if (!oneLoop) {
            r.unsupported = shape + " (all recursive calls must be in one loop, not nested)";
            return r;
        }
        const int perIteration =
            counter.calls(loop->calls) + (loop->children.empty() ? 0 : counter.node(loop->children[0]));
        if (perIteration != 1) {
            r.unsupported = shape + " (the loop body makes " + std::to_string(perIteration) +
                            " recursive calls per iteration, not 1)";
            return r;
        }
        if (r.shrink != ShrinkKind::Subtract || r.amount != 1) {
            r.unsupported = shape + " (the call must shrink the size by exactly 1)";
            return r;
        }
        const LoopFacts facts = analyzeLoop(*loop, inputNames(function));
        if (facts.step != StepKind::Additive ||
            (facts.bound != BoundKind::Input && facts.bound != BoundKind::Variable)) {
            r.unsupported = shape + " (the loop does not run over the remaining items)";
            return r;
        }
        r.calls = 1;
        r.inLoop = true;
        r.reasons.push_back({loop->line, "the loop runs over the remaining items and makes one "
                                         "recursive call per iteration"});
    } else {
        r.calls = counter.node(function.body);
        if (r.calls == 0) {
            r.unsupported = "no recursive call is reachable (each one comes after a return)";
            return r;
        }
        std::string text = "a = " + std::to_string(r.calls) + ": the most recursive calls one run "
                                                               "of the body can make";
        if (static_cast<int>(sites.size()) > r.calls) {
            text += " (calls in mutually exclusive branches are counted once)";
        }
        r.reasons.push_back({function.line, text});
    }

    std::vector<std::string> tables;
    storedTables(function.body, tables);
    if (const Node* check = tableCheck(function.body, tables)) {
        r.memoized = true;
        r.reasons.push_back({check->line, "memoized: the condition " + tokensToText(check->cond) +
                                              " returns a stored result early"});
    }
    return r;
}

RecursionResult solveRecurrence(const Recurrence& r, const Complexity& work, int line) {
    RecursionResult out;
    out.reasons = r.reasons;
    out.pattern = "Recursion";
    out.complexity = Complexity::unknown();

    if (!r.unsupported.empty()) {
        out.reasons.push_back({line, "no single recurrence: " + r.unsupported +
                                         ", so the result is Unknown"});
        return out;
    }
    if (work.isUnknown()) {
        out.reasons.push_back({line, "the non-recursive work f(n) is Unknown, so the result is "
                                     "Unknown"});
        return out;
    }
    if (work.kind != Complexity::Kind::Polynomial) {
        out.reasons.push_back({line, "f(n) = " + work.notation() + " is not polynomial; only "
                                     "polynomial f(n) is solved, so the result is Unknown"});
        return out;
    }

    const std::string f = work.notation();
    const std::string a = std::to_string(r.calls);
    const std::string amount = std::to_string(r.amount);
    if (r.inLoop) {
        out.complexity = Complexity::factorial();
        out.pattern = "Permutation Recursion";
        out.reasons.push_back({line, "T(n) = n*T(n - 1) + " + f + ": n * (n - 1) * ... * 1 calls "
                                     "(polynomial work per call does not change the class) -> "
                                     "O(n!)"});
    } else if (r.shrink == ShrinkKind::Subtract) {
        const std::string recurrence = "T(n) = " + (r.calls > 1 ? a + "*" : std::string()) +
                                       "T(n - " + amount + ") + " + f;
        if (r.calls == 1) {
            out.complexity = multiply(Complexity::linear(), work);
            out.pattern = "Linear Recursion";
            out.reasons.push_back({line, recurrence + ": about n / " + amount +
                                             " levels, each costing " + f + " -> " +
                                             out.complexity.notation()});
        } else {
            out.complexity = Complexity::exponential(r.calls);
            out.complexity.upperBound = true;
            out.pattern = "Exponential Recursion";
            std::string real;
            const bool differ = static_cast<int>(r.amounts.size()) == r.calls &&
                                std::count(r.amounts.begin(), r.amounts.end(), r.amount) !=
                                    static_cast<long>(r.amounts.size());
            if (differ) {
                real = "T(n) =";
                for (std::size_t i = 0; i < r.amounts.size(); ++i) {
                    real += (i == 0 ? " " : " + ") + std::string("T(n - ") +
                            std::to_string(r.amounts[i]) + ")";
                }
                real += " + " + f + " <= ";
            }
            const std::string written =
                differ ? real + recurrence.substr(std::string("T(n) = ").size()) : recurrence;
            out.reasons.push_back({line, written + ": each call makes " + a + " more, up to " + a +
                                             "^n calls -> " + out.complexity.notation()});
        }
    } else {
        const int b = r.amount;
        const int k = work.power;
        long long bk = 1;
        for (int i = 0; i < k && bk <= r.calls; ++i) bk *= b;
        const std::string recurrence = "T(n) = " + a + "*T(n / " + amount + ") + " + f;
        const std::string compare = "a = " + a + ", b^k = " + amount + "^" + std::to_string(k);
        out.pattern = "Divide and Conquer";
        if (r.calls < bk) {
            out.complexity = work;
            out.reasons.push_back({line, recurrence + ": " + compare + ", a < b^k, Master Theorem "
                                         "case 3: the work at the top dominates -> " + f});
        } else if (r.calls == bk) {
            out.complexity = Complexity::polynomial(k, work.logPower + 1);
            out.reasons.push_back({line, recurrence + ": " + compare + ", a = b^k, Master Theorem "
                                         "case 2: log n levels, each costing " + f + " -> " +
                                             out.complexity.notation()});
        } else {
            int e = 0;
            long long be = 1;
            while (be < r.calls) {
                be *= b;
                ++e;
            }
            out.complexity = Complexity::polynomial(e);
            std::string text = recurrence + ": " + compare + ", a > b^k, Master Theorem case 1: "
                                                             "the leaves dominate, n^log_" +
                               amount + "(" + a + ")";
            if (be == r.calls) {
                text += " = " + out.complexity.notation();
            } else {
                out.complexity.upperBound = true;
                text += " = n^" + formatExponent(std::log(r.calls) / std::log(b)) +
                        ", reported as " + out.complexity.notation() + " (upper bound)";
            }
            out.reasons.push_back({line, text});
        }
    }

    if (work.uncertain) out.complexity.uncertain = true;
    if (r.memoized) {
        out.complexity.uncertain = true;
        out.reasons.push_back({line, "memoized recursion: each value may be computed only once, "
                                     "so the real cost is probably lower (uncertain)"});
    }
    return out;
}

std::vector<std::string> mutualRecursionCycle(const Function& function,
                                              const std::vector<Function>& program) {
    std::vector<bool> visited(program.size(), false);
    std::vector<std::string> path{function.name};
    if (findPathBack(function, function, program, indexByName(program), visited, path)) return path;
    return {};
}

}
