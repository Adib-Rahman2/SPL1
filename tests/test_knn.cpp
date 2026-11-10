#include "TestFramework.h"

#include "ml/Knn.h"

#include <cmath>
#include <string>
#include <vector>

using codecomplex::KnnClassifier;
using codecomplex::KnnOptions;
using codecomplex::Neighbour;
using codecomplex::Prediction;
using codecomplex::Scaler;

namespace {

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

}

TEST_CASE("knn: min-max scaling uses the training rows; a constant feature becomes 0") {
    const Scaler s = Scaler::fit({{0, 5, 10}, {10, 5, 20}});
    const std::vector<double> scaled = s.apply({5, 5, 30});
    CHECK(near(scaled[0], 0.5));
    CHECK(near(scaled[1], 0.0));
    CHECK(near(scaled[2], 2.0));
}

TEST_CASE("knn: euclidean distance") {
    CHECK(near(codecomplex::euclideanDistance({0, 0}, {3, 4}), 5.0));
    CHECK(near(codecomplex::euclideanDistance({1, 1}, {1, 1}), 0.0));
}

TEST_CASE("knn: majority vote of the k nearest, confidence = votes / k") {
    const KnnClassifier knn({{0}, {1}, {2}, {10}, {11}}, {"A", "A", "B", "B", "B"}, 5);
    KnnOptions options;
    options.k = 3;
    const Prediction p = knn.predict({0.5}, options, knn.size());
    REQUIRE(p.ok);
    CHECK_EQ(p.label, std::string("A"));
    CHECK_EQ(p.votes, 2);
    CHECK(near(p.confidence, 2.0 / 3.0));
    REQUIRE(p.neighbours.size() == 3);
    CHECK_EQ(p.neighbours[0].index, std::size_t(0));

    options.k = 5;
    CHECK_EQ(knn.predict({0.5}, options, knn.size()).label, std::string("B"));
}

TEST_CASE("knn: an excluded example is never its own neighbour (leave-one-out)") {
    const KnnClassifier knn({{0}, {0}, {9}}, {"A", "B", "B"}, 0);
    KnnOptions options;
    options.k = 1;
    const Prediction p = knn.predict({0}, options, 0);
    REQUIRE(p.ok);
    CHECK_EQ(p.label, std::string("B"));
    CHECK_EQ(p.neighbours[0].index, std::size_t(1));
}

TEST_CASE("knn: examples with an empty label are ignored") {
    const KnnClassifier knn({{0}, {1}, {5}}, {"", "", "C"}, 3);
    CHECK_EQ(knn.labelledCount(), std::size_t(1));
    KnnOptions options;
    options.k = 3;
    const Prediction p = knn.predict({0}, options, knn.size());
    REQUIRE(p.ok);
    CHECK_EQ(p.label, std::string("C"));
    CHECK(p.note.find("only 1 of 3") != std::string::npos);

    const KnnClassifier none({{0}}, {""}, 1);
    CHECK(!none.predict({0}, options, none.size()).ok);
}

TEST_CASE("knn: a tie goes to the closer label, then to the nearest neighbour") {
    std::vector<Neighbour> ranked = {{0, 1.0}, {1, 2.0}, {2, 2.0}, {3, 4.0}};
    const std::vector<std::string> labels = {"A", "B", "B", "A"};
    KnnOptions options;
    options.k = 4;
    CHECK_EQ(codecomplex::vote(ranked, labels, options).label, std::string("B"));

    ranked = {{0, 1.0}, {1, 1.5}, {2, 1.5}, {3, 2.0}};
    CHECK_EQ(codecomplex::vote(ranked, labels, options).label, std::string("A"));
}

TEST_CASE("knn: distance and confidence thresholds give no vote") {
    const KnnClassifier knn({{0}, {1}, {10}}, {"A", "B", "C"}, 3);
    KnnOptions options;
    options.k = 3;
    options.maxDistance = 0.05;
    Prediction p = knn.predict({5}, options, knn.size());
    CHECK(!p.ok);
    CHECK(p.note.find("no similar example") != std::string::npos);

    options.maxDistance = 100;
    options.minConfidence = 0.5;
    p = knn.predict({5}, options, knn.size());
    CHECK(!p.ok);
    CHECK(p.label.empty());
    CHECK(p.note.find("only 1 of 3") != std::string::npos);
}
