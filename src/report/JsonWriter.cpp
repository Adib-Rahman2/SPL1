#include "report/JsonWriter.h"

#include <cstdio>
#include <fstream>

namespace codecomplex {

namespace {

const char* boolean(bool value) { return value ? "true" : "false"; }

std::string number(double value) {
    char text[32];
    std::snprintf(text, sizeof text, "%.2f", value);
    return text;
}

std::string stringOrNull(const std::string& text) {
    return text.empty() ? "null" : jsonString(text);
}

void writeFunction(std::ostream& out, const FunctionResult& r) {
    out << "    {\n"
        << "      \"name\": " << jsonString(r.name) << ",\n"
        << "      \"start_line\": " << r.startLine << ",\n"
        << "      \"end_line\": " << r.endLine << ",\n";

    out << "      \"features\": {";
    const std::vector<std::string> names = FeatureVector::names();
    const std::vector<double> values = r.features.values();
    for (std::size_t i = 0; i < names.size(); ++i) {
        out << (i ? ", " : "") << jsonString(names[i]) << ": " << static_cast<long long>(values[i]);
    }
    out << "},\n";

    out << "      \"rules\": {\"pattern\": " << jsonString(r.rules.pattern)
        << ", \"complexity\": " << jsonString(r.rules.complexity.notation())
        << ", \"upper_bound\": " << boolean(r.rules.complexity.upperBound)
        << ", \"uncertain\": " << boolean(r.rules.complexity.uncertain) << ", \"reasons\": [";
    for (std::size_t i = 0; i < r.rules.reasons.size(); ++i) {
        out << (i ? ", " : "") << "{\"line\": " << r.rules.reasons[i].line
            << ", \"text\": " << jsonString(r.rules.reasons[i].text) << "}";
    }
    out << "]},\n";

    const Prediction& p = r.knn.prediction;
    if (r.knn.ran && p.ok) {
        out << "      \"knn\": {\"complexity\": " << jsonString(p.label) << ", \"votes\": " << p.votes
            << ", \"k\": " << p.k << ", \"confidence\": " << number(p.confidence)
            << ", \"note\": " << jsonString(p.note) << "},\n";
    } else {
        out << "      \"knn\": {\"complexity\": null, \"note\": "
            << jsonString(r.knn.ran ? p.note : r.knn.unavailable) << "},\n";
    }

    out << "      \"recommendations\": [";
    for (std::size_t i = 0; i < r.recommendations.size(); ++i) {
        const Recommendation& rec = r.recommendations[i];
        out << (i ? "," : "") << "\n        {"
            << "\"title\": " << jsonString(rec.title)
            << ", \"line\": " << rec.line
            << ", \"source\": " << jsonString(sourceName(rec.source))
            << ", \"confidence\": " << jsonString(confidenceName(rec.source))
            << ", \"found\": " << jsonString(rec.found)
            << ", \"why_slow\": " << jsonString(rec.whySlow)
            << ", \"alternative\": " << jsonString(rec.alternative)
            << ", \"prerequisites\": " << jsonString(rec.prerequisites)
            << ", \"trade_offs\": " << jsonString(rec.tradeOffs)
            << ", \"improved_complexity\": " << stringOrNull(rec.improved)
            << ", \"improved_note\": " << jsonString(rec.improvedNote)
            << ", \"knn\": " << jsonString(rec.knnNote) << "}";
    }
    out << (r.recommendations.empty() ? "" : "\n      ") << "]\n";
    out << "    }";
}

}

std::string jsonString(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char code[8];
                std::snprintf(code, sizeof code, "\\u%04x", static_cast<unsigned char>(c));
                out += code;
            } else {
                out += c;
            }
        }
    }
    return out + "\"";
}

void writeJson(std::ostream& out, const std::string& file, const std::vector<FunctionResult>& results) {
    out << "{\n  \"file\": " << jsonString(file) << ",\n  \"functions\": [";
    for (std::size_t i = 0; i < results.size(); ++i) {
        out << (i ? ",\n" : "\n");
        writeFunction(out, results[i]);
    }
    out << (results.empty() ? "" : "\n  ") << "]\n}\n";
}

bool writeJsonFile(const std::string& path, const std::string& file,
                   const std::vector<FunctionResult>& results, std::string& error) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        error = "cannot open '" + path + "' for writing (does the folder exist?)";
        return false;
    }
    writeJson(out, file, results);
    out.flush();
    if (!out) {
        error = "writing '" + path + "' failed";
        return false;
    }
    return true;
}

}
