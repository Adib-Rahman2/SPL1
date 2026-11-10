#pragma once

#include "analysis/RuleEngine.h"
#include "structure/Ast.h"

#include <string>
#include <vector>

namespace codecomplex {

enum class PatternKind {
    LinearSearchInLoop,
    FindInLoop,
    LoopInvariantCall,
    RepeatedCall,
    OverlappingRecursion,
    QuadraticOrdering,
    PrefixRecomputation
};

const char* patternKindName(PatternKind kind);

struct DetectedPattern {
    PatternKind kind = PatternKind::LinearSearchInLoop;
    int line = 0;
    std::string detail;
};

std::vector<DetectedPattern> detectPatterns(const Function& function, const RuleResult& rules);

}
