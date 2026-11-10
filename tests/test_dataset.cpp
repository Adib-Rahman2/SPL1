#include "TestFramework.h"

#include "analysis/Program.h"
#include "lexer/Lexer.h"
#include "ml/Dataset.h"
#include "ml/Evaluation.h"
#include "structure/Parser.h"

#include <sstream>
#include <string>
#include <vector>

using codecomplex::Complexity;
using codecomplex::Dataset;
using codecomplex::LabelFile;
using codecomplex::Metrics;

namespace {

std::string dataDir(const std::string& name) {
    return std::string(CODECOMPLEX_TEST_DATA_DIR) + "/" + name;
}

bool anyContains(const std::vector<std::string>& lines, const std::string& part) {
    for (const std::string& line : lines) {
        if (line.find(part) != std::string::npos) return true;
    }
    return false;
}

codecomplex::ParseResult parseSource(const std::string& source,
                                     std::vector<codecomplex::Token>& tokens) {
    const codecomplex::LexResult lexed = codecomplex::tokenize(source);
    REQUIRE(lexed.ok());
    tokens = lexed.tokens;
    return codecomplex::parse(lexed.tokens);
}

}

TEST_CASE("dataset: CSV fields, with quotes") {
    std::vector<std::string> fields;
    std::string error;
    REQUIRE(codecomplex::splitCsvLine("a, b ,\"c,d\",\"say \"\"hi\"\"\",", fields, error));
    REQUIRE(fields.size() == 5);
    CHECK_EQ(fields[1], std::string("b"));
    CHECK_EQ(fields[2], std::string("c,d"));
    CHECK_EQ(fields[3], std::string("say \"hi\""));
    CHECK(fields[4].empty());
    CHECK(!codecomplex::splitCsvLine("\"open,x", fields, error));
}

TEST_CASE("dataset: a label file is read and paths are resolved against its folder") {
    const LabelFile labels = codecomplex::readLabelFile(dataDir("knn/cpp/labels.csv"));
    REQUIRE(labels.opened);
    CHECK(labels.errors.empty());
    REQUIRE(labels.rows.size() == 5);
    CHECK_EQ(labels.rows[0].line, 2);
    CHECK_EQ(labels.rows[0].path, dataDir("knn/cpp/constant.cpp"));
    CHECK_EQ(labels.rows[3].bestComplexity, std::string("O(n log n)"));
    CHECK_EQ(labels.rows[3].inefficiency, std::string("nested-comparison"));
    CHECK(!labels.rows[4].reviewed);
}

TEST_CASE("dataset: malformed rows are reported with line numbers and skipped") {
    const LabelFile labels = codecomplex::readLabelFile(dataDir("knn_bad/cpp/labels.csv"));
    REQUIRE(labels.opened);
    CHECK_EQ(labels.rows.size(), std::size_t(2));
    CHECK(anyContains(labels.errors, "labels.csv:3: reviewed must be yes or no"));
    CHECK(anyContains(labels.errors, "labels.csv:4: source must be corcod or cpp"));
    CHECK(anyContains(labels.errors, "labels.csv:5: unknown complexity 'O(n^4)'"));
    CHECK(anyContains(labels.errors, "labels.csv:6: best_complexity and inefficiency"));
    CHECK(anyContains(labels.errors, "labels.csv:7: expected 6 fields, found 3"));
    CHECK(anyContains(labels.errors, "labels.csv:8: a quoted field is not closed"));
    CHECK(!codecomplex::readLabelFile(dataDir("knn/nope.csv")).opened);
}

TEST_CASE("dataset: loading skips unreviewed rows and unreadable programs") {
    const Dataset good = codecomplex::loadDataset({dataDir("knn/cpp/labels.csv")});
    CHECK(good.errors.empty());
    REQUIRE(good.examples.size() == 4);
    CHECK(anyContains(good.warnings, "1 row(s) not reviewed yet"));
    CHECK_EQ(good.examples[1].complexity, std::string("O(n)"));
    CHECK_EQ(good.examples[1].features.loopCount, 1);
    CHECK_EQ(good.examples[1].ruleComplexity.notation(), std::string("O(n)"));
    CHECK_EQ(good.examples[3].features.maxNestingDepth, 2);

    const Dataset bad = codecomplex::loadDataset({dataDir("knn_bad/cpp/labels.csv")});
    CHECK_EQ(bad.examples.size(), std::size_t(1));
    CHECK(anyContains(bad.errors, "labels.csv:9: "));

    const Dataset none = codecomplex::loadDataset({dataDir("knn/missing/labels.csv")});
    CHECK(none.examples.empty());
    CHECK(anyContains(none.errors, "cannot open the label file"));
}

TEST_CASE("dataset: the default label files are the ones that exist") {
    const std::vector<std::string> files = codecomplex::defaultLabelFiles(dataDir("knn"));
    REQUIRE(files.size() == 1);
    CHECK(files[0].find("cpp/labels.csv") != std::string::npos);
    CHECK(codecomplex::defaultLabelFiles(dataDir("nothing-here")).empty());
}

TEST_CASE("program: functions reachable from main, or all without main") {
    std::vector<codecomplex::Token> tokens;
    const codecomplex::ParseResult withMain = parseSource(
        "int helper(int n) { int s = 0; for (int i = 0; i < n; i++) s += i; return s; }\n"
        "int unused(int n) { for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) {} return 0; }\n"
        "int main() { return helper(5); }\n", tokens);
    const std::vector<std::size_t> reached = codecomplex::reachableFromMain(withMain.functions);
    REQUIRE(reached.size() == 2);
    CHECK_EQ(reached[0], std::size_t(0));
    CHECK_EQ(reached[1], std::size_t(2));

    const codecomplex::ProgramAnalysis program = codecomplex::analyzeProgram(withMain, tokens);
    CHECK_EQ(program.features.loopCount, 1);
    CHECK_EQ(program.complexity.notation(), std::string("O(n)"));

    const codecomplex::ParseResult noMain = parseSource(
        "int a(int n) { for (int i = 0; i < n; i++) {} return 0; }\n"
        "int b(int n) { for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) {} return 0; }\n",
        tokens);
    const codecomplex::ProgramAnalysis all = codecomplex::analyzeProgram(noMain, tokens);
    CHECK_EQ(all.functions.size(), std::size_t(2));
    CHECK_EQ(all.features.loopCount, 3);
    CHECK_EQ(all.features.maxNestingDepth, 2);
    CHECK_EQ(all.complexity, Complexity::polynomial(2));
}

TEST_CASE("program: an Unknown function makes the program Unknown") {
    std::vector<codecomplex::Token> tokens;
    const codecomplex::ParseResult parsed = parseSource(
        "int a(int n) { for (int i = 0; i < n; i++) {} return 0; }\n"
        "int b(int n) { while (more()) step(); return 0; }\n", tokens);
    CHECK(codecomplex::analyzeProgram(parsed, tokens).complexity.isUnknown());
}

TEST_CASE("metrics: accuracy, per-class precision / recall / F1 and the confusion matrix") {
    const Metrics m = codecomplex::computeMetrics(
        {"O(1)", "O(1)", "O(n)", "O(n)", "O(n)"},
        {"O(1)", "O(n)", "O(n)", "Unknown", codecomplex::kNoAnswer});
    CHECK_EQ(m.total, 5);
    CHECK_EQ(m.correct, 2);
    CHECK_EQ(m.answered, 3);
    CHECK(m.accuracy() == 0.4);
    REQUIRE(m.perClass.size() == 2);
    CHECK(m.perClass[0].precision == 1.0);
    CHECK(m.perClass[0].recall == 0.5);
    CHECK(m.perClass[1].precision == 0.5);
    CHECK(m.perClass[1].f1 > 0.39 && m.perClass[1].f1 < 0.41);

    REQUIRE(m.columns.size() == 4);
    CHECK_EQ(m.columns[2], std::string("Unknown"));
    CHECK_EQ(m.columns[3], std::string(codecomplex::kNoAnswer));
    CHECK_EQ(m.confusion[0][1], 1);
    CHECK_EQ(m.confusion[1][2], 1);
}

TEST_CASE("metrics: classes outside the eight labels are 'other'") {
    const Metrics m = codecomplex::computeMetrics({"O(n^2)"}, {"O(n^2 log n)"});
    REQUIRE(m.columns.size() == 1);
    CHECK_EQ(m.columns[0], std::string(codecomplex::kOther));
    CHECK(m.perClass[0].predicted == 0);
}

TEST_CASE("evaluation: the report has every section and real counts") {
    const Dataset dataset = codecomplex::loadDataset({dataDir("knn/cpp/labels.csv")});
    codecomplex::EvaluationOptions options;
    options.knn.k = 1;
    options.kSweep = {1, 3};
    std::ostringstream out;
    codecomplex::writeEvaluation(out, dataset, options);
    const std::string text = out.str();
    CHECK(text.find("Examples: 4") != std::string::npos);
    CHECK(text.find("== C++ set: 4 examples ==") != std::string::npos);
    CHECK(text.find("Rules: accuracy 100.0% (4 of 4)") != std::string::npos);
    CHECK(text.find("KNN (leave-one-out)") != std::string::npos);
    CHECK(text.find("fewer than 10 examples: not reliable") != std::string::npos);
    CHECK(text.find("By lines of code") != std::string::npos);
    CHECK(text.find("k sweep") != std::string::npos);
    CHECK(text.find("distance limit sweep") != std::string::npos);
    CHECK(text.find("2 labelled examples: exact match") != std::string::npos);
}
