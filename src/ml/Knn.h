#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace codecomplex {

struct Scaler {
    std::vector<double> low, high;

    static Scaler fit(const std::vector<std::vector<double>>& rows);
    std::vector<double> apply(const std::vector<double>& values) const;
};

double euclideanDistance(const std::vector<double>& a, const std::vector<double>& b);

struct KnnOptions {
    int k = 5;
    double maxDistance = std::numeric_limits<double>::infinity();
    double minConfidence = 0.0;
};

struct Neighbour {
    std::size_t index = 0;
    double distance = 0.0;
};

struct Prediction {
    bool ok = false;
    std::string label;
    int votes = 0;
    int k = 0;
    double confidence = 0.0;
    std::vector<Neighbour> neighbours;
    std::string note;
};

class KnnClassifier {
public:
    KnnClassifier(std::vector<std::vector<double>> rows, std::vector<std::string> labels,
                  std::size_t exclude);

    Prediction predict(const std::vector<double>& features, const KnnOptions& options,
                       std::size_t exclude) const;

    std::vector<Neighbour> rank(const std::vector<double>& features, std::size_t exclude) const;

    std::size_t size() const { return labels_.size(); }
    std::size_t labelledCount() const;
    const std::string& label(std::size_t index) const { return labels_[index]; }

private:
    Scaler scaler_;
    std::vector<std::vector<double>> scaled_;
    std::vector<std::string> labels_;
};

Prediction vote(const std::vector<Neighbour>& ranked, const std::vector<std::string>& labels,
                const KnnOptions& options);

}
