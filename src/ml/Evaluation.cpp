#include "ml/Evaluation.h"

#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>

namespace codecomplex {

namespace {

constexpr int kReliableSupport = 10;

std::string percent(double value) {
    char text[16];
    std::snprintf(text, sizeof text, "%.1f%%", value * 100.0);
    return text;
}

std::string fixed2(double value) {
    char text[16];
    std::snprintf(text, sizeof text, "%.2f", value);
    return text;
}

std::string distanceText(double value) {
    if (value == std::numeric_limits<double>::infinity()) return "none";
    char text[16];
    std::snprintf(text, sizeof text, "%.2f", value);
    return text;
}

std::string column(const std::string& predicted) {
    if (predicted == kNoAnswer || predicted == "Unknown" || isComplexityLabel(predicted)) {
        return predicted;
    }
    return kOther;
}

int classOrder(const std::string& label) {
    const std::vector<std::string>& labels = complexityLabels();
    const auto at = std::find(labels.begin(), labels.end(), label);
    if (at != labels.end()) return static_cast<int>(at - labels.begin());
    if (label == "Unknown") return 100;
    if (label == kOther) return 101;
    return 102;
}

void sortByClass(std::vector<std::string>& labels) {
    std::sort(labels.begin(), labels.end(), [](const std::string& a, const std::string& b) {
        return classOrder(a) < classOrder(b);
    });
}

void cell(std::ostream& out, const std::string& text, int width) {
    out << std::left << std::setw(width) << text;
}

void writeMetrics(std::ostream& out, const std::string& title, const Metrics& m) {
    out << "\n  " << title << ": accuracy " << percent(m.accuracy()) << " (" << m.correct
        << " of " << m.total << "), macro F1 " << fixed2(m.macroF1());
    out << '\n';
    if (m.answered != m.total) {
        out << "    answered " << m.answered << " (" << percent(m.coverage())
            << "); accuracy when answered " << percent(m.accuracyWhenAnswered()) << '\n';
    }

    out << "    ";
    cell(out, "class", 12);
    cell(out, "examples", 10);
    cell(out, "precision", 11);
    cell(out, "recall", 8);
    out << "F1\n";
    for (const ClassMetrics& c : m.perClass) {
        out << "    ";
        cell(out, c.label, 12);
        cell(out, std::to_string(c.support), 10);
        cell(out, c.predicted == 0 ? "-" : fixed2(c.precision), 11);
        cell(out, fixed2(c.recall), 8);
        out << fixed2(c.f1);
        if (c.support < kReliableSupport) out << "   (fewer than 10 examples: not reliable)";
        out << '\n';
    }

    out << "    confusion (rows: label, columns: prediction)\n    ";
    cell(out, "", 12);
    for (const std::string& col : m.columns) cell(out, col, std::max<int>(11, col.size() + 2));
    out << '\n';
    for (std::size_t r = 0; r < m.rows.size(); ++r) {
        out << "    ";
        cell(out, m.rows[r], 12);
        for (std::size_t c = 0; c < m.columns.size(); ++c) {
            cell(out, std::to_string(m.confusion[r][c]),
                 std::max<int>(11, m.columns[c].size() + 2));
        }
        out << '\n';
    }
}

template <typename T>
std::vector<T> pick(const std::vector<T>& all, const std::vector<std::size_t>& indexes) {
    std::vector<T> out;
    for (std::size_t i : indexes) out.push_back(all[i]);
    return out;
}

std::vector<std::string> knnLabels(const std::vector<std::vector<Neighbour>>& ranked,
                                   const std::vector<std::string>& labels,
                                   const KnnOptions& options) {
    std::vector<std::string> predicted;
    for (const std::vector<Neighbour>& r : ranked) {
        const Prediction p = vote(r, labels, options);
        predicted.push_back(p.ok ? p.label : kNoAnswer);
    }
    return predicted;
}

std::string locBucket(int lines) {
    if (lines <= 20) return "1-20 lines";
    if (lines <= 50) return "21-50 lines";
    if (lines <= 100) return "51-100 lines";
    return "101+ lines";
}

std::string depthBucket(int depth) {
    if (depth >= 3) return "depth 3+";
    return "depth " + std::to_string(depth);
}

void writeBreakdown(std::ostream& out, const std::string& title,
                    const std::vector<std::string>& order, const std::vector<std::string>& bucketOf,
                    const std::vector<std::string>& truth, const std::vector<std::string>& rules,
                    const std::vector<std::string>& knn) {
    out << "\n  By " << title << ":\n    ";
    cell(out, "", 15);
    cell(out, "examples", 10);
    cell(out, "rules acc", 11);
    cell(out, "KNN acc", 9);
    cell(out, "rules F1", 10);
    out << "KNN F1\n";
    for (const std::string& bucket : order) {
        std::vector<std::size_t> in;
        for (std::size_t i = 0; i < bucketOf.size(); ++i) {
            if (bucketOf[i] == bucket) in.push_back(i);
        }
        if (in.empty()) continue;
        const Metrics r = computeMetrics(pick(truth, in), pick(rules, in));
        const Metrics k = computeMetrics(pick(truth, in), pick(knn, in));
        out << "    ";
        cell(out, bucket, 15);
        cell(out, std::to_string(in.size()), 10);
        cell(out, percent(r.accuracy()), 11);
        cell(out, percent(k.accuracy()), 9);
        cell(out, fixed2(r.macroF1()), 10);
        out << fixed2(k.macroF1()) << '\n';
    }
}

}

double Metrics::macroF1() const {
    if (perClass.empty()) return 0.0;
    double sum = 0.0;
    for (const ClassMetrics& c : perClass) sum += c.f1;
    return sum / perClass.size();
}

Metrics computeMetrics(const std::vector<std::string>& truth,
                       const std::vector<std::string>& predicted) {
    Metrics m;
    m.total = static_cast<int>(truth.size());
    for (std::size_t i = 0; i < truth.size(); ++i) {
        const std::string col = column(predicted[i]);
        if (std::find(m.rows.begin(), m.rows.end(), truth[i]) == m.rows.end()) {
            m.rows.push_back(truth[i]);
        }
        if (std::find(m.columns.begin(), m.columns.end(), col) == m.columns.end()) {
            m.columns.push_back(col);
        }
        if (col != kNoAnswer && col != "Unknown") ++m.answered;
        if (col == truth[i]) ++m.correct;
    }
    sortByClass(m.rows);
    sortByClass(m.columns);

    m.confusion.assign(m.rows.size(), std::vector<int>(m.columns.size(), 0));
    for (std::size_t i = 0; i < truth.size(); ++i) {
        const std::size_t r = std::find(m.rows.begin(), m.rows.end(), truth[i]) - m.rows.begin();
        const std::size_t c =
            std::find(m.columns.begin(), m.columns.end(), column(predicted[i])) - m.columns.begin();
        ++m.confusion[r][c];
    }

    for (const std::string& label : m.rows) {
        ClassMetrics c;
        c.label = label;
        for (std::size_t i = 0; i < truth.size(); ++i) {
            const bool isLabel = truth[i] == label;
            const bool saysLabel = column(predicted[i]) == label;
            if (isLabel) ++c.support;
            if (saysLabel) ++c.predicted;
            if (isLabel && saysLabel) ++c.correct;
        }
        c.precision = c.predicted == 0 ? 0.0 : double(c.correct) / c.predicted;
        c.recall = c.support == 0 ? 0.0 : double(c.correct) / c.support;
        c.f1 = c.precision + c.recall == 0 ? 0.0
                                           : 2 * c.precision * c.recall / (c.precision + c.recall);
        m.perClass.push_back(c);
    }
    return m;
}

std::vector<std::vector<Neighbour>> leaveOneOutRanking(const std::vector<std::vector<double>>& rows,
                                                       const std::vector<std::string>& labels) {
    std::vector<std::vector<Neighbour>> ranked;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const KnnClassifier knn(rows, labels, i);
        ranked.push_back(knn.rank(rows[i], i));
    }
    return ranked;
}

void writeEvaluation(std::ostream& out, const Dataset& dataset, const EvaluationOptions& options) {
    const std::vector<Example>& examples = dataset.examples;
    out << "CodeComplex evaluation\n";
    out << "Examples: " << examples.size() << " (reviewed rows whose program could be read)\n";
    for (const std::string& warning : dataset.warnings) out << "warning: " << warning << '\n';
    if (!dataset.errors.empty()) {
        out << "Skipped because of errors: " << dataset.errors.size()
            << " (listed at the end)\n";
    }
    if (examples.size() < 2) {
        out << "\nAt least 2 examples are needed for leave-one-out; nothing to evaluate.\n";
        return;
    }

    std::vector<std::vector<double>> rows;
    std::vector<std::string> truth, rules;
    int unsupported = 0;
    for (const Example& e : examples) {
        rows.push_back(e.features.values());
        truth.push_back(e.complexity);
        rules.push_back(e.ruleComplexity.notation());
        if (e.hasUnsupported) ++unsupported;
    }
    out << "Programs with unsupported code in a counted function: " << unsupported << " of "
        << examples.size() << '\n';

    const std::vector<std::vector<Neighbour>> ranked = leaveOneOutRanking(rows, truth);
    const std::vector<std::string> knn = knnLabels(ranked, truth, options.knn);
    out << "KNN setting: k = " << options.knn.k << ", distance limit "
        << distanceText(options.knn.maxDistance) << ", minimum confidence "
        << percent(options.knn.minConfidence) << ", features: ";
    const std::vector<std::string> names = FeatureVector::names();
    for (std::size_t f = 0; f < names.size(); ++f) out << (f ? ", " : "") << names[f];
    out << '\n';

    std::vector<std::pair<std::string, std::vector<std::size_t>>> subsets;
    for (const char* source : {"corcod", "cpp"}) {
        std::vector<std::size_t> in;
        for (std::size_t i = 0; i < examples.size(); ++i) {
            if (examples[i].source == source) in.push_back(i);
        }
        if (!in.empty()) {
            subsets.push_back({std::string(source) == "corcod" ? "CoRCoD (Java)" : "C++ set", in});
        }
    }
    if (subsets.size() > 1) {
        std::vector<std::size_t> all(examples.size());
        for (std::size_t i = 0; i < all.size(); ++i) all[i] = i;
        subsets.push_back({"Combined", all});
    }
    for (const auto& subset : subsets) {
        out << "\n== " << subset.first << ": " << subset.second.size() << " examples ==\n";
        writeMetrics(out, "Rules", computeMetrics(pick(truth, subset.second), pick(rules, subset.second)));
        writeMetrics(out, "KNN (leave-one-out)",
                     computeMetrics(pick(truth, subset.second), pick(knn, subset.second)));
    }

    out << "\n== By code size and nesting depth (all examples) ==\n";
    std::vector<std::string> loc, depth;
    for (const Example& e : examples) {
        loc.push_back(locBucket(e.features.linesOfCode));
        depth.push_back(depthBucket(e.features.maxNestingDepth));
    }
    writeBreakdown(out, "lines of code",
                   {"1-20 lines", "21-50 lines", "51-100 lines", "101+ lines"}, loc, truth, rules, knn);
    writeBreakdown(out, "loop nesting depth", {"depth 0", "depth 1", "depth 2", "depth 3+"}, depth,
                   truth, rules, knn);

    out << "\n== KNN settings (leave-one-out, all examples) ==\n";
    out << "\n  k sweep (distance limit " << distanceText(options.knn.maxDistance) << "):\n    ";
    cell(out, "k", 6);
    cell(out, "accuracy", 10);
    out << "macro F1\n";
    for (int k : options.kSweep) {
        KnnOptions o = options.knn;
        o.k = k;
        const Metrics m = computeMetrics(truth, knnLabels(ranked, truth, o));
        out << "    ";
        cell(out, std::to_string(k), 6);
        cell(out, percent(m.accuracy()), 10);
        out << fixed2(m.macroF1()) << '\n';
    }

    std::vector<double> distances = options.distanceSweep;
    if (distances.empty()) distances = {std::numeric_limits<double>::infinity(), 1.0, 0.5, 0.3, 0.2, 0.1};
    out << "\n  distance limit sweep (k = " << options.knn.k << "):\n    ";
    cell(out, "limit", 8);
    cell(out, "answered", 10);
    cell(out, "accuracy", 10);
    out << "accuracy when answered\n";
    for (double d : distances) {
        KnnOptions o = options.knn;
        o.maxDistance = d;
        const Metrics m = computeMetrics(truth, knnLabels(ranked, truth, o));
        out << "    ";
        cell(out, distanceText(d), 8);
        cell(out, percent(m.coverage()), 10);
        cell(out, percent(m.accuracy()), 10);
        out << (m.answered == 0 ? "-" : percent(m.accuracyWhenAnswered())) << '\n';
    }

    std::vector<std::string> improvement;
    std::vector<std::size_t> labelled;
    for (std::size_t i = 0; i < examples.size(); ++i) {
        const Example& e = examples[i];
        improvement.push_back(e.bestComplexity.empty() ? ""
                                                       : e.inefficiency + " -> " + e.bestComplexity);
        if (!e.bestComplexity.empty()) labelled.push_back(i);
    }
    out << "\n== Improvement vote (inefficiency -> best complexity) ==\n";
    if (labelled.size() < 2) {
        out << "  " << labelled.size()
            << " example(s) have improvement labels; at least 2 are needed. Not evaluated.\n";
    } else {
        const std::vector<std::vector<Neighbour>> rankedImprovement =
            leaveOneOutRanking(rows, improvement);
        std::vector<std::string> said;
        for (std::size_t i : labelled) {
            const Prediction p = vote(rankedImprovement[i], improvement, options.knn);
            said.push_back(p.ok ? p.label : kNoAnswer);
        }
        const Metrics m = computeMetrics(pick(improvement, labelled), said);
        out << "  " << labelled.size() << " labelled examples: exact match " << percent(m.accuracy())
            << " (" << m.correct << " of " << m.total << "), answered " << percent(m.coverage())
            << '\n';
    }

    if (!dataset.errors.empty()) {
        out << "\n== Skipped rows ==\n";
        for (const std::string& error : dataset.errors) out << "  " << error << '\n';
    }
}

}
