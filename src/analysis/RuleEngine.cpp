#include "analysis/RuleEngine.h"

#include "analysis/LibraryCosts.h"
#include "analysis/Recurrence.h"
#include "features/FeatureExtractor.h"

#include <algorithm>
#include <map>
#include <set>

namespace codecomplex {

namespace {

std::string loopWord(const Node& loop) {
    switch (loop.loopKind) {
    case LoopKind::For:      return "for loop";
    case LoopKind::RangeFor: return "range-for loop";
    case LoopKind::While:    return "while loop";
    case LoopKind::DoWhile:  return "do-while loop";
    }
    return "loop";
}

bool mentionsName(const std::vector<Token>& tokens, const std::string& name) {
    for (const Token& token : tokens) {
        if (token.kind == TokenKind::Identifier && token.text == name) return true;
    }
    return false;
}

bool isPositiveLiteral(const std::string& text) {
    if (text.empty()) return false;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
    }
    return text.find_first_not_of('0') != std::string::npos;
}

std::string addedName(const std::vector<Token>& step, const std::string& variable) {
    const auto is = [&](std::size_t i, const std::string& text) {
        return i < step.size() && step[i].text == text;
    };
    const auto name = [&](std::size_t i) {
        return i < step.size() && step[i].kind == TokenKind::Identifier;
    };
    if (step.size() == 3 && is(0, variable) && is(1, "+=") && name(2)) return step[2].text;
    if (step.size() == 5 && is(0, variable) && is(1, "=") && is(3, "+")) {
        if (is(2, variable) && name(4)) return step[4].text;
        if (is(4, variable) && name(2)) return step[2].text;
    }
    return "";
}

void directLoops(const Node& node, std::vector<const Node*>& loops, bool& hasCalls) {
    if (node.kind == NodeKind::Loop) {
        loops.push_back(&node);
        return;
    }
    if (!node.calls.empty()) hasCalls = true;
    for (const Node& child : node.children) directLoops(child, loops, hasCalls);
}

class Analyzer {
public:
    Analyzer(const Function& function, const std::vector<Function>& program, RuleCache& cache)
        : function_(function), program_(program), cache_(cache), inputs_(inputNames(function)),
          containers_(findContainers(function)) {}

    RuleResult run();

private:
    Complexity nodeCost(const Node& node, int depth);
    Complexity blockCost(const Node& block, int depth);
    Complexity ifCost(const Node& node, int depth);
    Complexity loopCost(const Node& loop, int depth);
    Complexity iterations(const Node& loop, const LoopFacts& facts);
    Complexity callsCost(const std::vector<Call>& calls);
    std::string patternName(const Complexity& total) const;
    bool definedInFile(const std::string& callee) const;
    const Function* calledFunction(const Call& call) const;
    Complexity costOf(const Function& callee);
    const LoopFacts* harmonicOuter(const Node& loop, const LoopFacts& facts) const;
    bool geometricBody(const Node& loop, const LoopFacts& facts) const;

    void addReason(int line, const std::string& text) { result_.reasons.push_back({line, text}); }

    const Function& function_;
    const std::vector<Function>& program_;
    RuleCache& cache_;
    bool inCycle_ = false;
    std::vector<std::string> inputs_;
    std::map<std::string, ContainerKind> containers_;
    std::set<std::string> reportedCalls_;
    std::vector<LoopFacts> enclosing_;
    RuleResult result_;
    int loops_ = 0;
    int sizedLoops_ = 0;
    int maxSizedDepth_ = 0;
    bool harmonic_ = false;
};

RuleResult Analyzer::run() {
    const std::vector<std::string> cycle = mutualRecursionCycle(function_, program_);
    inCycle_ = !cycle.empty();
    const Complexity work = nodeCost(function_.body, 0);
    result_.nonRecursiveWork = work;

    if (!cycle.empty()) {
        std::string path;
        for (const std::string& name : cycle) path += (path.empty() ? "" : " -> ") + name + "()";
        result_.hasRecursion = true;
        result_.complexity = Complexity::unknown();
        result_.pattern = "Mutual Recursion";
        addReason(function_.line, "mutual recursion " + path + " is not solved, so the result "
                                                           "is Unknown");
        return result_;
    }
    if (result_.hasRecursion) {
        addReason(function_.line, "the non-recursive work f(n) is " + work.toString() +
                                      " (recursive calls counted as O(1))");
        const RecursionResult solved =
            solveRecurrence(extractRecurrence(function_), work, function_.line);
        result_.reasons.insert(result_.reasons.end(), solved.reasons.begin(),
                               solved.reasons.end());
        result_.complexity = solved.complexity;
        result_.pattern = solved.pattern;
        return result_;
    }
    if (loops_ == 0 && !work.isUnknown()) {
        addReason(function_.line, work.isConstant()
                                      ? "no loops: every statement runs at most once -> " +
                                            work.notation()
                                      : "no loops: the cost comes from the calls -> " +
                                            work.notation());
    }
    result_.complexity = work;
    result_.pattern = patternName(work);
    return result_;
}

Complexity Analyzer::nodeCost(const Node& node, int depth) {
    switch (node.kind) {
    case NodeKind::Block:
        return blockCost(node, depth);
    case NodeKind::Statement:
        return callsCost(node.calls);
    case NodeKind::Loop:
        return loopCost(node, depth);
    case NodeKind::If:
        return ifCost(node, depth);
    case NodeKind::Branch:
    case NodeKind::Switch: {
        Complexity cost = callsCost(node.calls);
        if (!node.children.empty()) cost = add(cost, nodeCost(node.children[0], depth));
        return cost;
    }
    case NodeKind::Unsupported:
        addReason(node.line, "unsupported code (" + node.reason + "): its cost is unknown");
        return Complexity::unknown();
    }
    return Complexity::unknown();
}

Complexity Analyzer::blockCost(const Node& block, int depth) {
    Complexity total;
    std::vector<Complexity> growing;
    for (const Node& child : block.children) {
        const Complexity cost = nodeCost(child, depth);
        if (!cost.isConstant()) growing.push_back(cost);
        total = add(total, cost);
    }
    if (growing.size() >= 2) {
        std::string text = "in sequence: ";
        for (std::size_t i = 0; i < growing.size(); ++i) {
            if (i > 0) text += " + ";
            text += growing[i].notation();
        }
        addReason(block.line, text + " = " + total.notation() +
                                  (total.isUnknown() ? " (an unknown part makes the total unknown)"
                                                     : ""));
    }
    return total;
}

Complexity Analyzer::ifCost(const Node& node, int depth) {
    Complexity total;
    bool anyGrowing = false;
    for (const Node& branch : node.children) {
        const Complexity cost = nodeCost(branch, depth);
        if (!cost.isConstant()) anyGrowing = true;
        total = add(total, cost);
    }
    if (anyGrowing && node.children.size() > 1) {
        addReason(node.line, "if/else: the most expensive branch counts -> " + total.notation());
    }
    return total;
}

Complexity Analyzer::loopCost(const Node& loop, int depth) {
    ++loops_;
    const LoopFacts facts = analyzeLoop(loop, inputs_);
    const Complexity count = iterations(loop, facts);
    const bool sized = !count.isConstant();
    if (sized) {
        ++sizedLoops_;
        maxSizedDepth_ = std::max(maxSizedDepth_, depth + 1);
    }

    const bool geometric = geometricBody(loop, facts);
    const bool indexGrowsWithInput = loop.loopKind != LoopKind::RangeFor &&
                                     (facts.step == StepKind::Additive || geometric) &&
                                     facts.bound == BoundKind::Input && !facts.variable.empty();
    if (indexGrowsWithInput) inputs_.push_back(facts.variable);

    Complexity body = callsCost(loop.calls);
    if (!loop.children.empty()) {
        enclosing_.push_back(facts);
        body = add(body, nodeCost(loop.children[0], sized ? depth + 1 : depth));
        enclosing_.pop_back();
    }
    if (indexGrowsWithInput) inputs_.pop_back();

    if (geometric && !body.isConstant()) {
        const Complexity total = add(count, body);
        addReason(loop.line, "geometric series: " + facts.variable +
                                 " changes by a constant factor, so the loops up to " +
                                 facts.variable + " run 1 + 2 + 4 + ... + n, about 2n times in "
                                 "all, not " + count.notation() + " x " + body.notation() +
                                 " -> " + total.notation());
        return total;
    }
    const Complexity total = multiply(count, body);
    if (!body.isConstant()) {
        addReason(loop.line, "nested: " + count.notation() + " iterations x " + body.notation() +
                                 " per iteration = " + total.notation());
    }
    return total;
}

Complexity Analyzer::iterations(const Node& loop, const LoopFacts& facts) {
    std::string head = loopWord(loop);
    if (!facts.variable.empty()) head += " on " + facts.variable;

    const LoopFacts* outer = harmonicOuter(loop, facts);
    if (outer != nullptr) {
        harmonic_ = true;
        addReason(loop.line, head + ": step " + facts.stepText + " adds the outer loop variable " +
                                 outer->variable + ", which counts up from " + outer->start +
                                 "; over all " + outer->variable +
                                 " it runs n/1 + n/2 + ... + n/n times, "
                                 "about n log n in all (harmonic series) -> O(log n) iterations "
                                 "on average");
        return Complexity::logarithmic();
    }
    if (facts.step == StepKind::Other || facts.bound == BoundKind::Unknown) {
        std::string why = facts.note;
        if (why.empty()) why = "its step or bound was not recognised";
        addReason(loop.line, head + ": " + why + ", so the number of iterations is unknown");
        return Complexity::unknown();
    }

    std::string range = loop.loopKind == LoopKind::RangeFor
                            ? "over " + facts.limit
                            : (facts.start.empty() ? "up to " : "from " + facts.start + " to ") +
                                  facts.limit;
    if (loop.loopKind != LoopKind::RangeFor && !facts.note.empty()) range = facts.note;

    Complexity count;
    if (facts.bound == BoundKind::Constant) {
        addReason(loop.line, head + ": runs a constant number of times (" + range + ") -> O(1)");
    } else {
        const bool additive = facts.step == StepKind::Additive;
        count = additive ? Complexity::linear() : Complexity::logarithmic();
        std::string text = head + ": ";
        if (loop.loopKind == LoopKind::RangeFor) {
            text += "one element per iteration " + range;
        } else if (!facts.note.empty()) {
            text += range;
        } else {
            text += "step " + facts.stepText +
                    (additive ? " adds a constant, " : " multiplies or divides by a constant, ") +
                    range;
        }
        if (facts.bound == BoundKind::Input) {
            text += " (input size)";
        } else {
            count.uncertain = true;
            text += " (not traced to the input size; assumed to grow with it, uncertain)";
        }
        addReason(loop.line, text + " -> " + count.notation() + " iterations");
    }

    if (facts.modifiedInBody) {
        count.uncertain = true;
        addReason(loop.line, facts.variable + " is also changed inside the loop body, "
                                              "so the count may differ (uncertain)");
    }
    return count;
}

Complexity Analyzer::callsCost(const std::vector<Call>& calls) {
    Complexity cost;
    for (const Call& call : calls) {
        if (isRecursiveCall(call, function_)) {
            if (!result_.hasRecursion) {
                addReason(call.line, "recursive call to " + call.callee + "()");
            }
            result_.hasRecursion = true;
            continue;
        }
        if (const Function* callee = inCycle_ ? nullptr : calledFunction(call)) {
            if (cache_.inProgress.count(callee) == 0) {
                Complexity callCost = costOf(*callee);
                const bool first = reportedCalls_.insert(call.callee).second;
                if (callCost.isUnknown()) {
                    if (first) {
                        addReason(call.line, "call to " + call.callee + "() has an Unknown cost");
                    }
                } else if (callCost.isConstant()) {
                    if (first) {
                        addReason(call.line, "call to " + call.callee + "() costs " +
                                                 callCost.notation());
                    }
                } else {
                    callCost.uncertain = true;
                    if (first) {
                        addReason(call.line, "call to " + call.callee + "() costs " +
                                                 callCost.notation());
                    }
                }
                cost = add(cost, callCost);
                continue;
            }
        }
        const LibraryCost library =
            definedInFile(call.callee) ? LibraryCost() : libraryCost(call, containers_, inputs_);
        if (!library.found) {
            cost.uncertain = true;
            if (reportedCalls_.insert(call.callee).second) {
                addReason(call.line, "call to " + call.callee + "() is assumed O(1); the cost of "
                                     "called functions is not analysed");
            }
            continue;
        }
        if (library.cost.isConstant()) {
            if (library.what.find("hash") != std::string::npos &&
                reportedCalls_.insert(call.callee).second) {
                addReason(call.line, "call to " + call.callee + "() is " + library.what +
                                         " -> O(1)");
            }
            continue;
        }
        Complexity callCost = library.cost;
        std::string text = "call to " + call.callee + "() is " + library.what + " -> " +
                           callCost.notation() + " (library-cost table)";
        if (!library.tracedToInput) {
            callCost.uncertain = true;
            text += "; its size is not traced to the input size (uncertain)";
        }
        addReason(call.line, text);
        cost = add(cost, callCost);
    }
    return cost;
}

bool Analyzer::definedInFile(const std::string& callee) const {
    const std::size_t cut = callee.find_last_of(".:>");
    const std::string base = cut == std::string::npos ? callee : callee.substr(cut + 1);
    for (const Function& other : program_) {
        if (other.name == base) return true;
    }
    return false;
}

const Function* Analyzer::calledFunction(const Call& call) const {
    std::string callee = call.callee;
    if (callee.rfind("this->", 0) == 0) callee = callee.substr(6);
    if (callee.find('.') != std::string::npos || callee.find("->") != std::string::npos) {
        return nullptr;
    }
    std::string qualifier;
    const std::size_t cut = callee.rfind("::");
    if (cut != std::string::npos) {
        qualifier = callee.substr(0, cut);
        callee = callee.substr(cut + 2);
    }
    const Function* found = nullptr;
    int matches = 0;
    for (const Function& other : program_) {
        if (other.name != callee) continue;
        if (!qualifier.empty() && other.className != qualifier) continue;
        found = &other;
        ++matches;
    }
    return matches == 1 ? found : nullptr;
}

Complexity Analyzer::costOf(const Function& callee) {
    const auto known = cache_.costs.find(&callee);
    if (known != cache_.costs.end()) return known->second;
    return analyzeFunction(callee, program_, &cache_).complexity;
}

const LoopFacts* Analyzer::harmonicOuter(const Node& loop, const LoopFacts& facts) const {
    if (loop.loopKind != LoopKind::For || facts.step != StepKind::Other ||
        facts.bound != BoundKind::Input || facts.variable.empty()) {
        return nullptr;
    }
    const std::string added = addedName(loop.step, facts.variable);
    if (added.empty() || added == facts.variable || mentionsName(loop.cond, added)) return nullptr;
    for (const LoopFacts& outer : enclosing_) {
        if (outer.variable == added && outer.step == StepKind::Additive &&
            outer.bound == BoundKind::Input && isPositiveLiteral(outer.start)) {
            return &outer;
        }
    }
    return nullptr;
}

bool Analyzer::geometricBody(const Node& loop, const LoopFacts& facts) const {
    if (loop.loopKind == LoopKind::RangeFor || facts.step != StepKind::Multiplicative ||
        facts.bound != BoundKind::Input || facts.variable.empty() ||
        facts.variable.find(',') != std::string::npos || facts.modifiedInBody ||
        loop.children.empty()) {
        return false;
    }
    std::vector<const Node*> inner;
    bool hasCalls = false;
    directLoops(loop.children[0], inner, hasCalls);
    if (inner.empty() || hasCalls) return false;
    for (const Node* child : inner) {
        const LoopFacts f = analyzeLoop(*child, inputs_);
        const bool constantStart = f.start.empty() || (f.start.find_first_not_of("0123456789") ==
                                                       std::string::npos);
        if (child->loopKind == LoopKind::RangeFor || f.step != StepKind::Additive ||
            f.limit != facts.variable || !constantStart || f.modifiedInBody ||
            !child->calls.empty()) {
            return false;
        }
    }
    return true;
}

std::string Analyzer::patternName(const Complexity& total) const {
    if (total.isUnknown()) return "Unknown";
    if (sizedLoops_ == 0 && !total.isConstant()) return "Cost of Called Functions";
    if (sizedLoops_ == 0) return loops_ > 0 ? "Constant-Bound Loops" : "No Loops";
    if (maxSizedDepth_ == 1) {
        if (sizedLoops_ > 1) return "Sequential Loops";
        return total.power == 0 ? "Logarithmic Loop" : "Single Loop";
    }
    if (harmonic_) return "Harmonic Nested Loops";
    if (total.power == 0) return "Nested Logarithmic Loops";
    if (total.logPower > 0) return "Nested Loops with a Logarithmic Loop";
    return "Nested Loops";
}

}

RuleResult analyzeFunction(const Function& function, const std::vector<Function>& program,
                           RuleCache* cache) {
    RuleCache local;
    RuleCache& shared = cache != nullptr ? *cache : local;
    shared.inProgress.insert(&function);
    RuleResult result = Analyzer(function, program, shared).run();
    shared.inProgress.erase(&function);
    shared.costs[&function] = result.complexity;
    return result;
}

}
