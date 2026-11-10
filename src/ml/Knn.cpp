#include "ml/Knn.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

namespace codecomplex {

Scaler Scaler::fit(const std::vector<std::vector<double>>& rows) {
    Scaler scaler;
    if (rows.empty()) return scaler;
    scaler.low = rows[0];
    scaler.high = rows[0];
    for (const std::vector<double>& row : rows) {
        for (std::size_t f = 0; f < row.size() && f < scaler.low.size(); ++f) {
            scaler.low[f] = std::min(scaler.low[f], row[f]);
            scaler.high[f] = std::max(scaler.high[f], row[f]);
        }
    }
    return scaler;
}

std::vector<double> Scaler::apply(const std::vector<double>& values) const {
    std::vector<double> scaled(values.size(), 0.0);
    for (std::size_t f = 0; f < values.size() && f < low.size(); ++f) {
        const double range = high[f] - low[f];
        scaled[f] = range > 0 ? (values[f] - low[f]) / range : 0.0;
    }
    return scaled;
}

double euclideanDistance(const std::vector<double>& a, const std::vector<double>& b) {
    double sum = 0.0;
    for (std::size_t f = 0; f < a.size() && f < b.size(); ++f) {
        const double d = a[f] - b[f];
        sum += d * d;
    }
    return std::sqrt(sum);
}

KnnClassifier::KnnClassifier(std::vector<std::vector<double>> rows, std::vector<std::string> labels,
                             std::size_t exclude)
    : labels_(std::move(labels)) {
    std::vector<std::vector<double>> training;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (i != exclude && !labels_[i].empty()) training.push_back(rows[i]);
    }
    scaler_ = Scaler::fit(training);
    for (const std::vector<double>& row : rows) scaled_.push_back(scaler_.apply(row));
}

std::size_t KnnClassifier::labelledCount() const {
    std::size_t count = 0;
    for (const std::string& label : labels_) {
        if (!label.empty()) ++count;
    }
    return count;
}

std::vector<Neighbour> KnnClassifier::rank(const std::vector<double>& features,
                                           std::size_t exclude) const {
    const std::vector<double> query = scaler_.apply(features);
    std::vector<Neighbour> ranked;
    for (std::size_t i = 0; i < scaled_.size(); ++i) {
        if (i == exclude || labels_[i].empty()) continue;
        ranked.push_back({i, euclideanDistance(query, scaled_[i])});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const Neighbour& a, const Neighbour& b) {
        return a.distance < b.distance;
    });
    return ranked;
}

Prediction KnnClassifier::predict(const std::vector<double>& features, const KnnOptions& options,
                                  std::size_t exclude) const {
    return vote(rank(features, exclude), labels_, options);
}

Prediction vote(const std::vector<Neighbour>& ranked, const std::vector<std::string>& labels,
                const KnnOptions& options) {
    Prediction prediction;
    prediction.k = options.k;
    if (ranked.empty()) {
        prediction.note = "no labelled examples to compare with";
        return prediction;
    }
    const std::size_t k = std::min<std::size_t>(static_cast<std::size_t>(options.k), ranked.size());
    for (std::size_t i = 0; i < k; ++i) {
        if (ranked[i].distance <= options.maxDistance) prediction.neighbours.push_back(ranked[i]);
    }
    if (prediction.neighbours.empty()) {
        std::ostringstream note;
        note << "no similar example (the nearest is at distance " << ranked[0].distance
             << ", above the limit " << options.maxDistance << ")";
        prediction.note = note.str();
        return prediction;
    }

    struct Tally {
        int votes = 0;
        double distance = 0.0;
        std::size_t nearest = 0;
    };
    std::map<std::string, Tally> tallies;
    for (std::size_t i = 0; i < prediction.neighbours.size(); ++i) {
        const Neighbour& n = prediction.neighbours[i];
        Tally& tally = tallies[labels[n.index]];
        if (tally.votes == 0) tally.nearest = i;
        ++tally.votes;
        tally.distance += n.distance;
    }
    const Tally* best = nullptr;
    for (const auto& entry : tallies) {
        const Tally& t = entry.second;
        const bool better =
            best == nullptr || t.votes > best->votes ||
            (t.votes == best->votes &&
             (t.distance < best->distance ||
              (t.distance == best->distance && t.nearest < best->nearest)));
        if (better) {
            best = &t;
            prediction.label = entry.first;
        }
    }
    prediction.votes = best->votes;
    prediction.confidence = static_cast<double>(best->votes) / options.k;

    if (prediction.confidence < options.minConfidence) {
        std::ostringstream note;
        note << "no similar example (the best vote, " << prediction.label << ", has only "
             << best->votes << " of " << options.k << " neighbours)";
        prediction.note = note.str();
        prediction.label.clear();
        return prediction;
    }
    prediction.ok = true;
    if (static_cast<int>(prediction.neighbours.size()) < options.k) {
        prediction.note = "only " + std::to_string(prediction.neighbours.size()) + " of " +
                          std::to_string(options.k) + " neighbours were close enough to vote";
    }
    return prediction;
}

}
