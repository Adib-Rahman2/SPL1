#include "report/TextReport.h"

#include <iomanip>

namespace codecomplex {

namespace {

void row(std::ostream& out, const std::string& label, const std::string& value) {
    out << "    " << std::left << std::setw(18) << label << value << '\n';
}

std::string yesNo(bool value) { return value ? "Yes" : "No"; }

// The first faster class found by the rule patterns. KNN-only advice is left out,
// because its class is not checked against this code.
std::string bestPossible(const RuleResult& rules,
                         const std::vector<Recommendation>& recommendations) {
    if (rules.complexity.isUnknown()) return "Unknown";
    for (const Recommendation& r : recommendations) {
        if (r.source == AdviceSource::KnnOnly || r.improved.empty()) continue;
        std::string text = r.improved + "  (" + r.title;
        if (!r.improvedNote.empty()) text += "; " + r.improvedNote;
        return text + ")";
    }
    return "no faster version found";
}

void adviceRow(std::ostream& out, const std::string& label, const std::string& value) {
    out << "       " << std::left << std::setw(13) << label << value << '\n';
}

}

std::string displayName(const Function& function) {
    if (function.className.empty()) return function.name + "()";
    return function.className + "::" + function.name + "()";
}

void writeFileHeader(std::ostream& out, const std::string& path, int functionCount) {
    out << "File: " << path << '\n'
        << "Functions found: " << functionCount << '\n';
}

void writeFunctionReport(std::ostream& out, const Function& function,
                         const FeatureVector& features, const RuleResult& rules,
                         const KnnEstimate& knn,
                         const std::vector<Recommendation>& recommendations,
                         const ReportOptions& options) {
    out << "\nFunction: " << displayName(function) << "   [lines " << function.line << '-'
        << function.endLine << "]\n";

    out << "  Features\n";
    row(out, "Loop Count", std::to_string(features.loopCount));
    row(out, "Nesting Depth", std::to_string(features.maxNestingDepth));
    if (!features.hasRecursion && rules.hasRecursion) {
        row(out, "Recursion", "Mutual (no direct self-call)");
    } else {
        row(out, "Recursion", yesNo(features.hasRecursion));
    }
    row(out, "Log Pattern", yesNo(features.hasLogPattern));
    row(out, "Function Calls", std::to_string(features.functionCallCount));
    if (options.explain) {
        row(out, "Lines of Code", std::to_string(features.linesOfCode));
        row(out, "Linear Loops", std::to_string(features.linearLoopCount));
        row(out, "Log Loops", std::to_string(features.logLoopCount));
    }

    out << "  Complexity\n";
    row(out, "Detected Pattern", rules.pattern);
    std::string estimate = rules.complexity.notation();
    const Prediction& vote = knn.prediction;
    if (rules.complexity.isUnknown() && knn.ran && vote.ok && vote.k >= 3 &&
        vote.votes == vote.k) {
        estimate += " (KNN suggests " + vote.label + ": all " + std::to_string(vote.k) +
                    " neighbours agree)";
    }
    row(out, "Estimated Time", estimate);
    row(out, "Best Possible", bestPossible(rules, recommendations));
    if (!knn.ran) {
        row(out, "KNN Estimate", knn.unavailable);
    } else if (!knn.prediction.ok) {
        row(out, "KNN Estimate", knn.prediction.note);
    } else {
        const Prediction& p = knn.prediction;
        std::string text = p.label + "  (differs from the rules)";
        if (p.label == rules.complexity.notation()) {
            text = p.label + "  (" + std::to_string(p.votes) + " of " + std::to_string(p.k) +
                   " neighbours, " +
                   std::to_string(static_cast<int>(p.confidence * 100 + 0.5)) +
                   "%; agrees with the rules)";
        }
        row(out, "KNN Estimate", text);
        if (!p.note.empty()) row(out, "", p.note);
        if (options.explain) {
            for (const std::string& line : knn.neighbours) row(out, "  neighbour", line);
        }
    }

    out << "  Explanation\n";
    if (rules.reasons.empty()) out << "    (no reasons recorded)\n";
    for (const Reason& reason : rules.reasons) {
        out << "    - ";
        if (options.explain) out << "line " << reason.line << ": ";
        out << reason.text << '\n';
    }

    if (!options.recommend) return;
    out << "  Recommendation\n";
    if (recommendations.empty()) out << "    No inefficient pattern found.\n";
    for (std::size_t i = 0; i < recommendations.size(); ++i) {
        const Recommendation& r = recommendations[i];
        out << "    " << i + 1 << ". " << r.title << "   [";
        if (r.line > 0) out << "line " << r.line << "; ";
        out << sourceName(r.source) << ", " << confidenceName(r.source) << " confidence]\n";
        adviceRow(out, "Found", r.found);
        adviceRow(out, "Why slow", r.whySlow);
        adviceRow(out, "Alternative", r.alternative);
        adviceRow(out, "Needs", r.prerequisites);
        adviceRow(out, "Trade-offs", r.tradeOffs);
        std::string improved = r.improved.empty() ? "-" : r.improved;
        if (!r.improvedNote.empty()) improved += "  (" + r.improvedNote + ")";
        adviceRow(out, "Improved", improved);
        adviceRow(out, "KNN", r.knnNote);
    }
}

void writeUnsupported(std::ostream& out, const std::vector<Node>& unsupported) {
    if (unsupported.empty()) return;
    out << "\nNot analysed (outside any function):\n";
    for (const Node& node : unsupported) {
        out << "    - line " << node.line << ": " << node.reason << '\n';
    }
}

}
