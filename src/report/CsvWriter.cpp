#include "report/CsvWriter.h"

#include <cstdio>
#include <fstream>

namespace codecomplex {

namespace {

std::string yesNo(bool value) { return value ? "yes" : "no"; }

std::string number(double value) {
    char text[32];
    std::snprintf(text, sizeof text, "%.2f", value);
    return text;
}

std::string featureText(double value) {
    char text[32];
    std::snprintf(text, sizeof text, "%g", value);
    return text;
}

void writeLine(std::ostream& out, const std::vector<std::string>& fields) {
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) out << ',';
        out << csvField(fields[i]);
    }
    out << '\n';
}

}

std::string csvField(const std::string& text) {
    const bool needsQuotes = text.find_first_of(",\"\n\r") != std::string::npos ||
                             (!text.empty() && (text.front() == ' ' || text.back() == ' '));
    if (!needsQuotes) return text;
    std::string quoted = "\"";
    for (char c : text) {
        if (c == '"') quoted += '"';
        quoted += c;
    }
    return quoted + "\"";
}

std::vector<std::string> csvHeader() {
    std::vector<std::string> header = {"file", "function", "start_line", "end_line"};
    for (const std::string& name : FeatureVector::names()) header.push_back(name);
    for (const char* name : {"pattern", "complexity", "upper_bound", "uncertain",
                             "knn_complexity", "knn_votes", "knn_k", "knn_confidence", "knn_note",
                             "recommendations", "improved_complexity", "recommendation_source",
                             "recommendation_titles"}) {
        header.push_back(name);
    }
    return header;
}

std::vector<std::string> csvRow(const FunctionResult& r) {
    std::vector<std::string> row = {r.file, r.name, std::to_string(r.startLine),
                                    std::to_string(r.endLine)};
    for (double value : r.features.values()) row.push_back(featureText(value));

    row.push_back(r.rules.pattern);
    row.push_back(r.rules.complexity.notation());
    row.push_back(yesNo(r.rules.complexity.upperBound));
    row.push_back(yesNo(r.rules.complexity.uncertain));

    const Prediction& p = r.knn.prediction;
    const bool voted = r.knn.ran && p.ok;
    row.push_back(voted ? p.label : "");
    row.push_back(voted ? std::to_string(p.votes) : "");
    row.push_back(voted ? std::to_string(p.k) : "");
    row.push_back(voted ? number(p.confidence) : "");
    row.push_back(!r.knn.ran ? r.knn.unavailable : p.note);

    row.push_back(std::to_string(r.recommendations.size()));
    row.push_back(firstImprovedComplexity(r));
    row.push_back(r.recommendations.empty() ? "" : sourceName(r.recommendations[0].source));
    std::string titles;
    for (const Recommendation& rec : r.recommendations) {
        titles += (titles.empty() ? "" : "; ") + rec.title;
    }
    row.push_back(titles);
    return row;
}

void writeCsv(std::ostream& out, const std::vector<FunctionResult>& results) {
    writeLine(out, csvHeader());
    for (const FunctionResult& result : results) writeLine(out, csvRow(result));
}

bool writeCsvFile(const std::string& path, const std::vector<FunctionResult>& results,
                  std::string& error) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        error = "cannot open '" + path + "' for writing (does the folder exist?)";
        return false;
    }
    writeCsv(out, results);
    out.flush();
    if (!out) {
        error = "writing '" + path + "' failed";
        return false;
    }
    return true;
}

}
