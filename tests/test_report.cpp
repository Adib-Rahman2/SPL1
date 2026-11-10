#include "TestFramework.h"

#include "analysis/RuleEngine.h"
#include "features/FeatureExtractor.h"
#include "lexer/Lexer.h"
#include "report/TextReport.h"
#include "structure/Parser.h"

#include <sstream>
#include <string>

using codecomplex::ReportOptions;

namespace {

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

std::string reportOf(const std::string& source, const ReportOptions& options,
                     const codecomplex::KnnEstimate& knn = codecomplex::KnnEstimate(),
                     const std::vector<codecomplex::Recommendation>& advice = {}) {
    const codecomplex::LexResult lexed = codecomplex::tokenize(source);
    REQUIRE(lexed.ok());
    const codecomplex::ParseResult parsed = codecomplex::parse(lexed.tokens);
    REQUIRE(!parsed.functions.empty());
    const codecomplex::Function& function = parsed.functions[0];
    std::ostringstream out;
    codecomplex::writeFunctionReport(out, function,
                                     codecomplex::extractFeatures(function, lexed.tokens),
                                     codecomplex::analyzeFunction(function, parsed.functions),
                                     knn, advice, options);
    return out.str();
}

const char* kNested =
    "int pairs(int n) {\n"
    "    int c = 0;\n"
    "    for (int i = 0; i < n; i++)\n"
    "        for (int j = 0; j < n; j++) c++;\n"
    "    return c;\n"
    "}\n";

}

TEST_CASE("report: sections come in the slide 8 order") {
    const std::string text = reportOf(kNested, ReportOptions());
    const std::size_t function = text.find("Function: pairs()   [lines 1-6]");
    const std::size_t features = text.find("  Features\n");
    const std::size_t complexity = text.find("  Complexity\n");
    const std::size_t explanation = text.find("  Explanation\n");
    const std::size_t recommendation = text.find("  Recommendation\n");
    REQUIRE(function != std::string::npos);
    REQUIRE(features != std::string::npos);
    REQUIRE(complexity != std::string::npos);
    REQUIRE(explanation != std::string::npos);
    REQUIRE(recommendation != std::string::npos);
    CHECK(function < features);
    CHECK(features < complexity);
    CHECK(complexity < explanation);
    CHECK(explanation < recommendation);
}

TEST_CASE("report: shows the slide 6 features and the rule result") {
    const std::string text = reportOf(kNested, ReportOptions());
    CHECK(contains(text, "    Loop Count        2\n"));
    CHECK(contains(text, "    Nesting Depth     2\n"));
    CHECK(contains(text, "    Recursion         No\n"));
    CHECK(contains(text, "    Log Pattern       No\n"));
    CHECK(contains(text, "    Function Calls    0\n"));
    CHECK(contains(text, "    Detected Pattern  Nested Loops\n"));
    CHECK(contains(text, "    Estimated Time    O(n^2)\n"));
}

TEST_CASE("report: no KNN estimate shows why; no pattern says so") {
    codecomplex::KnnEstimate off;
    off.unavailable = "turned off (--no-knn)";
    const std::string text = reportOf(kNested, ReportOptions(), off);
    CHECK(contains(text, "    KNN Estimate      turned off (--no-knn)\n"));
    CHECK(contains(text, "  Recommendation\n    No inefficient pattern found.\n"));
}

TEST_CASE("report: a recommendation lists every field") {
    codecomplex::Recommendation r;
    r.title = "Linear search inside a loop";
    r.line = 4;
    r.found = "the loop on j tests a[i] == a[j]";
    r.whySlow = "scans again";
    r.alternative = "use a hash set";
    r.prerequisites = "hashable";
    r.tradeOffs = "memory";
    r.improved = "O(n)";
    r.improvedNote = "from O(n^2)";
    r.source = codecomplex::AdviceSource::Agree;
    r.knnNote = "similar programs";
    const std::string text = reportOf(kNested, ReportOptions(), codecomplex::KnnEstimate(), {r});
    CHECK(contains(text, "    1. Linear search inside a loop   [line 4; rules and KNN agree, high confidence]\n"));
    CHECK(contains(text, "       Found        the loop on j tests a[i] == a[j]\n"));
    CHECK(contains(text, "       Alternative  use a hash set\n"));
    CHECK(contains(text, "       Needs        hashable\n"));
    CHECK(contains(text, "       Trade-offs   memory\n"));
    CHECK(contains(text, "       Improved     O(n)  (from O(n^2))\n"));
    CHECK(contains(text, "       KNN          similar programs\n"));
}

TEST_CASE("report: best possible comes from the first rule pattern with a faster class") {
    ReportOptions noAdvice;
    noAdvice.recommend = false;
    const std::string none = reportOf(kNested, noAdvice);
    CHECK(contains(none, "    Estimated Time    O(n^2)\n    Best Possible     no faster version found\n"));

    codecomplex::Recommendation knnOnly;
    knnOnly.title = "Similar programs: x";
    knnOnly.improved = "O(1)";
    knnOnly.source = codecomplex::AdviceSource::KnnOnly;
    codecomplex::Recommendation rule;
    rule.title = "Linear search inside a loop";
    rule.improved = "O(n)";
    rule.improvedNote = "from O(n^2)";
    rule.source = codecomplex::AdviceSource::RulesOnly;
    const std::string text =
        reportOf(kNested, noAdvice, codecomplex::KnnEstimate(), {knnOnly, rule});
    CHECK(contains(text, "    Best Possible     O(n)  (Linear search inside a loop; from O(n^2))\n"));
    CHECK(!contains(text, "  Recommendation\n"));
}

TEST_CASE("report: the KNN vote is shown next to the rule result") {
    codecomplex::KnnEstimate knn;
    knn.ran = true;
    knn.prediction.ok = true;
    knn.prediction.label = "O(n^2)";
    knn.prediction.votes = 3;
    knn.prediction.k = 5;
    knn.prediction.confidence = 0.6;
    knn.neighbours = {"a.java  O(n^2)  distance 0.100"};
    const std::string text = reportOf(kNested, ReportOptions(), knn);
    CHECK(contains(text, "    KNN Estimate      O(n^2)  (3 of 5 neighbours, 60%; agrees with the rules)\n"));
    CHECK(!contains(text, "a.java"));

    ReportOptions explain;
    explain.explain = true;
    knn.prediction.label = "O(n)";
    const std::string explained = reportOf(kNested, explain, knn);
    CHECK(contains(explained, "    KNN Estimate      O(n)  (differs from the rules)\n"));
    CHECK(contains(explained, "      neighbour       a.java  O(n^2)  distance 0.100\n"));

    codecomplex::KnnEstimate none;
    none.ran = true;
    none.prediction.note = "no similar example (far away)";
    CHECK(contains(reportOf(kNested, ReportOptions(), none),
                   "    KNN Estimate      no similar example (far away)\n"));
}

TEST_CASE("report: line numbers and extra features only with --explain") {
    const std::string plain = reportOf(kNested, ReportOptions());
    CHECK(contains(plain, "    - nested: O(n) iterations x O(n) per iteration = O(n^2)\n"));
    CHECK(!contains(plain, "line 3:"));
    CHECK(!contains(plain, "Lines of Code"));

    ReportOptions explain;
    explain.explain = true;
    const std::string detailed = reportOf(kNested, explain);
    CHECK(contains(detailed, "    - line 3: nested: O(n) iterations x O(n) per iteration = O(n^2)\n"));
    CHECK(contains(detailed, "    Lines of Code     6\n"));
    CHECK(contains(detailed, "    Linear Loops      2\n"));
    CHECK(contains(detailed, "    Log Loops         0\n"));
}

TEST_CASE("report: estimated time has no qualifier; mutual recursion is marked") {
    const std::string fib = reportOf(
        "int fib(int n) {\n    if (n < 2) return n;\n    return fib(n - 1) + fib(n - 2);\n}\n",
        ReportOptions());
    CHECK(contains(fib, "    Recursion         Yes\n"));
    CHECK(contains(fib, "    Estimated Time    O(2^n)\n"));

    const std::string even = reportOf(
        "bool isOdd(int n);\n"
        "bool isEven(int n) { return n == 0 ? true : isOdd(n - 1); }\n"
        "bool isOdd(int n) { return n == 0 ? false : isEven(n - 1); }\n",
        ReportOptions());
    CHECK(contains(even, "    Recursion         Mutual (no direct self-call)\n"));
    CHECK(contains(even, "    Detected Pattern  Mutual Recursion\n"));
    CHECK(contains(even, "    Estimated Time    Unknown\n"));
}

TEST_CASE("report: display names include the class") {
    const codecomplex::LexResult lexed = codecomplex::tokenize(
        "struct Stack { void push(int x) { top++; } int top = 0; };\nint free_fn() { return 1; }\n");
    REQUIRE(lexed.ok());
    const codecomplex::ParseResult parsed = codecomplex::parse(lexed.tokens);
    REQUIRE(parsed.functions.size() == 2);
    CHECK_EQ(codecomplex::displayName(parsed.functions[0]), std::string("Stack::push()"));
    CHECK_EQ(codecomplex::displayName(parsed.functions[1]), std::string("free_fn()"));
}

TEST_CASE("report: lists code outside functions that was not analysed") {
    const codecomplex::LexResult lexed =
        codecomplex::tokenize("template <typename T>\nT id(T x) { return x; }\n");
    REQUIRE(lexed.ok());
    const codecomplex::ParseResult parsed = codecomplex::parse(lexed.tokens);
    std::ostringstream out;
    codecomplex::writeUnsupported(out, parsed.unsupported);
    CHECK(contains(out.str(), "Not analysed (outside any function):\n    - line 1: templates"));

    std::ostringstream empty;
    codecomplex::writeUnsupported(empty, {});
    CHECK(empty.str().empty());
}

TEST_CASE("report: when the rules say Unknown, a unanimous KNN vote is shown as a suggestion") {
    const char* unknownLoop =
        "int walk(int next[], int n) {\n"
        "    int s = 0;\n"
        "    for (int i = 0; i < n; i = next[i]) s++;\n"
        "    return s;\n"
        "}\n";
    codecomplex::KnnEstimate knn;
    knn.ran = true;
    knn.prediction.ok = true;
    knn.prediction.label = "O(n)";
    knn.prediction.votes = 5;
    knn.prediction.k = 5;
    knn.prediction.confidence = 1.0;
    CHECK(contains(reportOf(unknownLoop, ReportOptions(), knn),
                   "    Estimated Time    Unknown (KNN suggests O(n): all 5 neighbours agree)\n"));

    knn.prediction.votes = 4;
    knn.prediction.confidence = 0.8;
    const std::string split = reportOf(unknownLoop, ReportOptions(), knn);
    CHECK(contains(split, "    Estimated Time    Unknown\n"));
    CHECK(!contains(split, "KNN suggests"));

    knn.prediction.votes = 1;
    knn.prediction.k = 1;
    knn.prediction.confidence = 1.0;
    CHECK(!contains(reportOf(unknownLoop, ReportOptions(), knn), "KNN suggests"));
}

TEST_CASE("report: a unanimous KNN vote never replaces an answer from the rules") {
    codecomplex::KnnEstimate knn;
    knn.ran = true;
    knn.prediction.ok = true;
    knn.prediction.label = "O(n)";
    knn.prediction.votes = 5;
    knn.prediction.k = 5;
    knn.prediction.confidence = 1.0;
    const std::string text = reportOf(kNested, ReportOptions(), knn);
    CHECK(contains(text, "    Estimated Time    O(n^2)\n"));
    CHECK(!contains(text, "KNN suggests"));
}
