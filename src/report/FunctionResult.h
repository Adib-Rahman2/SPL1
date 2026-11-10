#pragma once

#include "advice/Recommender.h"
#include "analysis/RuleEngine.h"
#include "features/FeatureVector.h"
#include "report/TextReport.h"

#include <string>
#include <vector>

namespace codecomplex {

struct FunctionResult {
    std::string file;
    std::string name;
    int startLine = 0;
    int endLine = 0;
    FeatureVector features;
    RuleResult rules;
    KnnEstimate knn;
    std::vector<Recommendation> recommendations;
};

inline std::string firstImprovedComplexity(const FunctionResult& result) {
    for (const Recommendation& r : result.recommendations) {
        if (!r.improved.empty()) return r.improved;
    }
    return "";
}

}
