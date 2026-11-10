#include "analysis/Program.h"

#include "analysis/RuleEngine.h"
#include "features/FeatureExtractor.h"

#include <algorithm>

namespace codecomplex {

namespace {

std::string baseName(const std::string& callee) {
    const std::size_t cut = callee.find_last_of(".:>");
    return cut == std::string::npos ? callee : callee.substr(cut + 1);
}

void collectCalls(const Node& node, std::vector<std::string>& names) {
    for (const Call& call : node.calls) names.push_back(baseName(call.callee));
    for (const Node& child : node.children) collectCalls(child, names);
}

bool containsUnsupported(const Node& node) {
    if (node.kind == NodeKind::Unsupported) return true;
    for (const Node& child : node.children) {
        if (containsUnsupported(child)) return true;
    }
    return false;
}

void combine(FeatureVector& total, const FeatureVector& f) {
    total.loopCount += f.loopCount;
    total.maxNestingDepth = std::max(total.maxNestingDepth, f.maxNestingDepth);
    total.hasRecursion = total.hasRecursion || f.hasRecursion;
    total.hasLogPattern = total.hasLogPattern || f.hasLogPattern;
    total.functionCallCount += f.functionCallCount;
    total.linesOfCode += f.linesOfCode;
    total.linearLoopCount += f.linearLoopCount;
    total.logLoopCount += f.logLoopCount;
    total.sortCallCount += f.sortCallCount;
}

}

std::vector<std::size_t> reachableFromMain(const std::vector<Function>& functions) {
    std::vector<bool> reached(functions.size(), false);
    std::vector<std::size_t> work;
    for (std::size_t i = 0; i < functions.size(); ++i) {
        if (functions[i].name == "main") {
            reached[i] = true;
            work.push_back(i);
        }
    }
    if (work.empty()) std::fill(reached.begin(), reached.end(), true);

    while (!work.empty()) {
        const std::size_t current = work.back();
        work.pop_back();
        std::vector<std::string> called;
        collectCalls(functions[current].body, called);
        for (std::size_t i = 0; i < functions.size(); ++i) {
            if (reached[i]) continue;
            if (std::find(called.begin(), called.end(), functions[i].name) != called.end()) {
                reached[i] = true;
                work.push_back(i);
            }
        }
    }

    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < functions.size(); ++i) {
        if (reached[i]) result.push_back(i);
    }
    return result;
}

ProgramAnalysis analyzeProgram(const ParseResult& parsed, const std::vector<Token>& tokens) {
    ProgramAnalysis program;
    program.functions = reachableFromMain(parsed.functions);
    RuleCache cache;
    bool first = true;
    for (std::size_t index : program.functions) {
        const Function& function = parsed.functions[index];
        combine(program.features, extractFeatures(function, tokens));
        const Complexity cost = analyzeFunction(function, parsed.functions, &cache).complexity;
        program.complexity = first ? cost : add(program.complexity, cost);
        first = false;
        if (containsUnsupported(function.body)) program.hasUnsupported = true;
    }
    if (first) program.complexity = Complexity::unknown();
    return program;
}

}
