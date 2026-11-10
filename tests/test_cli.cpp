#include "TestFramework.h"

#include "cli/Cli.h"

#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using codecomplex::CliParseResult;
using codecomplex::Command;
using codecomplex::parseArgs;

namespace {

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

std::string dataFile(const std::string& name) {
    return std::string(CODECOMPLEX_TEST_DATA_DIR) + "/" + name;
}

struct Run {
    int exitCode = -1;
    std::string out;
    std::string err;
};

Run run(const std::vector<std::string>& args) {
    std::ostringstream out, err;
    Run r;
    r.exitCode = codecomplex::runCli(args, out, err);
    r.out = out.str();
    r.err = err.str();
    return r;
}

}

TEST_CASE("cli: analyze with defaults") {
    const CliParseResult r = parseArgs({"analyze", "a.cpp"});
    REQUIRE(r.ok);
    CHECK(r.options.command == Command::Analyze);
    CHECK_EQ(r.options.file, std::string("a.cpp"));
    CHECK(!r.options.explain);
    CHECK(r.options.useKnn);
    CHECK_EQ(r.options.k, 5);
    CHECK(r.options.functionName.empty());
    CHECK(r.options.csvPath.empty());
}

TEST_CASE("cli: every analyze option, in any order") {
    const CliParseResult r = parseArgs({"analyze", "--explain", "--k", "3", "a.cpp", "--no-knn",
                                        "--function", "Stack::push", "--csv", "out.csv",
                                        "--json", "out.json"});
    REQUIRE(r.ok);
    CHECK_EQ(r.options.file, std::string("a.cpp"));
    CHECK(r.options.explain);
    CHECK(!r.options.useKnn);
    CHECK_EQ(r.options.k, 3);
    CHECK_EQ(r.options.functionName, std::string("Stack::push"));
    CHECK_EQ(r.options.csvPath, std::string("out.csv"));
    CHECK_EQ(r.options.jsonPath, std::string("out.json"));
}

TEST_CASE("cli: help and version") {
    CHECK(parseArgs({"--help"}).options.command == Command::Help);
    CHECK(parseArgs({"-h"}).options.command == Command::Help);
    CHECK(parseArgs({"--version"}).options.command == Command::Version);
    CHECK(!parseArgs({"--version", "x"}).ok);
}

TEST_CASE("cli: usage errors are rejected with a message") {
    CHECK(contains(parseArgs({}).error, "no command"));
    CHECK(contains(parseArgs({"frobnicate"}).error, "unknown command 'frobnicate'"));
    CHECK(contains(parseArgs({"analyze"}).error, "needs a file"));
    CHECK(contains(parseArgs({"analyze", "a.cpp", "b.cpp"}).error, "one file"));
    CHECK(contains(parseArgs({"analyze", "a.cpp", "--fast"}).error, "unknown option '--fast'"));
    CHECK(contains(parseArgs({"analyze", "a.cpp", "--function"}).error, "--function needs a value"));
    CHECK(contains(parseArgs({"analyze", "a.cpp", "--csv", "--explain"}).error,
                   "--csv needs a value"));
}

TEST_CASE("cli: --k must be an odd whole number >= 1") {
    CHECK(parseArgs({"analyze", "a.cpp", "--k", "1"}).ok);
    CHECK(parseArgs({"analyze", "a.cpp", "--k", "7"}).ok);
    CHECK(contains(parseArgs({"analyze", "a.cpp", "--k", "4"}).error, "odd"));
    CHECK(contains(parseArgs({"analyze", "a.cpp", "--k", "0"}).error, "odd"));
    CHECK(!parseArgs({"analyze", "a.cpp", "--k", "-3"}).ok);
    CHECK(!parseArgs({"analyze", "a.cpp", "--k", "3x"}).ok);
    CHECK(!parseArgs({"analyze", "a.cpp", "--k", "99999999999"}).ok);
}

TEST_CASE("cli run: loops.cpp reports every function") {
    const Run r = run({"analyze", dataFile("loops.cpp")});
    CHECK_EQ(r.exitCode, codecomplex::kExitOk);
    CHECK(r.err.empty());
    CHECK(contains(r.out, "Functions found: 4\n"));
    CHECK(contains(r.out, "Function: sumArray()   [lines 4-8]"));
    CHECK(contains(r.out, "Function: findCommon()   [lines 10-18]"));
    CHECK(contains(r.out, "Function: countHalvings()   [lines 20-27]"));
    CHECK(contains(r.out, "Function: Stack::push()   [lines 31-31]"));
    CHECK(contains(r.out, "Estimated Time    O(n^2)"));
    CHECK(contains(r.out, "Estimated Time    O(log n)"));
}

TEST_CASE("cli run: --function picks one function, by name or Class::name") {
    const Run r = run({"analyze", dataFile("loops.cpp"), "--function", "findCommon"});
    CHECK_EQ(r.exitCode, codecomplex::kExitOk);
    CHECK(contains(r.out, "Function: findCommon()"));
    CHECK(!contains(r.out, "Function: sumArray()"));
    CHECK(contains(r.out, "Detected Pattern  Nested Loops"));

    const Run member = run({"analyze", dataFile("loops.cpp"), "--function", "Stack::push"});
    CHECK_EQ(member.exitCode, codecomplex::kExitOk);
    CHECK(contains(member.out, "Function: Stack::push()"));
    CHECK(!contains(member.out, "Function: findCommon()"));
}

TEST_CASE("cli run: --function with an unknown name is an input error") {
    const Run r = run({"analyze", dataFile("loops.cpp"), "--function", "nope"});
    CHECK_EQ(r.exitCode, codecomplex::kExitInput);
    CHECK(contains(r.err, "no function named 'nope'"));
    CHECK(contains(r.err, "sumArray()"));
}

TEST_CASE("cli run: recursion.cpp shows the solved recurrences") {
    const Run r = run({"analyze", dataFile("recursion.cpp"), "--explain"});
    CHECK_EQ(r.exitCode, codecomplex::kExitOk);
    CHECK(contains(r.out, "Detected Pattern  Linear Recursion"));
    CHECK(contains(r.out, "Estimated Time    O(2^n)\n"));
    CHECK(contains(r.out, "Detected Pattern  Divide and Conquer"));
    CHECK(contains(r.out, "Detected Pattern  Mutual Recursion"));
    CHECK(contains(r.out, "- line 4: recursive call factorial(n - 1)"));
}

TEST_CASE("cli run: code outside functions that is not analysed is listed") {
    const Run r = run({"analyze", dataFile("unsupported.cpp")});
    CHECK_EQ(r.exitCode, codecomplex::kExitOk);
    CHECK(contains(r.out, "Function: square()"));
    CHECK(contains(r.out, "Not analysed (outside any function):\n    - line 2: templates"));
}

TEST_CASE("cli run: a file with no function bodies") {
    const Run r = run({"analyze", dataFile("no_functions.cpp")});
    CHECK_EQ(r.exitCode, codecomplex::kExitOk);
    CHECK(contains(r.out, "Functions found: 0\n"));
    CHECK(contains(r.out, "No function definitions found."));
}

TEST_CASE("cli run: input errors exit with 2") {
    const Run missing = run({"analyze", dataFile("does_not_exist.cpp")});
    CHECK_EQ(missing.exitCode, codecomplex::kExitInput);
    CHECK(contains(missing.err, "error: "));
    CHECK(missing.out.empty());

    const Run lexError = run({"analyze", dataFile("lex_error.cpp")});
    CHECK_EQ(lexError.exitCode, codecomplex::kExitInput);
    CHECK(contains(lexError.err, "lex_error.cpp:2:14: error: unexpected character '@'"));
    CHECK(lexError.out.empty());
}

TEST_CASE("cli run: usage errors exit with 1 and print the usage") {
    const Run r = run({"analyze"});
    CHECK_EQ(r.exitCode, codecomplex::kExitUsage);
    CHECK(contains(r.err, "error: analyze needs a file"));
    CHECK(contains(r.err, "Usage:"));
    CHECK(r.out.empty());
}

TEST_CASE("cli run: --csv and --json write one entry per function") {
    const std::string csv = std::string(CODECOMPLEX_TEST_OUTPUT_DIR) + "/cli_loops.csv";
    const std::string json = std::string(CODECOMPLEX_TEST_OUTPUT_DIR) + "/cli_loops.json";
    const Run r = run({"analyze", dataFile("loops.cpp"), "--no-knn", "--csv", csv, "--json", json});
    CHECK_EQ(r.exitCode, codecomplex::kExitOk);
    CHECK(r.err.empty());
    CHECK(contains(r.out, "CSV written: " + csv + " (4 functions)"));
    CHECK(contains(r.out, "JSON written: " + json + " (4 functions)"));

    std::ifstream csvIn(csv);
    std::string line;
    int lines = 0;
    while (std::getline(csvIn, line)) ++lines;
    CHECK_EQ(lines, 5);
    std::ifstream jsonIn(json);
    const std::string text((std::istreambuf_iterator<char>(jsonIn)), std::istreambuf_iterator<char>());
    CHECK(contains(text, "\"name\": \"findCommon()\""));
}

TEST_CASE("cli run: an export that cannot be written exits with 2 after the report") {
    const Run r = run({"analyze", dataFile("loops.cpp"), "--no-knn", "--csv",
                       dataFile("no_such_dir/out.csv")});
    CHECK_EQ(r.exitCode, codecomplex::kExitInput);
    CHECK(contains(r.out, "Function: findCommon()"));
    CHECK(contains(r.err, "error: cannot open"));
}

TEST_CASE("cli run: --help and --version print to the output stream") {
    const Run help = run({"--help"});
    CHECK_EQ(help.exitCode, codecomplex::kExitOk);
    CHECK(contains(help.out, "Usage:"));
    const Run version = run({"--version"});
    CHECK_EQ(version.exitCode, codecomplex::kExitOk);
    CHECK(contains(version.out, "CodeComplex "));
}

TEST_CASE("cli: KNN options and evaluate are parsed") {
    const CliParseResult a = parseArgs({"analyze", "a.cpp", "--data", "d", "--max-distance", "0.5",
                                        "--min-confidence", "0.6"});
    REQUIRE(a.ok);
    CHECK_EQ(a.options.dataDir, std::string("d"));
    CHECK(a.options.maxDistance == 0.5);
    CHECK(a.options.minConfidence == 0.6);
    CHECK_EQ(parseArgs({"analyze", "a.cpp"}).options.dataDir, std::string("data"));

    const CliParseResult e = parseArgs({"evaluate", "--k", "3"});
    REQUIRE(e.ok);
    CHECK(e.options.command == Command::Evaluate);
    CHECK_EQ(e.options.k, 3);

    CHECK(contains(parseArgs({"evaluate", "x.cpp"}).error, "evaluate takes no file"));
    CHECK(contains(parseArgs({"evaluate", "--explain"}).error, "evaluate does not take --explain"));
    CHECK(contains(parseArgs({"analyze", "a.cpp", "--min-confidence", "2"}).error, "from 0 to 1"));
    CHECK(contains(parseArgs({"analyze", "a.cpp", "--max-distance", "1.2.3"}).error, "needs a number"));
    CHECK(contains(parseArgs({"analyze", "a.cpp", "--max-distance", "x"}).error, "needs a number"));
}

TEST_CASE("cli run: analyze shows the KNN estimate when a dataset is found") {
    const Run r = run({"analyze", dataFile("loops.cpp"), "--function", "findCommon", "--k", "3",
                       "--data", dataFile("knn")});
    CHECK_EQ(r.exitCode, codecomplex::kExitOk);
    CHECK(contains(r.out, "KNN dataset: 4 labelled programs from "));
    CHECK(contains(r.out, "    KNN Estimate      O("));
    CHECK(contains(r.out, " of 3 neighbours"));

    const Run tooBig = run({"analyze", dataFile("loops.cpp"), "--k", "7", "--data", dataFile("knn")});
    CHECK(contains(tooBig.out, "KNN Estimate      k = 7 is more than the 4 labelled examples"));

    const Run missing = run({"analyze", dataFile("loops.cpp"), "--data", dataFile("nothing-here")});
    CHECK_EQ(missing.exitCode, codecomplex::kExitOk);
    CHECK(contains(missing.out, "KNN Estimate      no dataset found in"));

    const Run off = run({"analyze", dataFile("loops.cpp"), "--no-knn"});
    CHECK(contains(off.out, "KNN Estimate      turned off (--no-knn)"));
}

TEST_CASE("cli run: evaluate prints the evaluation, or exits 2 without enough data") {
    const Run r = run({"evaluate", "--data", dataFile("knn"), "--k", "1"});
    CHECK_EQ(r.exitCode, codecomplex::kExitOk);
    CHECK(contains(r.out, "CodeComplex evaluation"));
    CHECK(contains(r.out, "Rules: accuracy"));

    const Run tooBig = run({"evaluate", "--data", dataFile("knn"), "--k", "5"});
    CHECK_EQ(tooBig.exitCode, codecomplex::kExitInput);
    CHECK(contains(tooBig.err, "the dataset has 4"));

    const Run none = run({"evaluate", "--data", dataFile("nothing-here")});
    CHECK_EQ(none.exitCode, codecomplex::kExitInput);
    CHECK(contains(none.err, "no dataset found"));
}

TEST_CASE("cli run: recommendations in the report") {
    const Run r = run({"analyze", dataFile("advice.cpp"), "--no-knn"});
    CHECK_EQ(r.exitCode, codecomplex::kExitOk);
    CHECK(contains(r.out, "1. Linear search inside a loop   [line 6; rules only, medium confidence]"));
    CHECK(contains(r.out, "1. Overlapping recursion without memoization   [line 10; rules only"));
    CHECK(contains(r.out, "Improved     O(n)  (from O(2^n)"));
    CHECK(contains(r.out, "KNN          turned off (--no-knn)"));
    CHECK(contains(r.out, "  Recommendation\n    No inefficient pattern found.\n"));

    const Run knn = run({"analyze", dataFile("advice.cpp"), "--function", "hasDuplicate",
                         "--data", dataFile("knn"), "--k", "1"});
    CHECK(contains(knn.out, "       KNN          "));
    CHECK(!contains(knn.out, "no program in the dataset has improvement labels"));
}

TEST_CASE("cli run: --no-recommend leaves the Recommendation section out") {
    CHECK(parseArgs({"analyze", "a.cpp"}).options.recommend);
    CHECK(!parseArgs({"analyze", "a.cpp", "--no-recommend"}).options.recommend);
    CHECK(contains(parseArgs({"evaluate", "--no-recommend"}).error,
                   "evaluate does not take --no-recommend"));
    const Run r = run({"analyze", dataFile("advice.cpp"), "--no-knn", "--no-recommend"});
    CHECK_EQ(r.exitCode, codecomplex::kExitOk);
    CHECK(!contains(r.out, "Recommendation"));
    CHECK(!contains(r.out, "Why slow"));
    CHECK(contains(r.out, "Best Possible     O(n)  (Linear search inside a loop;"));
    CHECK(contains(r.out, "Function: hasDuplicate()"));
    CHECK(contains(r.out, "  Explanation\n"));
    CHECK(contains(r.out, "Estimated Time    O(n^2)"));
}

TEST_CASE("cli run: --timing prints the phases on stderr") {
    CHECK(parseArgs({"analyze", "a.cpp", "--timing"}).options.timing);
    CHECK(contains(parseArgs({"evaluate", "--timing"}).error, "evaluate does not take --timing"));
    const Run r = run({"analyze", dataFile("loops.cpp"), "--no-knn", "--timing"});
    CHECK_EQ(r.exitCode, codecomplex::kExitOk);
    CHECK(contains(r.err, "timing: "));
    CHECK(contains(r.err, " functions; parse "));
    CHECK(contains(r.err, " ms, total "));
    CHECK(!contains(r.out, "timing:"));
}
