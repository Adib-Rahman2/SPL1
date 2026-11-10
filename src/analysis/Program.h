#pragma once

#include "analysis/Complexity.h"
#include "features/FeatureVector.h"
#include "lexer/Token.h"
#include "structure/Ast.h"

#include <cstddef>
#include <string>
#include <vector>

namespace codecomplex {

struct ProgramAnalysis {
    std::vector<std::size_t> functions;
    FeatureVector features;
    Complexity complexity;
    bool hasUnsupported = false;
};

std::vector<std::size_t> reachableFromMain(const std::vector<Function>& functions);

ProgramAnalysis analyzeProgram(const ParseResult& parsed, const std::vector<Token>& tokens);

}
