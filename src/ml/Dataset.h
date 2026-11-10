#pragma once

#include "analysis/Complexity.h"
#include "features/FeatureVector.h"

#include <string>
#include <vector>

namespace codecomplex {

const std::vector<std::string>& complexityLabels();
bool isComplexityLabel(const std::string& text);

struct LabelRow {
    int line = 0;
    std::string file;
    std::string path;
    std::string source;
    std::string complexity;
    std::string bestComplexity;
    std::string inefficiency;
    bool reviewed = false;
};

struct LabelFile {
    bool opened = false;
    std::vector<LabelRow> rows;
    std::vector<std::string> errors;
};

LabelFile readLabelFile(const std::string& path);

bool splitCsvLine(const std::string& line, std::vector<std::string>& fields, std::string& error);

struct Example {
    std::string file;
    std::string source;
    std::string complexity;
    std::string bestComplexity;
    std::string inefficiency;
    FeatureVector features;
    Complexity ruleComplexity;
    bool hasUnsupported = false;
};

struct Dataset {
    std::vector<Example> examples;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

std::vector<std::string> defaultLabelFiles(const std::string& dataDir);

Dataset loadDataset(const std::vector<std::string>& labelFiles);

}
