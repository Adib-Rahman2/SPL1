#include "TestFramework.h"

#include "ml/Dataset.h"
#include "report/CsvWriter.h"
#include "report/JsonWriter.h"

#include <sstream>
#include <string>
#include <vector>

using codecomplex::FunctionResult;
using codecomplex::Recommendation;

namespace {

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

FunctionResult sample() {
    FunctionResult r;
    r.file = "dir/a, b.cpp";
    r.name = "pairs()";
    r.startLine = 3;
    r.endLine = 9;
    r.features.loopCount = 2;
    r.features.maxNestingDepth = 2;
    r.features.linesOfCode = 7;
    r.rules.pattern = "Nested Loops";
    r.rules.complexity = codecomplex::Complexity::polynomial(2);
    r.rules.complexity.uncertain = true;
    r.rules.reasons.push_back({4, "nested: \"quoted\"\tand a tab"});
    r.knn.ran = true;
    r.knn.prediction.ok = true;
    r.knn.prediction.label = "O(n^2)";
    r.knn.prediction.votes = 4;
    r.knn.prediction.k = 5;
    r.knn.prediction.confidence = 0.8;
    Recommendation rec;
    rec.title = "Linear search inside a loop";
    rec.line = 6;
    rec.improved = "O(n)";
    rec.found = "a[i] == a[j]\non two lines";
    r.recommendations.push_back(rec);
    Recommendation second;
    second.title = "Same call repeated with the same arguments";
    r.recommendations.push_back(second);
    return r;
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) out.push_back(line);
    return out;
}

}

TEST_CASE("csv: fields are quoted only when needed") {
    CHECK_EQ(codecomplex::csvField("O(n^2)"), std::string("O(n^2)"));
    CHECK_EQ(codecomplex::csvField(""), std::string(""));
    CHECK_EQ(codecomplex::csvField("a,b"), std::string("\"a,b\""));
    CHECK_EQ(codecomplex::csvField("say \"hi\""), std::string("\"say \"\"hi\"\"\""));
    CHECK_EQ(codecomplex::csvField("two\nlines"), std::string("\"two\nlines\""));
    CHECK_EQ(codecomplex::csvField("cr\r"), std::string("\"cr\r\""));
    CHECK_EQ(codecomplex::csvField(" padded"), std::string("\" padded\""));
}

TEST_CASE("csv: one header and one row per function, with the same number of fields") {
    FunctionResult plain = sample();
    plain.file = "a.cpp";
    plain.recommendations[0].found = "x";
    std::ostringstream out;
    codecomplex::writeCsv(out, {plain, plain});
    const std::vector<std::string> text = lines(out.str());
    REQUIRE(text.size() == 3);
    std::vector<std::string> header, row;
    std::string error;
    REQUIRE(codecomplex::splitCsvLine(text[0], header, error));
    REQUIRE(codecomplex::splitCsvLine(text[1], row, error));
    CHECK_EQ(header.size(), std::size_t(26));
    CHECK_EQ(row.size(), header.size());
    CHECK_EQ(header[4], std::string("loop_count"));
    CHECK_EQ(header[12], std::string("sort_calls"));
    CHECK_EQ(row[1], std::string("pairs()"));
    CHECK_EQ(row[4], std::string("2"));
    CHECK_EQ(row[14], std::string("O(n^2)"));
    CHECK_EQ(row[15], std::string("no"));
    CHECK_EQ(row[16], std::string("yes"));
    CHECK_EQ(row[17], std::string("O(n^2)"));
    CHECK_EQ(row[20], std::string("0.80"));
    CHECK_EQ(row[22], std::string("2"));
    CHECK_EQ(row[23], std::string("O(n)"));
    CHECK_EQ(row[25], std::string("Linear search inside a loop; Same call repeated with the same arguments"));
}

TEST_CASE("csv: a comma in the file name is quoted; no KNN vote leaves its fields empty") {
    FunctionResult r = sample();
    r.knn = codecomplex::KnnEstimate();
    r.knn.unavailable = "turned off (--no-knn)";
    r.recommendations.clear();
    std::ostringstream out;
    codecomplex::writeCsv(out, {r});
    const std::vector<std::string> text = lines(out.str());
    REQUIRE(text.size() == 2);
    CHECK(text[1].rfind("\"dir/a, b.cpp\",pairs(),3,9,", 0) == 0);
    std::vector<std::string> row;
    std::string error;
    REQUIRE(codecomplex::splitCsvLine(text[1], row, error));
    REQUIRE(row.size() == 26);
    CHECK(row[17].empty());
    CHECK_EQ(row[21], std::string("turned off (--no-knn)"));
    CHECK_EQ(row[22], std::string("0"));
    CHECK(row[23].empty());
}

TEST_CASE("csv: an unwritable path is an error, not ignored") {
    std::string error;
    CHECK(!codecomplex::writeCsvFile(std::string(CODECOMPLEX_TEST_DATA_DIR) + "/no_such_dir/out.csv",
                                     {sample()}, error));
    CHECK(contains(error, "cannot open"));
}

TEST_CASE("json: strings escape quotes, backslashes and control characters") {
    CHECK_EQ(codecomplex::jsonString("plain"), std::string("\"plain\""));
    CHECK_EQ(codecomplex::jsonString("a\"b\\c"), std::string("\"a\\\"b\\\\c\""));
    CHECK_EQ(codecomplex::jsonString("x\ny\tz\r"), std::string("\"x\\ny\\tz\\r\""));
    CHECK_EQ(codecomplex::jsonString(std::string("\x01", 1)), std::string("\"\\u0001\""));
}

TEST_CASE("json: the document has every part and balanced brackets") {
    std::ostringstream out;
    codecomplex::writeJson(out, "a.cpp", {sample()});
    const std::string text = out.str();
    CHECK(contains(text, "\"file\": \"a.cpp\""));
    CHECK(contains(text, "\"name\": \"pairs()\""));
    CHECK(contains(text, "\"loop_count\": 2"));
    CHECK(contains(text, "\"complexity\": \"O(n^2)\", \"upper_bound\": false, \"uncertain\": true"));
    CHECK(contains(text, "{\"line\": 4, \"text\": \"nested: \\\"quoted\\\"\\tand a tab\"}"));
    CHECK(contains(text, "\"votes\": 4, \"k\": 5, \"confidence\": 0.80"));
    CHECK(contains(text, "\"found\": \"a[i] == a[j]\\non two lines\""));
    CHECK(contains(text, "\"improved_complexity\": \"O(n)\""));
    CHECK(contains(text, "\"improved_complexity\": null"));

    int braces = 0, squares = 0;
    bool inString = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            if (c == '\\') ++i;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') inString = true;
        if (c == '{') ++braces;
        if (c == '}') --braces;
        if (c == '[') ++squares;
        if (c == ']') --squares;
        CHECK(braces >= 0);
        CHECK(squares >= 0);
    }
    CHECK_EQ(braces, 0);
    CHECK_EQ(squares, 0);
    CHECK(!inString);
}

TEST_CASE("json: no functions, no KNN vote") {
    std::ostringstream out;
    codecomplex::writeJson(out, "e.cpp", {});
    CHECK_EQ(out.str(), std::string("{\n  \"file\": \"e.cpp\",\n  \"functions\": []\n}\n"));

    FunctionResult r = sample();
    r.knn = codecomplex::KnnEstimate();
    r.knn.unavailable = "turned off (--no-knn)";
    std::ostringstream noKnn;
    codecomplex::writeJson(noKnn, "a.cpp", {r});
    CHECK(contains(noKnn.str(), "\"knn\": {\"complexity\": null, \"note\": \"turned off (--no-knn)\"}"));
}

TEST_CASE("json: an unwritable path is an error") {
    std::string error;
    CHECK(!codecomplex::writeJsonFile(std::string(CODECOMPLEX_TEST_DATA_DIR) + "/no_such_dir/out.json",
                                      "a.cpp", {}, error));
    CHECK(contains(error, "cannot open"));
}
