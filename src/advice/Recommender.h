#pragma once

#include "advice/PatternDetector.h"
#include "analysis/RuleEngine.h"
#include "ml/Knn.h"

#include <string>
#include <vector>

namespace codecomplex {

enum class AdviceSource { Agree, RulesOnly, KnnOnly };

struct Recommendation {
    std::string title;
    int line = 0;
    std::string found;
    std::string whySlow;
    std::string alternative;
    std::string prerequisites;
    std::string tradeOffs;
    std::string improved;
    std::string improvedNote;
    AdviceSource source = AdviceSource::RulesOnly;
    std::string knnNote;
};

const char* confidenceName(AdviceSource source);
const char* sourceName(AdviceSource source);

struct ImprovementVote {
    bool ran = false;
    Prediction prediction;
    std::string note;
};

bool splitImprovementLabel(const std::string& label, std::string& inefficiency, std::string& best);

bool patternMatchesInefficiency(PatternKind kind, const std::string& inefficiency);

std::vector<Recommendation> recommend(const RuleResult& rules,
                                      const std::vector<DetectedPattern>& patterns,
                                      const ImprovementVote& knn);

}
