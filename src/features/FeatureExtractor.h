#pragma once

#include "features/FeatureVector.h"
#include "lexer/Token.h"
#include "structure/Ast.h"

#include <ostream>
#include <string>
#include <vector>

namespace codecomplex {

enum class StepKind {
    Additive,
    Multiplicative,
    Other
};

enum class BoundKind {
    Constant,
    Input,
    Variable,
    Unknown
};

const char* stepKindName(StepKind kind);
const char* boundKindName(BoundKind kind);
inline std::ostream& operator<<(std::ostream& out, StepKind kind) { return out << stepKindName(kind); }
inline std::ostream& operator<<(std::ostream& out, BoundKind kind) { return out << boundKindName(kind); }

struct LoopFacts {
    int line = 0;
    std::string variable;
    StepKind step = StepKind::Other;
    std::string stepText;
    BoundKind bound = BoundKind::Unknown;
    std::string start;
    std::string limit;
    std::string note;
    bool modifiedInBody = false;
};

std::vector<std::string> inputNames(const Function& function);

LoopFacts analyzeLoop(const Node& loop, const std::vector<std::string>& inputs);

bool isRecursiveCall(const Call& call, const Function& function);

bool isMidpointStatement(const std::vector<Token>& tokens);

std::string tokensToText(const std::vector<Token>& tokens);

FeatureVector extractFeatures(const Function& function, const std::vector<Token>& tokens);

}
