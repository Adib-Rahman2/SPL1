#pragma once

#include "advice/Recommender.h"
#include "analysis/RuleEngine.h"
#include "features/FeatureVector.h"
#include "ml/Knn.h"
#include "structure/Ast.h"

#include <ostream>
#include <string>
#include <vector>

namespace codecomplex {

struct ReportOptions {
    bool explain = false;
    bool recommend = true;
};

struct KnnEstimate {
    bool ran = false;
    std::string unavailable;
    Prediction prediction;
    std::vector<std::string> neighbours;
};

std::string displayName(const Function& function);

void writeFileHeader(std::ostream& out, const std::string& path, int functionCount);

void writeFunctionReport(std::ostream& out, const Function& function,
                         const FeatureVector& features, const RuleResult& rules,
                         const KnnEstimate& knn,
                         const std::vector<Recommendation>& recommendations,
                         const ReportOptions& options);

void writeUnsupported(std::ostream& out, const std::vector<Node>& unsupported);

}
