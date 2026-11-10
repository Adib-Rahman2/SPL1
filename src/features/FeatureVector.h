#pragma once

#include <string>
#include <vector>

namespace codecomplex {

struct FeatureVector {

    int loopCount = 0;

    int maxNestingDepth = 0;

    bool hasRecursion = false;

    bool hasLogPattern = false;

    int functionCallCount = 0;

    int linesOfCode = 0;

    int linearLoopCount = 0;

    int logLoopCount = 0;

    int sortCallCount = 0;

    static std::vector<std::string> names() {
        return {"loop_count", "max_nesting_depth", "recursion", "log_pattern",
                "function_calls", "lines_of_code", "linear_loops", "log_loops", "sort_calls"};
    }

    std::vector<double> values() const {
        return {double(loopCount), double(maxNestingDepth), hasRecursion ? 1.0 : 0.0,
                hasLogPattern ? 1.0 : 0.0, double(functionCallCount), double(linesOfCode),
                double(linearLoopCount), double(logLoopCount), double(sortCallCount)};
    }
};

}
