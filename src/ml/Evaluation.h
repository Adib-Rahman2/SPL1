#pragma once

#include "ml/Dataset.h"
#include "ml/Knn.h"

#include <ostream>
#include <string>
#include <vector>

namespace codecomplex {

constexpr const char* kNoAnswer = "no answer";
constexpr const char* kOther = "other";

struct ClassMetrics {
    std::string label;
    int support = 0;
    int predicted = 0;
    int correct = 0;
    double precision = 0.0;
    double recall = 0.0;
    double f1 = 0.0;
};

struct Metrics {
    int total = 0;
    int correct = 0;
    int answered = 0;
    std::vector<std::string> rows;
    std::vector<std::string> columns;
    std::vector<std::vector<int>> confusion;
    std::vector<ClassMetrics> perClass;

    double accuracy() const { return total == 0 ? 0.0 : double(correct) / total; }
    double coverage() const { return total == 0 ? 0.0 : double(answered) / total; }
    double accuracyWhenAnswered() const { return answered == 0 ? 0.0 : double(correct) / answered; }
    double macroF1() const;
};

Metrics computeMetrics(const std::vector<std::string>& truth,
                       const std::vector<std::string>& predicted);

struct EvaluationOptions {
    KnnOptions knn;
    std::vector<int> kSweep = {1, 3, 5, 7, 9};
    std::vector<double> distanceSweep;
};

std::vector<std::vector<Neighbour>> leaveOneOutRanking(const std::vector<std::vector<double>>& rows,
                                                       const std::vector<std::string>& labels);

void writeEvaluation(std::ostream& out, const Dataset& dataset, const EvaluationOptions& options);

}
