#pragma once

#include "analysis/Complexity.h"
#include "structure/Ast.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace codecomplex {

struct Reason {
    int line = 0;
    std::string text;
};

struct RuleResult {
    Complexity complexity;
    Complexity nonRecursiveWork;
    std::string pattern;
    std::vector<Reason> reasons;
    bool hasRecursion = false;
};

struct RuleCache {
    std::map<const Function*, Complexity> costs;
    std::set<const Function*> inProgress;
};

RuleResult analyzeFunction(const Function& function, const std::vector<Function>& program = {},
                           RuleCache* cache = nullptr);

}
