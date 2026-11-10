#include "ml/Dataset.h"

#include "analysis/Program.h"
#include "lexer/Lexer.h"
#include "source/Language.h"
#include "source/SourceFile.h"
#include "structure/Parser.h"

#include <algorithm>
#include <fstream>

namespace codecomplex {

namespace {

const char* const kHeader = "file,source,complexity,best_complexity,inefficiency,reviewed";

std::string trim(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) return "";
    const std::size_t last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

std::string folderOf(const std::string& path) {
    const std::size_t cut = path.find_last_of("/\\");
    return cut == std::string::npos ? "" : path.substr(0, cut + 1);
}

bool fileExists(const std::string& path) {
    std::ifstream in(path);
    return in.good();
}

std::string checkRow(const std::vector<std::string>& fields, LabelRow& row) {
    if (fields.size() != 6) {
        return "expected 6 fields, found " + std::to_string(fields.size());
    }
    row.file = fields[0];
    row.source = fields[1];
    row.complexity = fields[2];
    row.bestComplexity = fields[3];
    row.inefficiency = fields[4];
    if (row.file.empty()) return "the file name is empty";
    if (row.source != "corcod" && row.source != "cpp") {
        return "source must be corcod or cpp, found '" + row.source + "'";
    }
    if (!isComplexityLabel(row.complexity)) {
        return "unknown complexity '" + row.complexity + "'";
    }
    if (!row.bestComplexity.empty() && !isComplexityLabel(row.bestComplexity)) {
        return "unknown best_complexity '" + row.bestComplexity + "'";
    }
    if (row.bestComplexity.empty() != row.inefficiency.empty()) {
        return "best_complexity and inefficiency must be given together";
    }
    if (fields[5] != "yes" && fields[5] != "no") {
        return "reviewed must be yes or no, found '" + fields[5] + "'";
    }
    row.reviewed = fields[5] == "yes";
    return "";
}

}

const std::vector<std::string>& complexityLabels() {
    static const std::vector<std::string> labels = {
        "O(1)", "O(log n)", "O(n)", "O(n log n)", "O(n^2)", "O(n^3)", "O(2^n)", "O(n!)"};
    return labels;
}

bool isComplexityLabel(const std::string& text) {
    const std::vector<std::string>& labels = complexityLabels();
    return std::find(labels.begin(), labels.end(), text) != labels.end();
}

bool splitCsvLine(const std::string& line, std::vector<std::string>& fields, std::string& error) {
    fields.clear();
    std::string field;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                field += '"';
                ++i;
            } else if (c == '"') {
                quoted = false;
            } else {
                field += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            fields.push_back(trim(field));
            field.clear();
        } else {
            field += c;
        }
    }
    if (quoted) {
        error = "a quoted field is not closed";
        return false;
    }
    fields.push_back(trim(field));
    return true;
}

LabelFile readLabelFile(const std::string& path) {
    LabelFile result;
    std::ifstream in(path);
    if (!in) return result;
    result.opened = true;

    const std::string folder = folderOf(path);
    std::string line;
    int lineNumber = 0;
    bool sawHeader = false;
    while (std::getline(in, line)) {
        ++lineNumber;
        if (trim(line).empty()) continue;
        if (!sawHeader) {
            sawHeader = true;
            if (trim(line) != kHeader) {
                result.errors.push_back(path + ":" + std::to_string(lineNumber) +
                                        ": the header must be " + kHeader);
                return result;
            }
            continue;
        }
        std::vector<std::string> fields;
        std::string error;
        LabelRow row;
        row.line = lineNumber;
        if (splitCsvLine(line, fields, error)) error = checkRow(fields, row);
        if (!error.empty()) {
            result.errors.push_back(path + ":" + std::to_string(lineNumber) + ": " + error);
            continue;
        }
        row.path = folder + row.file;
        result.rows.push_back(row);
    }
    if (!sawHeader) result.errors.push_back(path + ": the file is empty");
    return result;
}

std::vector<std::string> defaultLabelFiles(const std::string& dataDir) {
    std::string folder = dataDir;
    if (!folder.empty() && folder.back() != '/' && folder.back() != '\\') folder += '/';
    std::vector<std::string> files;
    for (const char* name : {"corcod/labels.csv", "cpp/labels.csv"}) {
        if (fileExists(folder + name)) files.push_back(folder + name);
    }
    return files;
}

Dataset loadDataset(const std::vector<std::string>& labelFiles) {
    Dataset dataset;
    for (const std::string& labelPath : labelFiles) {
        const LabelFile labels = readLabelFile(labelPath);
        if (!labels.opened) {
            dataset.errors.push_back(labelPath + ": cannot open the label file");
            continue;
        }
        dataset.errors.insert(dataset.errors.end(), labels.errors.begin(), labels.errors.end());

        int unreviewed = 0;
        for (const LabelRow& row : labels.rows) {
            if (!row.reviewed) {
                ++unreviewed;
                continue;
            }
            const std::string where = labelPath + ":" + std::to_string(row.line) + ": ";
            const SourceLoadResult loaded = loadSourceFile(row.path);
            if (!loaded.ok) {
                dataset.errors.push_back(where + loaded.error);
                continue;
            }
            const Language language = languageForPath(row.path);
            const LexResult lexed = tokenize(loaded.file.text, language);
            if (!lexed.ok()) {
                dataset.errors.push_back(where + row.file + ":" +
                                         std::to_string(lexed.errors[0].line) + ": " +
                                         lexed.errors[0].message);
                continue;
            }
            const ParseResult parsed = parse(lexed.tokens, language);
            const ProgramAnalysis program = analyzeProgram(parsed, lexed.tokens);

            Example example;
            example.file = row.path;
            example.source = row.source;
            example.complexity = row.complexity;
            example.bestComplexity = row.bestComplexity;
            example.inefficiency = row.inefficiency;
            example.features = program.features;
            example.ruleComplexity = program.complexity;
            example.hasUnsupported = program.hasUnsupported;
            dataset.examples.push_back(example);
        }
        if (unreviewed > 0) {
            dataset.warnings.push_back(labelPath + ": " + std::to_string(unreviewed) +
                                       " row(s) not reviewed yet (reviewed = no) were skipped");
        }
    }
    return dataset;
}

}
