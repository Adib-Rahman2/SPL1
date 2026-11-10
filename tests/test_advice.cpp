#include "TestFramework.h"

#include "advice/PatternDetector.h"
#include "advice/Recommender.h"
#include "analysis/RuleEngine.h"
#include "lexer/Lexer.h"
#include "structure/Parser.h"

#include <string>
#include <vector>

using codecomplex::AdviceSource;
using codecomplex::DetectedPattern;
using codecomplex::ImprovementVote;
using codecomplex::PatternKind;
using codecomplex::Recommendation;
using codecomplex::RuleResult;

namespace {

struct Analysed {
    codecomplex::Function function;
    RuleResult rules;
    std::vector<DetectedPattern> patterns;
};

Analysed analyse(const std::string& source) {
    const codecomplex::LexResult lexed = codecomplex::tokenize(source);
    REQUIRE(lexed.ok());
    const codecomplex::ParseResult parsed = codecomplex::parse(lexed.tokens);
    REQUIRE(!parsed.functions.empty());
    Analysed a;
    a.function = parsed.functions[0];
    a.rules = codecomplex::analyzeFunction(a.function, parsed.functions);
    a.patterns = codecomplex::detectPatterns(a.function, a.rules);
    return a;
}

int count(const std::vector<DetectedPattern>& patterns, PatternKind kind) {
    int n = 0;
    for (const DetectedPattern& p : patterns) {
        if (p.kind == kind) ++n;
    }
    return n;
}

ImprovementVote vote(const std::string& label) {
    ImprovementVote v;
    v.ran = true;
    v.prediction.ok = true;
    v.prediction.label = label;
    v.prediction.votes = 3;
    v.prediction.k = 5;
    v.prediction.confidence = 0.6;
    return v;
}

const char* kDuplicates =
    "bool dup(int a[], int n) {\n"
    "    for (int i = 0; i < n; i++)\n"
    "        for (int j = 0; j < n; j++)\n"
    "            if (i != j && a[i] == a[j]) return true;\n"
    "    return false;\n"
    "}\n";

const char* kFib =
    "long fib(int n) {\n"
    "    if (n <= 1) return n;\n"
    "    return fib(n - 1) + fib(n - 2);\n"
    "}\n";

}

TEST_CASE("patterns: linear search inside a loop") {
    const Analysed a = analyse(kDuplicates);
    REQUIRE(a.patterns.size() == 1);
    CHECK(a.patterns[0].kind == PatternKind::LinearSearchInLoop);
    CHECK_EQ(a.patterns[0].line, 4);
    CHECK(a.patterns[0].detail.find("a[i] == a[j]") != std::string::npos);

    CHECK_EQ(count(analyse("bool f(int a[], int n) {\n"
                           "  for (int i = 0; i < n; i++)\n"
                           "    for (int j = i + 1; j < n; j++)\n"
                           "      for (int k = j + 1; k < n; k++)\n"
                           "        if (a[i] + a[j] + a[k] == 0) return true;\n"
                           "  return false;\n}\n").patterns,
                 PatternKind::LinearSearchInLoop),
             1);
}

TEST_CASE("patterns: not a search - a single loop, a constant test, a DP table, a remainder") {
    CHECK(analyse("int f(int a[], int n, int x) {\n"
                  "  for (int i = 0; i < n; i++) if (a[i] == x) return i;\n  return -1;\n}\n")
              .patterns.empty());
    CHECK(analyse("int f(bool dp[], int n) { int c = 0;\n"
                  "  for (int i = 0; i < n; i++) for (int j = 0; j < i; j++) if (dp[j] == true) c++;\n"
                  "  return c; }\n").patterns.empty());
    CHECK(analyse("int lcs(char x[], char y[], int n, int m) { int L[100][100];\n"
                  "  for (int i = 1; i <= n; i++)\n"
                  "    for (int j = 1; j <= m; j++)\n"
                  "      if (x[i - 1] == y[j - 1]) L[i][j] = L[i - 1][j - 1] + 1;\n"
                  "      else L[i][j] = 0;\n"
                  "  return L[n][m]; }\n").patterns.empty());
    CHECK(analyse("int f(int a[], int n) { int c = 0;\n"
                  "  for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) if (a[j] % a[i] == 0) c++;\n"
                  "  return c; }\n").patterns.empty());
}

TEST_CASE("patterns: std::find in a loop, and Java contains on a list") {
    const Analysed a = analyse(
        "int f(std::vector<int>& a, std::vector<int>& b) { int c = 0;\n"
        "  for (int x : a) if (std::find(b.begin(), b.end(), x) != b.end()) c++;\n"
        "  return c; }\n");
    CHECK_EQ(count(a.patterns, PatternKind::FindInLoop), 1);

    const codecomplex::LexResult lexed = codecomplex::tokenize(
        "class A { int f(List<Integer> xs, Set<Integer> seen, int n) { int c = 0;\n"
        "  for (int i = 0; i < n; i++) { if (xs.contains(i)) c++; if (seen.contains(i)) c++; }\n"
        "  return c; } }\n", codecomplex::Language::Java);
    const codecomplex::ParseResult parsed = codecomplex::parse(lexed.tokens, codecomplex::Language::Java);
    REQUIRE(parsed.functions.size() == 1);
    const std::vector<DetectedPattern> java = codecomplex::detectPatterns(
        parsed.functions[0], codecomplex::analyzeFunction(parsed.functions[0]));
    REQUIRE(count(java, PatternKind::FindInLoop) == 1);
    CHECK(java[0].detail.find("xs.contains(i)") != std::string::npos);

    CHECK(analyse("bool f(std::vector<int>& b, int x) {\n"
                  "  return std::find(b.begin(), b.end(), x) != b.end(); }\n").patterns.empty());
}

TEST_CASE("patterns: an O(n) call in a loop header that does not use the loop variable") {
    const Analysed a = analyse(
        "int f(const char* s) { int c = 0;\n"
        "  for (int i = 0; i < strlen(s); i++) c++;\n"
        "  return c; }\n");
    REQUIRE(count(a.patterns, PatternKind::LoopInvariantCall) == 1);
    CHECK(a.patterns[0].detail.find("strlen(s)") != std::string::npos);
    CHECK(analyse("int f(int n) { int c = 0; for (int i = 0; i < n; i++) c++; return c; }\n")
              .patterns.empty());
}

TEST_CASE("patterns: the same call twice in a statement or in a condition and its branch") {
    const Analysed a = analyse(
        "int f(int x) {\n"
        "  int y = cost(x) + cost(x);\n"
        "  if (weight(x) > 0) {\n"
        "    return weight(x);\n"
        "  }\n"
        "  return y; }\n");
    REQUIRE(count(a.patterns, PatternKind::RepeatedCall) == 2);
    CHECK(a.patterns[0].detail.find("cost(x) is computed twice") != std::string::npos);
    CHECK_EQ(a.patterns[1].line, 4);

    CHECK(analyse("int f(int x) {\n  if (weight(x) > 0) {\n    x++;\n    return weight(x);\n  }\n"
                  "  return 0; }\n").patterns.empty());
    CHECK(analyse("int f(int a, int b) { return std::max(a, b) + std::max(a, b) + rand() + rand(); }\n")
              .patterns.empty());
}

TEST_CASE("patterns: overlapping recursion, but not memoized or divide and conquer") {
    const Analysed a = analyse(kFib);
    REQUIRE(a.patterns.size() == 1);
    CHECK(a.patterns[0].kind == PatternKind::OverlappingRecursion);
    CHECK(a.patterns[0].detail.find("fib(n - 1), fib(n - 2)") != std::string::npos);

    CHECK(analyse("long fib(int n, long memo[]) {\n"
                  "  if (n <= 1) return n;\n"
                  "  if (memo[n] != -1) return memo[n];\n"
                  "  memo[n] = fib(n - 1, memo) + fib(n - 2, memo);\n"
                  "  return memo[n]; }\n").patterns.empty());
    CHECK(analyse("int hanoi(int n, int a, int b, int c) {\n"
                  "  if (n == 0) return 0;\n"
                  "  return hanoi(n - 1, a, c, b) + 1 + hanoi(n - 1, c, b, a); }\n").patterns.empty());
}

TEST_CASE("patterns: quadratic sort, but not a DP comparison that writes another array") {
    const Analysed bubble = analyse(
        "void sort(int a[], int n) {\n"
        "  for (int i = 0; i < n - 1; i++)\n"
        "    for (int j = 0; j < n - 1 - i; j++)\n"
        "      if (a[j] > a[j + 1]) { int t = a[j]; a[j] = a[j + 1]; a[j + 1] = t; }\n"
        "}\n");
    REQUIRE(bubble.patterns.size() == 1);
    CHECK(bubble.patterns[0].kind == PatternKind::QuadraticOrdering);
    CHECK_EQ(bubble.patterns[0].line, 4);

    CHECK(analyse("int lis(int a[], int n, int len[]) {\n"
                  "  for (int i = 1; i < n; i++)\n"
                  "    for (int j = 0; j < i; j++)\n"
                  "      if (a[i] > a[j]) len[i] = len[j] + 1;\n"
                  "  return len[n - 1]; }\n").patterns.empty());
}

TEST_CASE("patterns: prefix recomputed in a nested loop") {
    const Analysed a = analyse(
        "void prefix(int a[], long out[], int n) {\n"
        "  for (int i = 0; i < n; i++) {\n"
        "    long s = 0;\n"
        "    for (int j = 0; j <= i; j++) s += a[j];\n"
        "    out[i] = s;\n"
        "  }\n}\n");
    REQUIRE(a.patterns.size() == 1);
    CHECK(a.patterns[0].kind == PatternKind::PrefixRecomputation);
    CHECK_EQ(a.patterns[0].line, 4);
    CHECK(analyse("long f(int a[], int n) { long best = 0;\n"
                  "  for (int i = 0; i < n; i++) { long s = 0;\n"
                  "    for (int j = 0; j <= i; j++) { s += a[j]; if (s > best) best = s; } }\n"
                  "  return best; }\n").patterns.empty());
}

TEST_CASE("recommend: rules only, with the improved complexity and every field") {
    const Analysed a = analyse(kDuplicates);
    ImprovementVote none;
    none.note = "no program in the dataset has improvement labels yet";
    const std::vector<Recommendation> r = codecomplex::recommend(a.rules, a.patterns, none);
    REQUIRE(r.size() == 1);
    CHECK(r[0].source == AdviceSource::RulesOnly);
    CHECK_EQ(std::string(codecomplex::confidenceName(r[0].source)), std::string("medium"));
    CHECK_EQ(r[0].line, 4);
    CHECK_EQ(r[0].improved, std::string("O(n)"));
    CHECK(r[0].improvedNote.find("from O(n^2)") != std::string::npos);
    CHECK(!r[0].whySlow.empty());
    CHECK(r[0].alternative.find("hash set") != std::string::npos);
    CHECK(!r[0].prerequisites.empty());
    CHECK(!r[0].tradeOffs.empty());
    CHECK_EQ(r[0].knnNote, none.note);
}

TEST_CASE("recommend: rules and KNN agree -> high confidence") {
    const Analysed a = analyse(kDuplicates);
    const std::vector<Recommendation> r =
        codecomplex::recommend(a.rules, a.patterns, vote("nested-linear-search -> O(n)"));
    REQUIRE(r.size() == 1);
    CHECK(r[0].source == AdviceSource::Agree);
    CHECK_EQ(std::string(codecomplex::confidenceName(r[0].source)), std::string("high"));
    CHECK(r[0].knnNote.find("best O(n) (3 of 5 neighbours)") != std::string::npos);
}

TEST_CASE("recommend: KNN disagrees -> rules only, and the KNN's own advice is added") {
    const Analysed a = analyse(kDuplicates);
    const std::vector<Recommendation> r =
        codecomplex::recommend(a.rules, a.patterns, vote("quadratic-sort -> O(n log n)"));
    REQUIRE(r.size() == 2);
    CHECK(r[0].source == AdviceSource::RulesOnly);
    CHECK(r[0].knnNote.find("another inefficiency: quadratic-sort") != std::string::npos);
    CHECK(r[1].source == AdviceSource::KnnOnly);
    CHECK_EQ(r[1].line, 0);
    CHECK_EQ(r[1].improved, std::string("O(n log n)"));

    const std::vector<Recommendation> optimal =
        codecomplex::recommend(a.rules, a.patterns, vote("none -> O(n^2)"));
    REQUIRE(optimal.size() == 1);
    CHECK(optimal[0].knnNote.find("already at their best") != std::string::npos);
}

TEST_CASE("recommend: KNN only when its best complexity beats the rules' estimate") {
    const Analysed a = analyse("int f(int a[], int n, int x) {\n"
                               "  for (int i = 0; i < n; i++) if (a[i] == x) return i;\n"
                               "  return -1;\n}\n");
    REQUIRE(a.patterns.empty());
    const std::vector<Recommendation> r = codecomplex::recommend(
        a.rules, a.patterns, vote("linear-search-on-sorted-data -> O(log n)"));
    REQUIRE(r.size() == 1);
    CHECK(r[0].source == AdviceSource::KnnOnly);
    CHECK_EQ(std::string(codecomplex::confidenceName(r[0].source)), std::string("low"));
    CHECK(r[0].alternative.find("binary search") != std::string::npos);
    CHECK_EQ(r[0].improved, std::string("O(log n)"));

    CHECK(codecomplex::recommend(a.rules, a.patterns, vote("repeated-computation -> O(n)")).empty());
    CHECK(codecomplex::recommend(a.rules, a.patterns, ImprovementVote()).empty());
}

TEST_CASE("recommend: improved complexity for recursion, sorting and repeated calls") {
    const Analysed fib = analyse(kFib);
    const std::vector<Recommendation> r = codecomplex::recommend(fib.rules, fib.patterns, ImprovementVote());
    REQUIRE(r.size() == 1);
    CHECK_EQ(r[0].improved, std::string("O(n)"));
    CHECK(r[0].improvedNote.find("from O(2^n)") != std::string::npos);

    const Analysed repeated = analyse("int f(int x) { return cost(x) + cost(x); }\n");
    const std::vector<Recommendation> same =
        codecomplex::recommend(repeated.rules, repeated.patterns, ImprovementVote());
    REQUIRE(same.size() == 1);
    CHECK(same[0].improved.empty());
    CHECK(same[0].improvedNote.find("the class stays O(1)") != std::string::npos);
}

TEST_CASE("recommend: improvement labels are split at the arrow") {
    std::string inefficiency, best;
    REQUIRE(codecomplex::splitImprovementLabel("brute-force-pairs -> O(n)", inefficiency, best));
    CHECK_EQ(inefficiency, std::string("brute-force-pairs"));
    CHECK_EQ(best, std::string("O(n)"));
    CHECK(!codecomplex::splitImprovementLabel("O(n)", inefficiency, best));
    CHECK(codecomplex::patternMatchesInefficiency(PatternKind::LinearSearchInLoop, "brute-force-pairs"));
    CHECK(!codecomplex::patternMatchesInefficiency(PatternKind::FindInLoop, "quadratic-sort"));
}

TEST_CASE("patterns: an inner loop that only counts is not a recomputed prefix") {
    CHECK_EQ(count(analyse("long long f(int n) { long long s = 0;\n"
                           "  for (int j = 1; j <= n; j *= 2)\n"
                           "    for (int k = 0; k < j; k++) s += (j + k) & 1;\n"
                           "  return s; }\n").patterns,
                   PatternKind::PrefixRecomputation),
             0);
    CHECK_EQ(count(analyse("long f(int n) { long s = 0;\n"
                           "  for (int i = 0; i < n; i++)\n"
                           "    for (int j = 0; j <= i; j++) s += j;\n"
                           "  return s; }\n").patterns,
                   PatternKind::PrefixRecomputation),
             0);
}
