#pragma once

#include "analysis/Complexity.h"
#include "analysis/RuleEngine.h"
#include "structure/Ast.h"

#include <string>
#include <vector>

namespace codecomplex {

enum class ShrinkKind {
    Subtract,
    Divide,
    Other
};

struct Recurrence {
    int calls = 0;
    ShrinkKind shrink = ShrinkKind::Other;
    int amount = 0;
    std::vector<int> amounts;
    bool inLoop = false;
    bool memoized = false;
    std::string unsupported;
    std::vector<Reason> reasons;
};

struct RecursionResult {
    Complexity complexity;
    std::string pattern;
    std::vector<Reason> reasons;
};

Recurrence extractRecurrence(const Function& function);

RecursionResult solveRecurrence(const Recurrence& recurrence, const Complexity& work, int line);

std::vector<std::string> mutualRecursionCycle(const Function& function,
                                              const std::vector<Function>& program);

}
